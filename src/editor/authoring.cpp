#include "marrow/editor/authoring.hpp"

#include "curve_auto.hpp"
#include "mesh_weight_model.hpp"
#include "timeline_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace marrow::editor {
namespace {

using runtime::json::Document;
using runtime::json::Value;

const Value::Object* effective_animations(
    const ProjectData& project,
    const Document& base_skeleton_document,
    Document* effective_document) {
    *effective_document = build_project_runtime_document(project, base_skeleton_document);
    const Value* animations = runtime::json::find_member(effective_document->root, "animations");
    return animations != nullptr && animations->is_object()
        ? &animations->as_object()
        : nullptr;
}

bool valid_animation_name(std::string_view name) {
    return !name.empty();
}

const AnimationEdit* pending_duration_edit(
    const ProjectData& project,
    std::string_view animation_name) {
    for (auto iterator = project.animation_edits.rbegin();
         iterator != project.animation_edits.rend();
         ++iterator) {
        if (iterator->kind == AnimationEditKind::SetDuration) {
            if (iterator->name == animation_name) {
                return &(*iterator);
            }
            continue;
        }
        // Catalog and opaque operations may change the identity or meaning of
        // a name. Never move a duration mutation across such a barrier.
        break;
    }
    return nullptr;
}

AnimationEdit* coalescible_duration_edit(
    ProjectData* project,
    std::string_view animation_name) {
    return const_cast<AnimationEdit*>(
        pending_duration_edit(*project, animation_name));
}

template <typename TimelineEdit>
void include_animation_timeline_maximum(
    const std::vector<TimelineEdit>& edits,
    std::string_view animation_name,
    double* maximum_time) {
    marrow::editor::timeline_model::include_animation_timeline_maximum(
        edits, animation_name, maximum_time);
}

/**
 * @brief The overlay maximum with managed loop boundaries excluded.
 *
 * A loop-synchronized lane's last key sits at the explicit duration by the
 * boundary contract, so counting it would make an opted-in clip permanently
 * un-shortenable and would grow the duration straight back after a shrink. A
 * lane that is not opted in folds in exactly as before, which is what makes
 * this a bit-exact no-op for every project that does not use MAR-172.
 */
template <typename TimelineEdit>
void include_animation_timeline_maximum_excluding_loop_boundaries(
    const std::vector<TimelineEdit>& edits,
    std::string_view animation_name,
    double* maximum_time) {
    if (maximum_time == nullptr) {
        return;
    }
    for (const TimelineEdit& edit : edits) {
        if (edit.animation_name != animation_name) {
            continue;
        }
        const std::size_t count = edit.keyframes.size();
        const std::size_t limit = edit.loop_sync && count >= 2U ? count - 1U : count;
        for (std::size_t index = 0U; index < limit; ++index) {
            *maximum_time = std::max(*maximum_time, edit.keyframes[index].time);
        }
    }
}

template <typename Edit>
void rename_timeline_edits(std::vector<Edit>* edits, std::string_view from, std::string_view to) {
    for (Edit& edit : *edits) {
        if (edit.animation_name == from) {
            edit.animation_name = std::string(to);
        }
    }
}

template <typename Edit>
void erase_timeline_edits(std::vector<Edit>* edits, std::string_view animation_name) {
    edits->erase(
        std::remove_if(
            edits->begin(),
            edits->end(),
            [&](const Edit& edit) { return edit.animation_name == animation_name; }),
        edits->end());
}

void rename_all_timeline_edits(ProjectData* project, std::string_view from, std::string_view to) {
    rename_timeline_edits(&project->transform_timeline_edits, from, to);
    rename_timeline_edits(&project->mesh_deform_timeline_edits, from, to);
    rename_timeline_edits(&project->draw_order_timeline_edits, from, to);
    rename_timeline_edits(&project->event_timeline_edits, from, to);
    rename_timeline_edits(&project->slot_color_timeline_edits, from, to);
    rename_timeline_edits(&project->slot_attachment_timeline_edits, from, to);
}

void erase_all_timeline_edits(ProjectData* project, std::string_view animation_name) {
    erase_timeline_edits(&project->transform_timeline_edits, animation_name);
    erase_timeline_edits(&project->mesh_deform_timeline_edits, animation_name);
    erase_timeline_edits(&project->draw_order_timeline_edits, animation_name);
    erase_timeline_edits(&project->event_timeline_edits, animation_name);
    erase_timeline_edits(&project->slot_color_timeline_edits, animation_name);
    erase_timeline_edits(&project->slot_attachment_timeline_edits, animation_name);
}

AuthoringResult missing_project_result() {
    return {false, "Animation authoring requires an open project."};
}

AuthoringResult invalid_name_result() {
    return {false, "Animation names must not be empty."};
}

constexpr double kKeyTimeEpsilon = 1e-6;
constexpr double kNonEventKeySpacing = 0.001;

struct ResolvedTimelineKey {
    TimelineKeyKind kind{TimelineKeyKind::Transform};
    std::size_t timeline_index{0U};
    std::size_t key_index{0U};
    double original_time{0.0};
};

template <typename Timeline, typename Matches>
std::optional<std::size_t> matching_timeline_index(
    const std::vector<Timeline>& timelines,
    Matches&& matches) {
    const auto iterator = std::find_if(
        timelines.begin(), timelines.end(), std::forward<Matches>(matches));
    if (iterator == timelines.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(timelines.begin(), iterator));
}

template <typename Keyframe>
std::optional<std::size_t> matching_key_index(
    const std::vector<Keyframe>& keyframes,
    double time,
    std::size_t ordinal) {
    std::size_t matching_ordinal = 0U;
    for (std::size_t index = 0U; index < keyframes.size(); ++index) {
        if (std::abs(keyframes[index].time - time) > kKeyTimeEpsilon) {
            continue;
        }
        if (matching_ordinal == ordinal) {
            return index;
        }
        ++matching_ordinal;
    }
    return std::nullopt;
}

std::string selector_label(const TimelineKeySelector& selector) {
    std::ostringstream stream;
    stream << selector.animation_name << " at " << selector.time;
    return stream.str();
}

std::optional<ResolvedTimelineKey> resolve_timeline_key(
    const ProjectData& project,
    const TimelineKeySelector& selector,
    std::string* error_out) {
    const auto fail = [&](std::string_view family) -> std::optional<ResolvedTimelineKey> {
        *error_out = "Persisted " + std::string(family) + " key not found: " +
            selector_label(selector);
        return std::nullopt;
    };

    switch (selector.kind) {
    case TimelineKeyKind::Transform: {
        const auto timeline_index = matching_timeline_index(
            project.transform_timeline_edits,
            [&](const TransformTimelineEdit& edit) {
                return edit.animation_name == selector.animation_name &&
                    edit.bone_name == selector.bone_name &&
                    edit.channel == selector.transform_channel;
            });
        if (!timeline_index.has_value()) return fail("transform");
        const auto key_index = matching_key_index(
            project.transform_timeline_edits[*timeline_index].keyframes,
            selector.time,
            0U);
        if (!key_index.has_value()) return fail("transform");
        return ResolvedTimelineKey{
            selector.kind, *timeline_index, *key_index, selector.time};
    }
    case TimelineKeyKind::Deform: {
        const auto timeline_index = matching_timeline_index(
            project.mesh_deform_timeline_edits,
            [&](const MeshDeformTimelineEdit& edit) {
                return edit.animation_name == selector.animation_name &&
                    edit.slot_name == selector.slot_name &&
                    edit.attachment_name == selector.attachment_name;
            });
        if (!timeline_index.has_value()) return fail("deform");
        const auto key_index = matching_key_index(
            project.mesh_deform_timeline_edits[*timeline_index].keyframes,
            selector.time,
            0U);
        if (!key_index.has_value()) return fail("deform");
        return ResolvedTimelineKey{
            selector.kind, *timeline_index, *key_index, selector.time};
    }
    case TimelineKeyKind::DrawOrder: {
        const auto timeline_index = matching_timeline_index(
            project.draw_order_timeline_edits,
            [&](const DrawOrderTimelineEdit& edit) {
                return edit.animation_name == selector.animation_name;
            });
        if (!timeline_index.has_value()) return fail("draw-order");
        const auto key_index = matching_key_index(
            project.draw_order_timeline_edits[*timeline_index].keyframes,
            selector.time,
            0U);
        if (!key_index.has_value()) return fail("draw-order");
        return ResolvedTimelineKey{
            selector.kind, *timeline_index, *key_index, selector.time};
    }
    case TimelineKeyKind::Event: {
        const auto timeline_index = matching_timeline_index(
            project.event_timeline_edits,
            [&](const EventTimelineEdit& edit) {
                return edit.animation_name == selector.animation_name;
            });
        if (!timeline_index.has_value()) return fail("event");
        const auto key_index = matching_key_index(
            project.event_timeline_edits[*timeline_index].keyframes,
            selector.time,
            selector.same_time_ordinal);
        if (!key_index.has_value()) return fail("event");
        return ResolvedTimelineKey{
            selector.kind, *timeline_index, *key_index, selector.time};
    }
    case TimelineKeyKind::SlotColor: {
        const auto timeline_index = matching_timeline_index(
            project.slot_color_timeline_edits,
            [&](const SlotColorTimelineEdit& edit) {
                return edit.animation_name == selector.animation_name &&
                    edit.slot_name == selector.slot_name;
            });
        if (!timeline_index.has_value()) return fail("slot-color");
        const auto key_index = matching_key_index(
            project.slot_color_timeline_edits[*timeline_index].keyframes,
            selector.time,
            0U);
        if (!key_index.has_value()) return fail("slot-color");
        return ResolvedTimelineKey{
            selector.kind, *timeline_index, *key_index, selector.time};
    }
    case TimelineKeyKind::SlotAttachment: {
        const auto timeline_index = matching_timeline_index(
            project.slot_attachment_timeline_edits,
            [&](const SlotAttachmentTimelineEdit& edit) {
                return edit.animation_name == selector.animation_name &&
                    edit.slot_name == selector.slot_name;
            });
        if (!timeline_index.has_value()) return fail("slot-attachment");
        const auto key_index = matching_key_index(
            project.slot_attachment_timeline_edits[*timeline_index].keyframes,
            selector.time,
            0U);
        if (!key_index.has_value()) return fail("slot-attachment");
        return ResolvedTimelineKey{
            selector.kind, *timeline_index, *key_index, selector.time};
    }
    }
    *error_out = "Unsupported timeline key kind.";
    return std::nullopt;
}

bool component_is_color_channel(TimelineScalarComponent component) {
    switch (component) {
    case TimelineScalarComponent::Red:
    case TimelineScalarComponent::Green:
    case TimelineScalarComponent::Blue:
    case TimelineScalarComponent::Alpha:
        return true;
    case TimelineScalarComponent::Angle:
    case TimelineScalarComponent::X:
    case TimelineScalarComponent::Y:
        return false;
    }
    return false;
}

bool finite_animation_scalar(double value) {
    return std::isfinite(value) &&
        std::abs(value) <=
            static_cast<double>(std::numeric_limits<runtime::AnimationScalar>::max());
}

/**
 * @brief Reports whether `component` is authorable on one timeline family.
 *
 * Rotate owns Angle; Translate, Scale, and Shear own X and Y; Slot Color owns
 * the four channels. Deform, Draw Order, Event, and Slot Attachment keys own
 * none. `channel` is read only for Transform. This is the single definition
 * `read_scalar_component()` and `curve_driver_is_authorable()` share, so an
 * offset and an automatic curve driver can never disagree about a family.
 */
bool family_owns_scalar_component(
    TimelineKeyKind kind,
    TransformTimelineChannel channel,
    TimelineScalarComponent component) {
    switch (kind) {
    case TimelineKeyKind::Transform:
        if (channel == TransformTimelineChannel::Rotate) {
            return component == TimelineScalarComponent::Angle;
        }
        return component == TimelineScalarComponent::X ||
            component == TimelineScalarComponent::Y;
    case TimelineKeyKind::SlotColor:
        switch (component) {
        case TimelineScalarComponent::Red:
        case TimelineScalarComponent::Green:
        case TimelineScalarComponent::Blue:
        case TimelineScalarComponent::Alpha:
            return true;
        case TimelineScalarComponent::Angle:
        case TimelineScalarComponent::X:
        case TimelineScalarComponent::Y:
            return false;
        }
        return false;
    case TimelineKeyKind::Deform:
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::Event:
    case TimelineKeyKind::SlotAttachment:
        return false;
    }
    return false;
}

/** @brief Reads one authorable scalar of a resolved key; never `time`, never easing. */
bool read_scalar_component(
    const ProjectData& project,
    const ResolvedTimelineKey& resolved,
    TimelineScalarComponent component,
    double* value_out) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform: {
        const auto& timeline = project.transform_timeline_edits[resolved.timeline_index];
        const auto& keyframe = timeline.keyframes[resolved.key_index];
        if (!family_owns_scalar_component(resolved.kind, timeline.channel, component)) {
            return false;
        }
        if (timeline.channel == TransformTimelineChannel::Rotate) {
            *value_out = keyframe.angle;
            return true;
        }
        *value_out =
            component == TimelineScalarComponent::X ? keyframe.x : keyframe.y;
        return true;
    }
    case TimelineKeyKind::SlotColor: {
        const auto& keyframe =
            project.slot_color_timeline_edits[resolved.timeline_index]
                .keyframes[resolved.key_index];
        switch (component) {
        case TimelineScalarComponent::Red:
            *value_out = static_cast<double>(keyframe.color.r);
            return true;
        case TimelineScalarComponent::Green:
            *value_out = static_cast<double>(keyframe.color.g);
            return true;
        case TimelineScalarComponent::Blue:
            *value_out = static_cast<double>(keyframe.color.b);
            return true;
        case TimelineScalarComponent::Alpha:
            *value_out = static_cast<double>(keyframe.color.a);
            return true;
        case TimelineScalarComponent::Angle:
        case TimelineScalarComponent::X:
        case TimelineScalarComponent::Y:
            return false;
        }
        return false;
    }
    case TimelineKeyKind::Deform:
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::Event:
    case TimelineKeyKind::SlotAttachment:
        return false;
    }
    return false;
}

/** @brief Writes exactly one scalar field; never `time` and never easing. */
void write_scalar_component(
    ProjectData* project,
    const ResolvedTimelineKey& resolved,
    TimelineScalarComponent component,
    double value) {
    if (resolved.kind == TimelineKeyKind::Transform) {
        auto& timeline = project->transform_timeline_edits[resolved.timeline_index];
        auto& keyframe = timeline.keyframes[resolved.key_index];
        const bool rotate = timeline.channel == TransformTimelineChannel::Rotate;
        switch (component) {
        case TimelineScalarComponent::Angle:
            if (rotate) keyframe.angle = value;
            return;
        case TimelineScalarComponent::X:
            if (!rotate) keyframe.x = value;
            return;
        case TimelineScalarComponent::Y:
            if (!rotate) keyframe.y = value;
            return;
        case TimelineScalarComponent::Red:
        case TimelineScalarComponent::Green:
        case TimelineScalarComponent::Blue:
        case TimelineScalarComponent::Alpha:
            return;
        }
        return;
    }
    if (resolved.kind == TimelineKeyKind::SlotColor) {
        auto& keyframe = project->slot_color_timeline_edits[resolved.timeline_index]
                             .keyframes[resolved.key_index];
        const auto scalar = static_cast<runtime::AnimationScalar>(value);
        switch (component) {
        case TimelineScalarComponent::Red: keyframe.color.r = scalar; return;
        case TimelineScalarComponent::Green: keyframe.color.g = scalar; return;
        case TimelineScalarComponent::Blue: keyframe.color.b = scalar; return;
        case TimelineScalarComponent::Alpha: keyframe.color.a = scalar; return;
        case TimelineScalarComponent::Angle:
        case TimelineScalarComponent::X:
        case TimelineScalarComponent::Y:
            return;
        }
        return;
    }
}

template <typename Keyframe>
void include_retime_bounds(
    const std::vector<Keyframe>& keyframes,
    const ResolvedTimelineKey& resolved,
    const std::set<std::size_t>& selected_indices,
    double spacing,
    double* minimum_delta,
    double* maximum_delta) {
    marrow::editor::timeline_model::RetimeBounds bounds{
        *minimum_delta, *maximum_delta};
    marrow::editor::timeline_model::include_retime_bounds(
        keyframes,
        resolved.key_index,
        resolved.original_time,
        selected_indices,
        spacing,
        &bounds);
    *minimum_delta = bounds.minimum_delta;
    *maximum_delta = bounds.maximum_delta;
}

template <typename Timeline>
void include_timeline_retime_bounds(
    const std::vector<Timeline>& timelines,
    const ResolvedTimelineKey& resolved,
    const std::vector<ResolvedTimelineKey>& all_resolved,
    double spacing,
    double* minimum_delta,
    double* maximum_delta) {
    std::set<std::size_t> selected_indices;
    for (const ResolvedTimelineKey& selected : all_resolved) {
        if (selected.kind == resolved.kind &&
            selected.timeline_index == resolved.timeline_index) {
            selected_indices.insert(selected.key_index);
        }
    }
    include_retime_bounds(
        timelines[resolved.timeline_index].keyframes,
        resolved,
        selected_indices,
        spacing,
        minimum_delta,
        maximum_delta);
}

/**
 * @brief The minimum time separation two keys of one family must keep.
 *
 * Event timelines return 0.0 because same-time event keys are legal and are
 * distinguished by `same_time_ordinal`; every other family returns the shared
 * `kNonEventKeySpacing`. This is the single definition
 * `include_resolved_retime_bounds()` and `scale_keyframe_times()` share, so a
 * retime and a scale can never disagree about what a collision is.
 */
double family_key_spacing(TimelineKeyKind kind) {
    switch (kind) {
    case TimelineKeyKind::Event:
        return 0.0;
    case TimelineKeyKind::Transform:
    case TimelineKeyKind::Deform:
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::SlotColor:
    case TimelineKeyKind::SlotAttachment:
        return kNonEventKeySpacing;
    }
    return kNonEventKeySpacing;
}

/**
 * @brief Reports whether one resolved key sits on a loop-synchronized end.
 *
 * The first key defines the boundary value at t = 0 and the last key IS the
 * managed boundary at the clip duration, so an opted-in lane treats both as
 * immovable. This is the single definition `include_loop_boundary_retime_pins()`
 * and `scale_keyframe_times()` share, so a retime and a scale can never
 * disagree about which keys an opt-in freezes.
 *
 * This answers "may this key move?", which is deliberately a different question
 * from `managed_boundary_index()`'s "does the boundary contract own this key,
 * so may synchronization overwrite it?". The latter is stricter — it never
 * names key 0, and it disowns a last key that is neither at the boundary nor
 * still the bit-exact mirror of key 0 — because overwriting authored data is a
 * worse failure than refusing to move it.
 */
template <typename Timeline>
bool timeline_key_is_loop_pinned(
    const std::vector<Timeline>& timelines,
    const ResolvedTimelineKey& resolved) {
    const Timeline& timeline = timelines[resolved.timeline_index];
    return timeline.loop_sync &&
        (resolved.key_index == 0U ||
         resolved.key_index + 1U == timeline.keyframes.size());
}

/**
 * @brief Pins both ends of a loop-synchronized lane, for the three continuous
 *        families that carry the flag.
 *
 * Moving either end would leave the lane without the prerequisites its opt-in
 * asserts, so both behave as immovable neighbours. This composes with the
 * existing shared-bounds model rather than fighting it: one immovable key
 * already freezes a whole selection, so a drag that includes one collapses to
 * the existing `changed == false` result.
 */
template <typename Timeline>
void include_loop_boundary_retime_pins(
    const std::vector<Timeline>& timelines,
    const ResolvedTimelineKey& resolved,
    double* minimum_delta,
    double* maximum_delta) {
    if (timeline_key_is_loop_pinned(timelines, resolved)) {
        *minimum_delta = std::max(*minimum_delta, 0.0);
        *maximum_delta = std::min(*maximum_delta, 0.0);
    }
}

/**
 * @brief Reports whether one resolved key is pinned by loop synchronization.
 *
 * Only Transform, Slot Color, and Deform timelines carry the flag; the three
 * discrete families are never pinned. Retime clamps its shared delta to zero
 * for such a key, which is coherent because one immovable key pins the whole
 * selection consistently; a scale rejects instead, because pinning one key
 * while the rest scale produces a shape that is not `p + (t - p) * s` for any
 * `s`.
 */
bool resolved_key_is_loop_pinned(
    const ProjectData& project,
    const ResolvedTimelineKey& resolved) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform:
        return timeline_key_is_loop_pinned(project.transform_timeline_edits, resolved);
    case TimelineKeyKind::Deform:
        return timeline_key_is_loop_pinned(project.mesh_deform_timeline_edits, resolved);
    case TimelineKeyKind::SlotColor:
        return timeline_key_is_loop_pinned(project.slot_color_timeline_edits, resolved);
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::Event:
    case TimelineKeyKind::SlotAttachment:
        return false;
    }
    return false;
}

/**
 * @brief The family and lane a scale rejection names, for one resolved key.
 *
 * Rejections must be actionable, so every message says which lane of which
 * animation refused; this builds that fragment from the resolved key rather
 * than from the selector, so it names the lane the primitive actually walked.
 */
std::string scale_lane_label(
    const ProjectData& project,
    const ResolvedTimelineKey& resolved) {
    const auto channel_token = [](TransformTimelineChannel channel) -> std::string {
        switch (channel) {
        case TransformTimelineChannel::Rotate:
            return "rotate";
        case TransformTimelineChannel::Translate:
            return "translate";
        case TransformTimelineChannel::Scale:
            return "scale";
        case TransformTimelineChannel::Shear:
            return "shear";
        }
        return "rotate";
    };
    switch (resolved.kind) {
    case TimelineKeyKind::Transform: {
        const auto& edit = project.transform_timeline_edits[resolved.timeline_index];
        return "transform key '" + edit.bone_name + "/" +
            channel_token(edit.channel) + "'";
    }
    case TimelineKeyKind::Deform: {
        const auto& edit = project.mesh_deform_timeline_edits[resolved.timeline_index];
        return "deform key '" + edit.slot_name + "/" + edit.attachment_name + "'";
    }
    case TimelineKeyKind::DrawOrder:
        return "draw-order key";
    case TimelineKeyKind::Event:
        return "event key";
    case TimelineKeyKind::SlotColor: {
        const auto& edit = project.slot_color_timeline_edits[resolved.timeline_index];
        return "slot-color key '" + edit.slot_name + "'";
    }
    case TimelineKeyKind::SlotAttachment: {
        const auto& edit =
            project.slot_attachment_timeline_edits[resolved.timeline_index];
        return "slot-attachment key '" + edit.slot_name + "'";
    }
    }
    return "timeline key";
}

/**
 * @brief The time a resolved key actually stores, in full `double` precision.
 *
 * `resolve_timeline_key()` carries the *selector's* time through to
 * `ResolvedTimelineKey::original_time`, and a shell selector is built from the
 * runtime track rows, whose times are already narrowed to `float32`. A retime
 * adds one shared delta and does not care. A scale multiplies, and a live
 * gesture re-derives its selectors from those rows on every frame, so reading
 * the narrowed value would re-round each key through `float32` once per frame
 * and accumulate an error far larger than the `double` composition bound the
 * gesture's commit-time drift check enforces. Scaling therefore snapshots the
 * stored double, which is the value the project is authoritative for.
 */
template <typename Timeline>
double timeline_stored_key_time(
    const std::vector<Timeline>& timelines,
    const ResolvedTimelineKey& resolved) {
    return timelines[resolved.timeline_index].keyframes[resolved.key_index].time;
}

double resolved_stored_key_time(
    const ProjectData& project,
    const ResolvedTimelineKey& resolved) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform:
        return timeline_stored_key_time(project.transform_timeline_edits, resolved);
    case TimelineKeyKind::Deform:
        return timeline_stored_key_time(project.mesh_deform_timeline_edits, resolved);
    case TimelineKeyKind::DrawOrder:
        return timeline_stored_key_time(project.draw_order_timeline_edits, resolved);
    case TimelineKeyKind::Event:
        return timeline_stored_key_time(project.event_timeline_edits, resolved);
    case TimelineKeyKind::SlotColor:
        return timeline_stored_key_time(project.slot_color_timeline_edits, resolved);
    case TimelineKeyKind::SlotAttachment:
        return timeline_stored_key_time(
            project.slot_attachment_timeline_edits, resolved);
    }
    return resolved.original_time;
}

/**
 * @brief The refusal text for a selection that names part of an event tie.
 *
 * A tie one side of which is selected would be split by any `s != 1`, which
 * contradicts the promise that tied event keys stay tied. The primitive names
 * the time rather than widening the caller's selection. This is the single
 * definition `scale_keyframe_times()` and `timeline_scale_selection_refusal()`
 * share.
 */
std::string partial_event_tie_rejection(
    double time,
    std::string_view animation_name) {
    return "Event keys sharing time " + std::to_string(time) + " s in animation '" +
        std::string(animation_name) +
        "' must be scaled together; select every event key at that time.";
}

/** @brief The refusal text for a selection containing a loop-pinned key. */
std::string loop_pinned_rejection(
    const ProjectData& project,
    const ResolvedTimelineKey& resolved,
    std::string_view animation_name) {
    return "Animation '" + std::string(animation_name) + "' timeline " +
        scale_lane_label(project, resolved) +
        " is loop synchronized; its first and last keys are pinned. Disable loop "
        "synchronization on that timeline to scale them.";
}

/** @brief The time of a partially selected same-time group, if one exists. */
template <typename Timeline>
std::optional<double> partial_tie_time(
    const Timeline& timeline,
    const std::set<std::size_t>& selected_indices) {
    for (std::size_t index = 0U; index + 1U < timeline.keyframes.size(); ++index) {
        if (std::abs(
                timeline.keyframes[index + 1U].time - timeline.keyframes[index].time) >
            kKeyTimeEpsilon) {
            continue;
        }
        if ((selected_indices.count(index) != 0U) !=
            (selected_indices.count(index + 1U) != 0U)) {
            return timeline.keyframes[index].time;
        }
    }
    return std::nullopt;
}

/**
 * @brief Validates one affected timeline's whole projected key set.
 *
 * The projected list spans the timeline's entire `keyframes` vector, not just
 * the selected keys, so one adjacent-pair rule covers a selected key colliding
 * with a selected neighbour and a selected key intruding on an unselected one,
 * on either side, without any notion of "the nearest unselected neighbour".
 *
 * The required gap is `min(spacing, original_gap)` rather than a flat
 * `spacing`: a stored timeline can already carry a sub-millisecond gap that an
 * import or a hand-edited document produced, and a flat rule would make every
 * scale on such a timeline impossible even when the scale does not make the gap
 * worse. The `min` form says exactly what is meant — a gap that satisfied the
 * spacing must still satisfy it, and a gap that was already tighter must not
 * get tighter.
 */
template <typename Timeline>
bool validate_projected_scale(
    const Timeline& timeline,
    const std::set<std::size_t>& selected_indices,
    TimelineKeyKind kind,
    std::string_view animation_name,
    const std::string& lane_label,
    double pivot_time,
    double scale,
    std::string* error_out) {
    const double spacing = family_key_spacing(kind);
    const std::size_t count = timeline.keyframes.size();
    std::vector<double> projected;
    projected.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const double time = timeline.keyframes[index].time;
        projected.push_back(
            selected_indices.count(index) != 0U
                ? pivot_time + (time - pivot_time) * scale
                : time);
    }
    for (std::size_t index = 0U; index + 1U < count; ++index) {
        const double original_gap =
            timeline.keyframes[index + 1U].time - timeline.keyframes[index].time;
        const bool selected_low = selected_indices.count(index) != 0U;
        const bool selected_high = selected_indices.count(index + 1U) != 0U;
        if (kind == TimelineKeyKind::Event &&
            std::abs(original_gap) <= kKeyTimeEpsilon &&
            selected_low != selected_high) {
            *error_out = partial_event_tie_rejection(
                timeline.keyframes[index].time, animation_name);
            return false;
        }
        if (!selected_low && !selected_high) {
            continue;
        }
        const double required_gap = std::min(spacing, original_gap);
        const double projected_gap = projected[index + 1U] - projected[index];
        if (projected_gap >= required_gap - kKeyTimeEpsilon) {
            continue;
        }
        *error_out = "Scaling would place animation '" + std::string(animation_name) +
            "' " + lane_label + " at " + std::to_string(projected[index + 1U]) +
            " s, " + std::to_string(projected_gap) + " s from the " +
            (selected_low ? "selected" : "unselected") + " key at " +
            std::to_string(projected[index]) + " s; the minimum separation is " +
            std::to_string(required_gap) + " s.";
        return false;
    }
    return true;
}

template <typename Timeline>
void apply_timeline_scale(
    std::vector<Timeline>* timelines,
    const ResolvedTimelineKey& resolved,
    double pivot_time,
    double scale) {
    (*timelines)[resolved.timeline_index].keyframes[resolved.key_index].time =
        pivot_time + (resolved.original_time - pivot_time) * scale;
}

void apply_resolved_scale(
    ProjectData* project,
    const ResolvedTimelineKey& resolved,
    double pivot_time,
    double scale) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform:
        apply_timeline_scale(
            &project->transform_timeline_edits, resolved, pivot_time, scale);
        return;
    case TimelineKeyKind::Deform:
        apply_timeline_scale(
            &project->mesh_deform_timeline_edits, resolved, pivot_time, scale);
        return;
    case TimelineKeyKind::DrawOrder:
        apply_timeline_scale(
            &project->draw_order_timeline_edits, resolved, pivot_time, scale);
        return;
    case TimelineKeyKind::Event:
        apply_timeline_scale(
            &project->event_timeline_edits, resolved, pivot_time, scale);
        return;
    case TimelineKeyKind::SlotColor:
        apply_timeline_scale(
            &project->slot_color_timeline_edits, resolved, pivot_time, scale);
        return;
    case TimelineKeyKind::SlotAttachment:
        apply_timeline_scale(
            &project->slot_attachment_timeline_edits, resolved, pivot_time, scale);
        return;
    }
}

void include_resolved_retime_bounds(
    const ProjectData& project,
    const ResolvedTimelineKey& resolved,
    const std::vector<ResolvedTimelineKey>& all_resolved,
    double* minimum_delta,
    double* maximum_delta) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform:
        include_timeline_retime_bounds(
            project.transform_timeline_edits,
            resolved,
            all_resolved,
            family_key_spacing(resolved.kind),
            minimum_delta,
            maximum_delta);
        include_loop_boundary_retime_pins(
            project.transform_timeline_edits, resolved, minimum_delta, maximum_delta);
        return;
    case TimelineKeyKind::Deform:
        include_timeline_retime_bounds(
            project.mesh_deform_timeline_edits,
            resolved,
            all_resolved,
            family_key_spacing(resolved.kind),
            minimum_delta,
            maximum_delta);
        include_loop_boundary_retime_pins(
            project.mesh_deform_timeline_edits, resolved, minimum_delta, maximum_delta);
        return;
    case TimelineKeyKind::DrawOrder:
        include_timeline_retime_bounds(
            project.draw_order_timeline_edits,
            resolved,
            all_resolved,
            family_key_spacing(resolved.kind),
            minimum_delta,
            maximum_delta);
        return;
    case TimelineKeyKind::Event:
        include_timeline_retime_bounds(
            project.event_timeline_edits,
            resolved,
            all_resolved,
            family_key_spacing(resolved.kind),
            minimum_delta,
            maximum_delta);
        return;
    case TimelineKeyKind::SlotColor:
        include_timeline_retime_bounds(
            project.slot_color_timeline_edits,
            resolved,
            all_resolved,
            family_key_spacing(resolved.kind),
            minimum_delta,
            maximum_delta);
        include_loop_boundary_retime_pins(
            project.slot_color_timeline_edits, resolved, minimum_delta, maximum_delta);
        return;
    case TimelineKeyKind::SlotAttachment:
        include_timeline_retime_bounds(
            project.slot_attachment_timeline_edits,
            resolved,
            all_resolved,
            family_key_spacing(resolved.kind),
            minimum_delta,
            maximum_delta);
        return;
    }
}

template <typename Timeline>
void apply_timeline_retime(
    std::vector<Timeline>* timelines,
    const ResolvedTimelineKey& resolved,
    double delta) {
    (*timelines)[resolved.timeline_index].keyframes[resolved.key_index].time =
        resolved.original_time + delta;
}

void apply_resolved_retime(
    ProjectData* project,
    const ResolvedTimelineKey& resolved,
    double delta) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform:
        apply_timeline_retime(&project->transform_timeline_edits, resolved, delta);
        return;
    case TimelineKeyKind::Deform:
        apply_timeline_retime(&project->mesh_deform_timeline_edits, resolved, delta);
        return;
    case TimelineKeyKind::DrawOrder:
        apply_timeline_retime(&project->draw_order_timeline_edits, resolved, delta);
        return;
    case TimelineKeyKind::Event:
        apply_timeline_retime(&project->event_timeline_edits, resolved, delta);
        return;
    case TimelineKeyKind::SlotColor:
        apply_timeline_retime(&project->slot_color_timeline_edits, resolved, delta);
        return;
    case TimelineKeyKind::SlotAttachment:
        apply_timeline_retime(&project->slot_attachment_timeline_edits, resolved, delta);
        return;
    }
}

template <typename Timeline>
void sort_affected_timelines(
    std::vector<Timeline>* timelines,
    TimelineKeyKind kind,
    const std::vector<ResolvedTimelineKey>& resolved) {
    std::set<std::size_t> affected;
    for (const ResolvedTimelineKey& key : resolved) {
        if (key.kind == kind) affected.insert(key.timeline_index);
    }
    for (const std::size_t timeline_index : affected) {
        auto& keyframes = (*timelines)[timeline_index].keyframes;
        std::stable_sort(
            keyframes.begin(), keyframes.end(), [](const auto& left, const auto& right) {
                return left.time < right.time;
            });
    }
}

void sort_retimed_timelines(
    ProjectData* project,
    const std::vector<ResolvedTimelineKey>& resolved) {
    sort_affected_timelines(
        &project->transform_timeline_edits, TimelineKeyKind::Transform, resolved);
    sort_affected_timelines(
        &project->mesh_deform_timeline_edits, TimelineKeyKind::Deform, resolved);
    sort_affected_timelines(
        &project->draw_order_timeline_edits, TimelineKeyKind::DrawOrder, resolved);
    sort_affected_timelines(
        &project->event_timeline_edits, TimelineKeyKind::Event, resolved);
    sort_affected_timelines(
        &project->slot_color_timeline_edits, TimelineKeyKind::SlotColor, resolved);
    sort_affected_timelines(
        &project->slot_attachment_timeline_edits,
        TimelineKeyKind::SlotAttachment,
        resolved);
}

bool valid_parameter_definition(
    const ParameterAuthoringDefinition& definition,
    std::string* error) {
    if (definition.id.empty() || definition.name.empty()) {
        *error = "Parameter ids and names must not be empty.";
        return false;
    }
    if (!std::isfinite(definition.min_value) ||
        !std::isfinite(definition.max_value) ||
        !std::isfinite(definition.default_value) ||
        definition.min_value > definition.max_value ||
        (definition.clamp &&
         (definition.default_value < definition.min_value ||
          definition.default_value > definition.max_value))) {
        *error = "Parameter ranges and defaults must be finite and ordered.";
        return false;
    }
    if (definition.ui_step.has_value() &&
        (!std::isfinite(*definition.ui_step) || *definition.ui_step <= 0.0)) {
        *error = "Parameter ui_step must be finite and positive.";
        return false;
    }
    return true;
}

bool valid_group_definition(
    const ParameterModel& model,
    const ParameterGroupAuthoringDefinition& definition,
    std::string* error) {
    if (definition.id.empty() || definition.name.empty()) {
        *error = "Parameter group ids and names must not be empty.";
        return false;
    }
    std::set<std::string> references;
    for (const std::string& parameter_id : definition.parameter_ids) {
        if (model.find_parameter(parameter_id) == nullptr) {
            *error = "Parameter group references missing parameter: " + parameter_id;
            return false;
        }
        if (!references.insert(parameter_id).second) {
            *error = "Parameter group references must be unique.";
            return false;
        }
    }
    return true;
}

const Value* identity_member(const Value& value, std::string_view key) {
    return value.is_object() ? runtime::json::find_member(value, key) : nullptr;
}

bool same_lossless_array_identity(const Value& left, const Value& right) {
    static constexpr std::array<std::string_view, 5U> kStringKeys{
        "id", "parameter", "axis", "phoneme", "name"};
    for (std::string_view key : kStringKeys) {
        const Value* left_value = identity_member(left, key);
        const Value* right_value = identity_member(right, key);
        if (left_value != nullptr && left_value->is_string() &&
            right_value != nullptr && right_value->is_string() &&
            left_value->as_string() != right_value->as_string()) {
            return false;
        }
    }
    const Value* left_id = identity_member(left, "id");
    const Value* right_id = identity_member(right, "id");
    if (left_id != nullptr && right_id != nullptr) return true;
    const Value* left_x = identity_member(left, "x");
    const Value* left_y = identity_member(left, "y");
    const Value* right_x = identity_member(right, "x");
    const Value* right_y = identity_member(right, "y");
    if (left_x != nullptr && left_y != nullptr && right_x != nullptr && right_y != nullptr &&
        left_x->is_number() && left_y->is_number() &&
        right_x->is_number() && right_y->is_number()) {
        return left_x->as_number() == right_x->as_number() &&
            left_y->as_number() == right_y->as_number();
    }
    const Value* left_parameter = identity_member(left, "parameter");
    const Value* right_parameter = identity_member(right, "parameter");
    const Value* left_axis = identity_member(left, "axis");
    const Value* right_axis = identity_member(right, "axis");
    if (left_parameter != nullptr && right_parameter != nullptr &&
        left_parameter->is_string() && right_parameter->is_string()) {
        if (left_axis != nullptr && right_axis != nullptr &&
            left_axis->is_string() && right_axis->is_string()) {
            return left_parameter->as_string() == right_parameter->as_string() &&
                left_axis->as_string() == right_axis->as_string();
        }
        return left_parameter->as_string() == right_parameter->as_string();
    }
    const Value* left_value = identity_member(left, "value");
    const Value* right_value = identity_member(right, "value");
    return left_value != nullptr && right_value != nullptr &&
        left_value->is_number() && right_value->is_number() &&
        left_value->as_number() == right_value->as_number();
}

Value merge_lossless_json(const Value& existing, const Value& incoming) {
    if (existing.is_object() && incoming.is_object()) {
        Value::Object merged = existing.as_object();
        for (const auto& [key, value] : incoming.as_object()) {
            const auto found = merged.find(key);
            merged[key] = found == merged.end()
                ? value
                : merge_lossless_json(found->second, value);
        }
        return Value(std::move(merged), {});
    }
    if (existing.is_array() && incoming.is_array()) {
        Value::Array merged;
        merged.reserve(incoming.as_array().size());
        for (std::size_t index = 0U; index < incoming.as_array().size(); ++index) {
            const Value& value = incoming.as_array()[index];
            const auto matching = std::find_if(
                existing.as_array().begin(), existing.as_array().end(),
                [&](const Value& candidate) {
                    return same_lossless_array_identity(candidate, value);
                });
            if (matching != existing.as_array().end()) {
                merged.push_back(merge_lossless_json(*matching, value));
            } else if (index < existing.as_array().size()) {
                merged.push_back(merge_lossless_json(existing.as_array()[index], value));
            } else {
                merged.push_back(value);
            }
        }
        return Value(std::move(merged), {});
    }
    return incoming;
}

template <typename Definition, typename Parse, typename Build>
AuthoringResult upsert_typed_definition(
    ProjectData* project,
    std::vector<Definition>* values,
    Value definition,
    std::string_view family,
    bool replace_existing,
    Parse&& parse,
    Build&& build) {
    if (project == nullptr) {
        return {false, "Parameter authoring requires an open project."};
    }
    Definition parsed;
    std::string error;
    if (!parse(definition, &parsed, &error) || parsed.id.empty()) {
        return {false, std::string(family) + " definition is invalid: " + error};
    }
    const auto existing = std::find_if(
        values->begin(), values->end(),
        [&](const Definition& value) { return value.id == parsed.id; });
    if (existing != values->end()) {
        if (!replace_existing) {
            return {false, std::string(family) + " already exists: " + parsed.id};
        }
        parsed.preserved_source = merge_lossless_json(build(*existing), definition);
        *existing = std::move(parsed);
        return {true, {}};
    }
    values->push_back(std::move(parsed));
    return {true, {}};
}

template <typename Definition>
AuthoringResult erase_typed_definition(
    ProjectData* project,
    std::vector<Definition>* values,
    std::string_view id,
    std::string_view family) {
    if (project == nullptr) {
        return {false, "Parameter authoring requires an open project."};
    }
    const auto found = std::find_if(values->begin(), values->end(), [&](const Definition& value) {
        return value.id == id;
    });
    if (found == values->end()) {
        return {false, std::string(family) + " not found: " + std::string(id)};
    }
    values->erase(found);
    return {true, {}};
}

struct KeyformCoordinateEpsilon {
    double value{1e-9};
    double x{1e-9};
    double y{1e-9};
};

KeyformCoordinateEpsilon keyform_coordinate_epsilon(
    const ParameterModel& model,
    const Value& definition) {
    KeyformCoordinateEpsilon epsilon;
    const auto parameter_epsilon = [&](std::string_view id) {
        const ParameterAuthoringDefinition* parameter = model.find_parameter(id);
        const double range = parameter == nullptr
            ? 1.0
            : parameter->max_value - parameter->min_value;
        return 1e-9 * std::max(1.0, range);
    };
    const Value* bindings = runtime::json::find_member(definition, "parameter_bindings");
    if (bindings != nullptr && bindings->is_array()) {
        for (const Value& binding : bindings->as_array()) {
            const Value* parameter = runtime::json::find_member(binding, "parameter");
            const Value* axis = runtime::json::find_member(binding, "axis");
            if (parameter != nullptr && parameter->is_string() &&
                axis != nullptr && axis->is_string()) {
                const double value = parameter_epsilon(parameter->as_string());
                if (axis->as_string() == "x") epsilon.x = value;
                else if (axis->as_string() == "y") epsilon.y = value;
                else if (axis->as_string() == "angle") epsilon.value = value;
            }
        }
    }
    const Value* parameter = runtime::json::find_member(definition, "parameter");
    if (parameter != nullptr && parameter->is_string()) {
        epsilon.value = parameter_epsilon(parameter->as_string());
    }
    return epsilon;
}

bool same_keyform_coordinate(
    const Value& left,
    const Value& right,
    const KeyformCoordinateEpsilon& epsilon) {
    const Value* left_x = runtime::json::find_member(left, "x");
    const Value* left_y = runtime::json::find_member(left, "y");
    const Value* right_x = runtime::json::find_member(right, "x");
    const Value* right_y = runtime::json::find_member(right, "y");
    if (left_x != nullptr && left_x->is_number() && left_y != nullptr && left_y->is_number() &&
        right_x != nullptr && right_x->is_number() && right_y != nullptr && right_y->is_number()) {
        return std::abs(left_x->as_number() - right_x->as_number()) <= epsilon.x &&
            std::abs(left_y->as_number() - right_y->as_number()) <= epsilon.y;
    }
    const Value* left_value = runtime::json::find_member(left, "value");
    const Value* right_value = runtime::json::find_member(right, "value");
    return left_value != nullptr && left_value->is_number() &&
        right_value != nullptr && right_value->is_number() &&
        std::abs(left_value->as_number() - right_value->as_number()) <= epsilon.value;
}

} // namespace

ParameterModel& ensure_parameter_model(ProjectData* project) {
    if (!project->parameter_model.has_value()) {
        project->parameter_model.emplace();
    }
    return *project->parameter_model;
}

AuthoringResult create_parameter(
    ProjectData* project,
    ParameterAuthoringDefinition definition) {
    if (project == nullptr) {
        return {false, "Parameter authoring requires an open project."};
    }
    std::string error;
    if (!valid_parameter_definition(definition, &error)) {
        return {false, std::move(error)};
    }
    ParameterModel& model = ensure_parameter_model(project);
    if (model.find_parameter(definition.id) != nullptr) {
        return {false, "Parameter already exists: " + definition.id};
    }
    model.parameters.push_back(std::move(definition));
    return {true, {}};
}

AuthoringResult update_parameter(
    ProjectData* project,
    std::string_view parameter_id,
    ParameterAuthoringDefinition definition) {
    if (project == nullptr || !project->parameter_model.has_value()) {
        return {false, "Parameter not found: " + std::string(parameter_id)};
    }
    if (definition.id != parameter_id) {
        return {false, "Parameter ids are stable and cannot be changed."};
    }
    std::string error;
    if (!valid_parameter_definition(definition, &error)) {
        return {false, std::move(error)};
    }
    ParameterAuthoringDefinition* existing =
        project->parameter_model->find_parameter(parameter_id);
    if (existing == nullptr) {
        return {false, "Parameter not found: " + std::string(parameter_id)};
    }
    *existing = std::move(definition);
    return {true, {}};
}

AuthoringResult delete_parameter(ProjectData* project, std::string_view parameter_id) {
    if (project == nullptr || !project->parameter_model.has_value()) {
        return {false, "Parameter not found: " + std::string(parameter_id)};
    }
    ParameterModel& model = *project->parameter_model;
    const auto found = std::find_if(
        model.parameters.begin(), model.parameters.end(),
        [&](const ParameterAuthoringDefinition& parameter) {
            return parameter.id == parameter_id;
        });
    if (found == model.parameters.end()) {
        return {false, "Parameter not found: " + std::string(parameter_id)};
    }

    std::vector<std::string> dependencies;
    for (const ParameterGroupAuthoringDefinition& group : model.groups) {
        if (std::find(group.parameter_ids.begin(), group.parameter_ids.end(), parameter_id) !=
            group.parameter_ids.end()) {
            dependencies.push_back("group " + group.id);
        }
    }
    for (const ParameterShapeAuthoringDefinition& shape : model.blend_shapes) {
        if (shape.parameter == parameter_id) dependencies.push_back("blend shape " + shape.id);
    }
    for (const ParameterDeformerAuthoringDefinition& deformer : model.deformers) {
        for (const runtime::ParameterBindingDefinition& binding :
             deformer.parameter_bindings) {
            if (binding.parameter == parameter_id) {
                dependencies.push_back("deformer " + deformer.id);
            }
        }
    }
    for (const ArtPathAuthoringDefinition& art_path : model.art_paths) {
        if (art_path.parameter_keyforms.has_value() &&
            art_path.parameter_keyforms->parameter == parameter_id) {
            dependencies.push_back("art path " + art_path.id);
        }
    }
    for (const ExpressionAuthoringDefinition& expression : model.expressions) {
        for (const runtime::ExpressionTargetDefinition& target : expression.targets) {
            if (target.parameter == parameter_id) {
                dependencies.push_back("expression " + expression.id);
            }
        }
    }
    for (const LipSyncMappingAuthoringDefinition& mapping : model.lip_sync.mappings) {
        if (mapping.parameter == parameter_id) {
            dependencies.push_back("lip-sync mapping " + mapping.parameter);
        }
    }
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    if (!dependencies.empty()) {
        return {false, "Parameter is still referenced: " + std::string(parameter_id),
                std::move(dependencies)};
    }

    model.parameters.erase(found);
    return {true, {}};
}

AuthoringResult create_parameter_group(
    ProjectData* project,
    ParameterGroupAuthoringDefinition definition) {
    if (project == nullptr) {
        return {false, "Parameter authoring requires an open project."};
    }
    ParameterModel& model = ensure_parameter_model(project);
    std::string error;
    if (!valid_group_definition(model, definition, &error)) {
        return {false, std::move(error)};
    }
    if (model.find_group(definition.id) != nullptr) {
        return {false, "Parameter group already exists: " + definition.id};
    }
    model.groups.push_back(std::move(definition));
    return {true, {}};
}

AuthoringResult update_parameter_group(
    ProjectData* project,
    std::string_view group_id,
    ParameterGroupAuthoringDefinition definition) {
    if (project == nullptr || !project->parameter_model.has_value()) {
        return {false, "Parameter group not found: " + std::string(group_id)};
    }
    if (definition.id != group_id) {
        return {false, "Parameter group ids are stable and cannot be changed."};
    }
    std::string error;
    if (!valid_group_definition(*project->parameter_model, definition, &error)) {
        return {false, std::move(error)};
    }
    ParameterGroupAuthoringDefinition* existing =
        project->parameter_model->find_group(group_id);
    if (existing == nullptr) {
        return {false, "Parameter group not found: " + std::string(group_id)};
    }
    *existing = std::move(definition);
    return {true, {}};
}

AuthoringResult delete_parameter_group(ProjectData* project, std::string_view group_id) {
    if (project == nullptr || !project->parameter_model.has_value()) {
        return {false, "Parameter group not found: " + std::string(group_id)};
    }
    auto& groups = project->parameter_model->groups;
    const auto found = std::find_if(groups.begin(), groups.end(), [&](const auto& group) {
        return group.id == group_id;
    });
    if (found == groups.end()) {
        return {false, "Parameter group not found: " + std::string(group_id)};
    }
    groups.erase(found);
    return {true, {}};
}

AuthoringResult upsert_parameter_shape(
    ProjectData* project,
    Value definition,
    bool replace_existing) {
    if (project == nullptr) {
        return {false, "Parameter authoring requires an open project."};
    }
    ParameterModel& model = ensure_parameter_model(project);
    return upsert_typed_definition(
        project,
        &model.blend_shapes,
        std::move(definition),
        "Parameter shape",
        replace_existing,
        parse_parameter_shape_authoring_value,
        build_parameter_shape_authoring_value);
}

AuthoringResult delete_parameter_shape(ProjectData* project, std::string_view shape_id) {
    if (project == nullptr || !project->parameter_model.has_value()) {
        return {false, "Parameter shape not found: " + std::string(shape_id)};
    }
    return erase_typed_definition(
        project, &project->parameter_model->blend_shapes, shape_id, "Parameter shape");
}

AuthoringResult upsert_parameter_deformer(
    ProjectData* project,
    Value definition,
    bool replace_existing) {
    if (project == nullptr) {
        return {false, "Parameter authoring requires an open project."};
    }
    ParameterModel& model = ensure_parameter_model(project);
    return upsert_typed_definition(
        project,
        &model.deformers,
        std::move(definition),
        "Parameter deformer",
        replace_existing,
        parse_parameter_deformer_authoring_value,
        build_parameter_deformer_authoring_value);
}

AuthoringResult delete_parameter_deformer(ProjectData* project, std::string_view deformer_id) {
    if (project == nullptr || !project->parameter_model.has_value()) {
        return {false, "Parameter deformer not found: " + std::string(deformer_id)};
    }
    ParameterModel& model = *project->parameter_model;
    std::vector<std::string> dependencies;
    for (const ParameterDeformerAuthoringDefinition& child : model.deformers) {
        if (child.parent == std::optional<std::string>(deformer_id)) {
            dependencies.push_back("child deformer " + child.id);
        }
    }
    for (const ArtPathAuthoringDefinition& art_path : model.art_paths) {
        if (art_path.parent_deformer == std::optional<std::string>(deformer_id)) {
            dependencies.push_back("art path " + art_path.id);
        }
    }
    if (!dependencies.empty()) {
        return {false, "Parameter deformer still has dependents: " + std::string(deformer_id),
                std::move(dependencies)};
    }
    return erase_typed_definition(project, &model.deformers, deformer_id, "Parameter deformer");
}

AuthoringResult upsert_expression(
    ProjectData* project,
    Value definition,
    bool replace_existing) {
    if (project == nullptr) {
        return {false, "Parameter authoring requires an open project."};
    }
    ParameterModel& model = ensure_parameter_model(project);
    return upsert_typed_definition(
        project,
        &model.expressions,
        std::move(definition),
        "Expression",
        replace_existing,
        parse_expression_authoring_value,
        build_expression_authoring_value);
}

AuthoringResult delete_expression(ProjectData* project, std::string_view expression_id) {
    if (project == nullptr || !project->parameter_model.has_value()) {
        return {false, "Expression not found: " + std::string(expression_id)};
    }
    return erase_typed_definition(
        project, &project->parameter_model->expressions, expression_id, "Expression");
}

AuthoringResult upsert_lip_sync_mapping(ProjectData* project, Value mapping) {
    if (project == nullptr) {
        return {false, "Parameter authoring requires an open project."};
    }
    const Value* parameter = runtime::json::find_member(mapping, "parameter");
    if (!mapping.is_object() || parameter == nullptr || !parameter->is_string() ||
        parameter->as_string().empty()) {
        return {false, "Lip-sync mappings require a target parameter id."};
    }
    ParameterModel& model = ensure_parameter_model(project);
    if (model.find_parameter(parameter->as_string()) == nullptr) {
        return {false, "Lip-sync target parameter not found: " + parameter->as_string()};
    }
    LipSyncAuthoringDefinition parsed_section;
    Value::Object section_object;
    section_object["mappings"] = Value(Value::Array{mapping}, {});
    std::string error;
    if (!parse_lip_sync_authoring_value(
            Value(std::move(section_object), {}), &parsed_section, &error) ||
        parsed_section.mappings.size() != 1U) {
        return {false, "Lip-sync mapping is invalid: " + error};
    }
    LipSyncMappingAuthoringDefinition parsed = std::move(parsed_section.mappings.front());
    for (LipSyncMappingAuthoringDefinition& existing : model.lip_sync.mappings) {
        if (existing.parameter == parsed.parameter) {
            LipSyncAuthoringDefinition existing_section;
            existing_section.mappings.push_back(existing);
            const Value existing_value =
                build_lip_sync_authoring_value(existing_section);
            const Value* existing_mappings = runtime::json::find_member(
                existing_value, "mappings");
            if (existing_mappings != nullptr && existing_mappings->is_array() &&
                !existing_mappings->as_array().empty()) {
                parsed.preserved_source = merge_lossless_json(
                    existing_mappings->as_array().front(), mapping);
            }
            existing = std::move(parsed);
            return {true, {}};
        }
    }
    model.lip_sync.mappings.push_back(std::move(parsed));
    return {true, {}};
}

AuthoringResult delete_lip_sync_mapping(ProjectData* project, std::string_view parameter_id) {
    if (project == nullptr || !project->parameter_model.has_value()) {
        return {false, "Lip-sync mapping not found: " + std::string(parameter_id)};
    }
    auto& values = project->parameter_model->lip_sync.mappings;
    const auto found = std::find_if(values.begin(), values.end(), [&](const auto& mapping) {
        return mapping.parameter == parameter_id;
    });
    if (found == values.end()) {
        return {false, "Lip-sync mapping not found: " + std::string(parameter_id)};
    }
    values.erase(found);
    return {true, {}};
}

AuthoringResult capture_deformer_keyform(
    ProjectData* project,
    std::string_view deformer_id,
    Value keyform,
    bool replace_existing) {
    if (project == nullptr || !project->parameter_model.has_value()) {
        return {false, "Parameter deformer not found: " + std::string(deformer_id)};
    }
    if (!keyform.is_object()) {
        return {false, "Captured keyforms must be objects."};
    }
    ParameterModel& model = *project->parameter_model;
    ParameterDeformerAuthoringDefinition* deformer = model.find_deformer(deformer_id);
    ParameterShapeAuthoringDefinition* shape = model.find_shape(deformer_id);
    if (deformer == nullptr && shape == nullptr) {
        return {false, "Parameter deformer not found: " + std::string(deformer_id)};
    }
    Value definition = deformer != nullptr
        ? build_parameter_deformer_authoring_value(*deformer)
        : build_parameter_shape_authoring_value(*shape);
    const auto adopt_definition = [&]() -> AuthoringResult {
        std::string error;
        if (deformer != nullptr) {
            ParameterDeformerAuthoringDefinition parsed;
            if (!parse_parameter_deformer_authoring_value(definition, &parsed, &error)) {
                return {false, "Captured deformer keyform is invalid: " + error};
            }
            *deformer = std::move(parsed);
        } else {
            ParameterShapeAuthoringDefinition parsed;
            if (!parse_parameter_shape_authoring_value(definition, &parsed, &error)) {
                return {false, "Captured shape keyform is invalid: " + error};
            }
            *shape = std::move(parsed);
        }
        return {true, {}};
    };
    Value* keyforms = runtime::json::find_member(definition, "keyforms");
    if (keyforms == nullptr) {
        definition.as_object()["keyforms"] = Value(Value::Array{}, {});
        keyforms = runtime::json::find_member(definition, "keyforms");
    }
    if (!keyforms->is_array()) {
        return {false, "Parameter deformer keyforms must be an array."};
    }
    const KeyformCoordinateEpsilon epsilon =
        keyform_coordinate_epsilon(model, definition);
    auto& values = keyforms->as_array();
    const auto existing = std::find_if(values.begin(), values.end(), [&](const Value& value) {
        return same_keyform_coordinate(value, keyform, epsilon);
    });
    if (existing != values.end()) {
        if (!replace_existing) {
            return {false, "A keyform already exists at the preview coordinates."};
        }
        *existing = merge_lossless_json(*existing, keyform);
        return adopt_definition();
    }
    const bool has_value = runtime::json::find_member(keyform, "value") != nullptr;
    const bool has_grid = runtime::json::find_member(keyform, "x") != nullptr &&
        runtime::json::find_member(keyform, "y") != nullptr;
    if (!has_value && !has_grid) {
        return {false, "Captured keyforms require value or x/y coordinates."};
    }
    values.push_back(std::move(keyform));
    std::stable_sort(values.begin(), values.end(), [](const Value& left, const Value& right) {
        const Value* left_value = runtime::json::find_member(left, "value");
        const Value* right_value = runtime::json::find_member(right, "value");
        if (left_value != nullptr && left_value->is_number() &&
            right_value != nullptr && right_value->is_number()) {
            return left_value->as_number() < right_value->as_number();
        }
        const Value* left_x = runtime::json::find_member(left, "x");
        const Value* right_x = runtime::json::find_member(right, "x");
        const Value* left_y = runtime::json::find_member(left, "y");
        const Value* right_y = runtime::json::find_member(right, "y");
        if (left_x == nullptr || right_x == nullptr || left_y == nullptr || right_y == nullptr ||
            !left_x->is_number() || !right_x->is_number() ||
            !left_y->is_number() || !right_y->is_number()) {
            return false;
        }
        return left_x->as_number() == right_x->as_number()
            ? left_y->as_number() < right_y->as_number()
            : left_x->as_number() < right_x->as_number();
    });
    return adopt_definition();
}

AuthoringResult capture_current_deformer_keyform(
    ProjectData* project,
    const runtime::SkeletonData& runtime_data,
    const runtime::Skeleton& preview_skeleton,
    std::string_view deformer_id,
    bool replace_existing) {
    const auto number = [](double value) { return Value(value, {}); };
    const auto object = [](Value::Object value) { return Value(std::move(value), {}); };
    const auto array = [](Value::Array value) { return Value(std::move(value), {}); };

    if (project == nullptr || !project->parameter_model.has_value() ||
        preview_skeleton.data().get() != &runtime_data) {
        return {false, "Current keyform capture requires a matching project preview."};
    }

    if (const auto shape_index = runtime_data.find_parameter_shape_index(deformer_id)) {
        const runtime::ParameterShapeDefinition& shape =
            runtime_data.parameter_shapes()[*shape_index];
        if (!shape.parameter_index.has_value() ||
            *shape.parameter_index >= preview_skeleton.parameter_values().size()) {
            return {false, "Parameter shape binding is unresolved: " + std::string(deformer_id)};
        }
        std::vector<double> offsets;
        if (const std::vector<double>* current =
                preview_skeleton.current_mesh_vertex_offsets(shape.target_slot_index)) {
            offsets = *current;
        } else if (!shape.keyforms.empty()) {
            offsets.assign(shape.keyforms.front().vertices.size(), 0.0);
        }
        Value::Array vertices;
        vertices.reserve(offsets.size());
        for (const double offset : offsets) {
            vertices.push_back(number(offset));
        }
        Value::Object keyform;
        keyform.emplace(
            "value",
            number(preview_skeleton.parameter_values()[*shape.parameter_index]));
        keyform.emplace("vertices", array(std::move(vertices)));
        return capture_deformer_keyform(
            project, deformer_id, object(std::move(keyform)), replace_existing);
    }

    const auto deformer_index = runtime_data.find_parameter_deformer_index(deformer_id);
    if (!deformer_index.has_value()) {
        return {false, "Parameter deformer not found: " + std::string(deformer_id)};
    }
    const runtime::ParameterDeformerDefinition& deformer =
        runtime_data.parameter_deformers()[*deformer_index];
    const auto bound_value = [&](runtime::ParameterDeformerAxis axis)
        -> std::optional<double> {
        const auto binding = std::find_if(
            deformer.parameter_bindings.begin(),
            deformer.parameter_bindings.end(),
            [&](const runtime::ParameterBindingDefinition& candidate) {
                return candidate.axis == axis;
            });
        if (binding == deformer.parameter_bindings.end() ||
            !binding->parameter_index.has_value() ||
            *binding->parameter_index >= preview_skeleton.parameter_values().size()) {
            return std::nullopt;
        }
        return preview_skeleton.parameter_values()[*binding->parameter_index];
    };

    if (deformer.kind == runtime::ParameterDeformerKind::Rotation) {
        const std::optional<double> value =
            bound_value(runtime::ParameterDeformerAxis::Angle);
        if (!value.has_value() || deformer.rotation_keyforms.empty()) {
            return {false, "Rotation deformer binding or keyforms are unavailable."};
        }
        const auto& keyforms = deformer.rotation_keyforms;
        double angle = keyforms.front().angle;
        if (*value >= keyforms.back().value) {
            angle = keyforms.back().angle;
        } else if (*value > keyforms.front().value) {
            const auto upper = std::upper_bound(
                keyforms.begin(), keyforms.end(), *value,
                [](double coordinate, const runtime::RotationDeformerKeyform& keyform) {
                    return coordinate < keyform.value;
                });
            const auto lower = std::prev(upper);
            const double span = upper->value - lower->value;
            const double mix = span > 0.0 ? (*value - lower->value) / span : 0.0;
            angle = lower->angle + ((upper->angle - lower->angle) * mix);
        }
        Value::Object keyform;
        keyform.emplace("value", number(*value));
        keyform.emplace("angle", number(angle));
        return capture_deformer_keyform(
            project, deformer_id, object(std::move(keyform)), replace_existing);
    }

    const std::optional<double> x = bound_value(runtime::ParameterDeformerAxis::X);
    const std::optional<double> y = bound_value(runtime::ParameterDeformerAxis::Y);
    if (!x.has_value() || !y.has_value() || deformer.warp_keyforms.empty()) {
        return {false, "Warp deformer bindings or keyforms are unavailable."};
    }
    const ParameterDeformerAuthoringDefinition* authored_deformer =
        project->parameter_model->find_deformer(deformer_id);
    const KeyformCoordinateEpsilon coordinate_epsilon = authored_deformer == nullptr
        ? KeyformCoordinateEpsilon{}
        : keyform_coordinate_epsilon(
              *project->parameter_model,
              build_parameter_deformer_authoring_value(*authored_deformer));

    std::vector<double> x_coordinates;
    std::vector<double> y_coordinates;
    for (const runtime::WarpDeformerKeyform& keyform : deformer.warp_keyforms) {
        x_coordinates.push_back(keyform.x);
        y_coordinates.push_back(keyform.y);
    }
    const auto normalize_coordinates = [](
        std::vector<double>* coordinates,
        double epsilon) {
        std::sort(coordinates->begin(), coordinates->end());
        const auto unique_end = std::unique(
            coordinates->begin(), coordinates->end(),
            [epsilon](double left, double right) {
                return std::abs(left - right) <= epsilon;
            });
        coordinates->erase(unique_end, coordinates->end());
    };
    normalize_coordinates(&x_coordinates, coordinate_epsilon.x);
    normalize_coordinates(&y_coordinates, coordinate_epsilon.y);
    const auto bracket = [](
        const std::vector<double>& coordinates,
        double value,
        double epsilon) {
        const double clamped = std::clamp(value, coordinates.front(), coordinates.back());
        const auto upper = std::lower_bound(coordinates.begin(), coordinates.end(), clamped);
        if (upper == coordinates.begin()) {
            return std::pair{coordinates.front(), coordinates.front()};
        }
        if (upper == coordinates.end()) {
            return std::pair{coordinates.back(), coordinates.back()};
        }
        if (std::abs(*upper - clamped) <= epsilon) {
            return std::pair{*upper, *upper};
        }
        return std::pair{*std::prev(upper), *upper};
    };
    const auto [x0, x1] = bracket(x_coordinates, *x, coordinate_epsilon.x);
    const auto [y0, y1] = bracket(y_coordinates, *y, coordinate_epsilon.y);
    const auto find_keyform = [&](double key_x, double key_y)
        -> const runtime::WarpDeformerKeyform* {
        const auto found = std::find_if(
            deformer.warp_keyforms.begin(),
            deformer.warp_keyforms.end(),
            [&](const runtime::WarpDeformerKeyform& keyform) {
                return std::abs(keyform.x - key_x) <= coordinate_epsilon.x &&
                    std::abs(keyform.y - key_y) <= coordinate_epsilon.y;
            });
        return found == deformer.warp_keyforms.end() ? nullptr : &*found;
    };
    const auto* q00 = find_keyform(x0, y0);
    const auto* q10 = find_keyform(x1, y0);
    const auto* q01 = find_keyform(x0, y1);
    const auto* q11 = find_keyform(x1, y1);
    if (q00 == nullptr || q10 == nullptr || q01 == nullptr || q11 == nullptr) {
        return {false, "Warp deformer is missing a Cartesian keyform corner."};
    }
    const std::size_t point_count = q00->control_points.size();
    if (q10->control_points.size() != point_count ||
        q01->control_points.size() != point_count ||
        q11->control_points.size() != point_count) {
        return {false, "Warp deformer keyform lattice sizes do not match."};
    }
    const double tx = std::abs(x1 - x0) <= coordinate_epsilon.x
        ? 0.0
        : (std::clamp(*x, x0, x1) - x0) / (x1 - x0);
    const double ty = std::abs(y1 - y0) <= coordinate_epsilon.y
        ? 0.0
        : (std::clamp(*y, y0, y1) - y0) / (y1 - y0);
    Value::Array control_points;
    control_points.reserve(point_count * 2U);
    for (std::size_t index = 0U; index < point_count; ++index) {
        const auto interpolate = [&](double a00, double a10, double a01, double a11) {
            const double bottom = a00 + ((a10 - a00) * tx);
            const double top = a01 + ((a11 - a01) * tx);
            return bottom + ((top - bottom) * ty);
        };
        control_points.push_back(number(interpolate(
            q00->control_points[index].x,
            q10->control_points[index].x,
            q01->control_points[index].x,
            q11->control_points[index].x)));
        control_points.push_back(number(interpolate(
            q00->control_points[index].y,
            q10->control_points[index].y,
            q01->control_points[index].y,
            q11->control_points[index].y)));
    }
    Value::Object keyform;
    keyform.emplace("x", number(*x));
    keyform.emplace("y", number(*y));
    keyform.emplace("control_points", array(std::move(control_points)));
    return capture_deformer_keyform(
        project, deformer_id, object(std::move(keyform)), replace_existing);
}

AuthoringResult create_animation(
    ProjectData* project,
    const Document& base_skeleton_document,
    std::string_view animation_name) {
    if (project == nullptr) {
        return missing_project_result();
    }
    if (!valid_animation_name(animation_name)) {
        return invalid_name_result();
    }
    Document effective;
    const Value::Object* animations = effective_animations(*project, base_skeleton_document, &effective);
    if (animations != nullptr && animations->find(animation_name) != animations->end()) {
        return {false, "Animation already exists: " + std::string(animation_name)};
    }

    AnimationEdit edit;
    edit.kind = AnimationEditKind::Create;
    edit.name = std::string(animation_name);
    edit.animation = Value(Value::Object{}, {});
    project->animation_edits.push_back(std::move(edit));
    project->editor_metadata.active_animation = std::string(animation_name);
    return {true, {}};
}

AuthoringResult duplicate_animation(
    ProjectData* project,
    const Document& base_skeleton_document,
    std::string_view source_animation,
    std::string_view animation_name) {
    if (project == nullptr) {
        return missing_project_result();
    }
    if (!valid_animation_name(source_animation) || !valid_animation_name(animation_name)) {
        return invalid_name_result();
    }
    Document effective;
    const Value::Object* animations = effective_animations(*project, base_skeleton_document, &effective);
    if (animations == nullptr) {
        return {false, "The effective runtime has no animations."};
    }
    const auto source = animations->find(source_animation);
    if (source == animations->end()) {
        return {false, "Animation not found: " + std::string(source_animation)};
    }
    if (animations->find(animation_name) != animations->end()) {
        return {false, "Animation already exists: " + std::string(animation_name)};
    }

    AnimationEdit edit;
    edit.kind = AnimationEditKind::Create;
    edit.name = std::string(animation_name);
    edit.animation = source->second;
    project->animation_edits.push_back(std::move(edit));
    project->editor_metadata.active_animation = std::string(animation_name);
    return {true, {}};
}

AuthoringResult rename_animation(
    ProjectData* project,
    const Document& base_skeleton_document,
    std::string_view source_animation,
    std::string_view animation_name) {
    if (project == nullptr) {
        return missing_project_result();
    }
    if (!valid_animation_name(source_animation) || !valid_animation_name(animation_name)) {
        return invalid_name_result();
    }
    if (source_animation == animation_name) {
        return {false, {}};
    }
    Document effective;
    const Value::Object* animations = effective_animations(*project, base_skeleton_document, &effective);
    if (animations == nullptr || animations->find(source_animation) == animations->end()) {
        return {false, "Animation not found: " + std::string(source_animation)};
    }
    if (animations->find(animation_name) != animations->end()) {
        return {false, "Animation already exists: " + std::string(animation_name)};
    }

    AnimationEdit edit;
    edit.kind = AnimationEditKind::Rename;
    edit.name = std::string(source_animation);
    edit.new_name = std::string(animation_name);
    project->animation_edits.push_back(std::move(edit));
    rename_all_timeline_edits(project, source_animation, animation_name);
    if (project->editor_metadata.active_animation == source_animation) {
        project->editor_metadata.active_animation = std::string(animation_name);
    }
    return {true, {}};
}

AuthoringResult delete_animation(
    ProjectData* project,
    const Document& base_skeleton_document,
    std::string_view animation_name) {
    if (project == nullptr) {
        return missing_project_result();
    }
    if (!valid_animation_name(animation_name)) {
        return invalid_name_result();
    }
    Document effective;
    const Value::Object* animations = effective_animations(*project, base_skeleton_document, &effective);
    if (animations == nullptr || animations->find(animation_name) == animations->end()) {
        return {false, "Animation not found: " + std::string(animation_name)};
    }
    if (animations->size() <= 1U) {
        return {false, "The last animation cannot be deleted."};
    }

    AnimationEdit edit;
    edit.kind = AnimationEditKind::Delete;
    edit.name = std::string(animation_name);
    project->animation_edits.push_back(std::move(edit));
    erase_all_timeline_edits(project, animation_name);
    if (project->editor_metadata.active_animation == animation_name) {
        const auto replacement = std::find_if(
            animations->begin(),
            animations->end(),
            [&](const auto& entry) { return entry.first != animation_name; });
        project->editor_metadata.active_animation =
            replacement != animations->end() ? replacement->first : std::string{};
    }
    return {true, {}};
}

AuthoringResult set_animation_duration(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    double duration) {
    if (project == nullptr) {
        return missing_project_result();
    }
    if (!valid_animation_name(animation_name)) {
        return invalid_name_result();
    }
    if (!std::isfinite(duration) || duration < 0.0 ||
        duration > static_cast<double>(
            std::numeric_limits<runtime::AnimationScalar>::max())) {
        return {
            false,
            "Animation duration must be finite, non-negative, and within the runtime float32 range."};
    }

    const runtime::AnimationData* animation =
        effective_skeleton.find_animation(animation_name);
    if (animation == nullptr) {
        return {false, "Animation not found: " + std::string(animation_name)};
    }

    // MAR-172: a managed loop boundary key sits at the explicit duration by
    // the boundary contract, so it must not act as a floor on the duration it
    // follows. Both the effective side and the project overlay exclude it, and
    // both exclusions are bit-exact no-ops when no lane is opted in.
    double inferred_duration = inferred_duration_excluding_loop_boundaries(
        *project, effective_skeleton, *animation);
    include_animation_timeline_maximum_excluding_loop_boundaries(
        project->transform_timeline_edits, animation_name, &inferred_duration);
    include_animation_timeline_maximum_excluding_loop_boundaries(
        project->mesh_deform_timeline_edits, animation_name, &inferred_duration);
    include_animation_timeline_maximum(
        project->draw_order_timeline_edits, animation_name, &inferred_duration);
    include_animation_timeline_maximum(
        project->event_timeline_edits, animation_name, &inferred_duration);
    include_animation_timeline_maximum_excluding_loop_boundaries(
        project->slot_color_timeline_edits, animation_name, &inferred_duration);
    include_animation_timeline_maximum(
        project->slot_attachment_timeline_edits, animation_name, &inferred_duration);
    if (!std::isfinite(inferred_duration) || inferred_duration < 0.0 ||
        inferred_duration > static_cast<double>(
            std::numeric_limits<runtime::AnimationScalar>::max())) {
        return {
            false,
            "Animation timeline keys must have finite non-negative float32 times."};
    }

    const double applied_duration = static_cast<double>(
        static_cast<runtime::AnimationScalar>(duration));
    const double normalized_inferred_duration = std::max(
        inferred_duration_excluding_loop_boundaries(
            *project, effective_skeleton, *animation),
        static_cast<double>(
            static_cast<runtime::AnimationScalar>(inferred_duration)));
    if (!std::isfinite(applied_duration) ||
        applied_duration < normalized_inferred_duration) {
        return {
            false,
            "Animation duration cannot be shorter than the last authored key (" +
                std::to_string(normalized_inferred_duration) + " seconds)."};
    }

    AnimationEdit* existing = coalescible_duration_edit(project, animation_name);
    const std::optional<double> current_duration = existing != nullptr
        ? std::optional<double>(existing->duration)
        : animation->explicit_duration;
    if (current_duration.has_value() && *current_duration == applied_duration) {
        return {};
    }

    if (existing != nullptr) {
        existing->duration = applied_duration;
        return {true, {}};
    }

    AnimationEdit edit;
    edit.kind = AnimationEditKind::SetDuration;
    edit.name = std::string(animation_name);
    edit.duration = applied_duration;
    project->animation_edits.push_back(std::move(edit));
    return {true, {}};
}

AuthoringResult auto_extend_explicit_animation_durations(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton) {
    if (project == nullptr) {
        return missing_project_result();
    }

    ProjectData candidate = *project;
    bool changed = false;
    for (const runtime::AnimationData& animation : effective_skeleton.animations()) {
        const AnimationEdit* pending_duration =
            coalescible_duration_edit(&candidate, animation.name);
        if (!animation.explicit_duration.has_value() && pending_duration == nullptr) {
            continue;
        }
        const double authored_boundary = pending_duration != nullptr
            ? pending_duration->duration
            : *animation.explicit_duration;
        // MAR-172: without the same exclusion this would grow a shrunken
        // duration straight back to the boundary key one line later.
        double maximum_time = authored_boundary;
        include_animation_timeline_maximum_excluding_loop_boundaries(
            candidate.transform_timeline_edits, animation.name, &maximum_time);
        include_animation_timeline_maximum_excluding_loop_boundaries(
            candidate.mesh_deform_timeline_edits, animation.name, &maximum_time);
        include_animation_timeline_maximum(
            candidate.draw_order_timeline_edits, animation.name, &maximum_time);
        include_animation_timeline_maximum(
            candidate.event_timeline_edits, animation.name, &maximum_time);
        include_animation_timeline_maximum_excluding_loop_boundaries(
            candidate.slot_color_timeline_edits, animation.name, &maximum_time);
        include_animation_timeline_maximum(
            candidate.slot_attachment_timeline_edits, animation.name, &maximum_time);

        if (!std::isfinite(maximum_time) || maximum_time < 0.0) {
            return {
                false,
                "Animation timeline keys must have finite non-negative times."};
        }
        const double normalized_maximum = static_cast<double>(
            static_cast<runtime::AnimationScalar>(maximum_time));
        if (normalized_maximum <= authored_boundary) {
            continue;
        }

        const AuthoringResult result = set_animation_duration(
            &candidate, effective_skeleton, animation.name, normalized_maximum);
        if (!result) {
            return result;
        }
        changed = changed || result.changed;
    }

    if (!changed) {
        return {};
    }
    *project = std::move(candidate);
    return {true, {}};
}

TimelineRetimeResult retime_keyframes(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    double requested_delta,
    bool snap_to_frames,
    double frames_per_second) {
    if (project == nullptr) {
        return {{false, "Timeline authoring requires an open project."}, 0.0, 0U};
    }
    if (selectors.empty()) {
        return {{false, "At least one timeline key is required."}, 0.0, 0U};
    }
    if (!std::isfinite(requested_delta)) {
        return {{false, "Timeline retime delta must be finite."}, 0.0, 0U};
    }
    if (snap_to_frames && (!std::isfinite(frames_per_second) || frames_per_second <= 0.0)) {
        return {{false, "Timeline frames per second must be positive."}, 0.0, 0U};
    }

    ProjectData candidate = *project;
    std::vector<ResolvedTimelineKey> resolved;
    resolved.reserve(selectors.size());
    std::set<std::tuple<int, std::size_t, std::size_t>> identities;
    for (const TimelineKeySelector& selector : selectors) {
        if (selector.animation_name.empty() || !std::isfinite(selector.time) ||
            selector.time < 0.0) {
            return {{false, "Timeline selectors require an animation and non-negative finite time."},
                    0.0,
                    0U};
        }
        std::string error;
        const auto key = resolve_timeline_key(candidate, selector, &error);
        if (!key.has_value()) {
            return {{false, std::move(error)}, 0.0, 0U};
        }
        const auto identity = std::make_tuple(
            static_cast<int>(key->kind), key->timeline_index, key->key_index);
        if (!identities.insert(identity).second) {
            return {{false, "A timeline key was selected more than once."}, 0.0, 0U};
        }
        resolved.push_back(*key);
    }

    double applied_delta = requested_delta;
    const double earliest_original_time = std::min_element(
        resolved.begin(), resolved.end(), [](const auto& left, const auto& right) {
            return left.original_time < right.original_time;
        })->original_time;
    if (snap_to_frames) {
        applied_delta = *timeline_model::snap_delta_to_frames(
            earliest_original_time, applied_delta, frames_per_second);
    }

    double minimum_delta = -std::numeric_limits<double>::infinity();
    double maximum_delta = std::numeric_limits<double>::infinity();
    for (const ResolvedTimelineKey& key : resolved) {
        include_resolved_retime_bounds(
            candidate, key, resolved, &minimum_delta, &maximum_delta);
    }
    if (minimum_delta > maximum_delta) {
        return {{false, "Selected timeline keys have inconsistent retime bounds."}, 0.0, 0U};
    }
    const double snapped_delta = applied_delta;
    applied_delta = std::clamp(applied_delta, minimum_delta, maximum_delta);
    if (snap_to_frames && applied_delta != snapped_delta) {
        // The clamp landed on a raw neighbour bound; re-snap inward so the
        // snap_to_frames contract still holds for the written keys. When no
        // frame boundary exists inside the bounds, apply nothing.
        const double frame_seconds = 1.0 / frames_per_second;
        const double clamped_time = earliest_original_time + applied_delta;
        const double inward_time = applied_delta < snapped_delta
            ? std::floor(clamped_time / frame_seconds) * frame_seconds
            : std::ceil(clamped_time / frame_seconds) * frame_seconds;
        applied_delta = inward_time - earliest_original_time;
        if (applied_delta < minimum_delta || applied_delta > maximum_delta) {
            return {{false, {}}, 0.0, resolved.size()};
        }
    }
    if (std::abs(applied_delta) <= 1e-12) {
        return {{false, {}}, 0.0, resolved.size()};
    }

    for (const ResolvedTimelineKey& key : resolved) {
        apply_resolved_retime(&candidate, key, applied_delta);
    }
    sort_retimed_timelines(&candidate, resolved);
    *project = std::move(candidate);
    return {{true, {}}, applied_delta, resolved.size()};
}

std::string timeline_scale_selection_refusal(
    const ProjectData& project,
    const std::vector<TimelineKeySelector>& selectors) {
    if (selectors.size() < 2U) {
        return {};
    }
    std::vector<ResolvedTimelineKey> resolved;
    resolved.reserve(selectors.size());
    for (const TimelineKeySelector& selector : selectors) {
        std::string ignored;
        const auto key = resolve_timeline_key(project, selector, &ignored);
        if (!key.has_value()) {
            // An unresolvable selector is not a selection-shaped refusal; the
            // primitive reports it with its own `not found` message.
            return {};
        }
        resolved.push_back(*key);
    }
    const std::string_view animation_name = selectors.front().animation_name;
    for (const ResolvedTimelineKey& key : resolved) {
        if (resolved_key_is_loop_pinned(project, key)) {
            return loop_pinned_rejection(project, key, animation_name);
        }
    }
    std::set<std::size_t> event_indices;
    std::optional<std::size_t> event_timeline;
    for (const ResolvedTimelineKey& key : resolved) {
        if (key.kind != TimelineKeyKind::Event) continue;
        event_timeline = key.timeline_index;
        event_indices.insert(key.key_index);
    }
    if (event_timeline.has_value()) {
        const auto tie = partial_tie_time(
            project.event_timeline_edits[*event_timeline], event_indices);
        if (tie.has_value()) {
            return partial_event_tie_rejection(*tie, animation_name);
        }
    }
    return {};
}

TimelineScaleResult scale_keyframe_times(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    TimelineScalePivot pivot,
    double scale) {
    const auto fail = [](std::string message) {
        return TimelineScaleResult{
            {false, std::move(message)}, 0.0, 1.0, 0.0, 0.0, 0U, 0U};
    };
    if (project == nullptr) {
        return fail("Timeline authoring requires an open project.");
    }
    if (selectors.empty()) {
        return fail("At least one timeline key is required.");
    }
    if (!std::isfinite(scale) || scale <= 0.0) {
        return fail("Timeline scale ratio must be finite and positive.");
    }

    ProjectData candidate = *project;
    std::vector<ResolvedTimelineKey> resolved;
    resolved.reserve(selectors.size());
    std::set<std::tuple<int, std::size_t, std::size_t>> identities;
    for (const TimelineKeySelector& selector : selectors) {
        if (selector.animation_name.empty() || !std::isfinite(selector.time) ||
            selector.time < 0.0) {
            return fail(
                "Timeline selectors require an animation and non-negative finite time.");
        }
        if (selector.animation_name != selectors.front().animation_name) {
            // The dopesheet rebuilds `selected_keys` per animation, so a
            // multi-animation set can only come from a script; the pivot is
            // meaningless across two independent time lines.
            return fail(
                "Timeline scaling requires every key to belong to one animation.");
        }
        std::string error;
        const auto key = resolve_timeline_key(candidate, selector, &error);
        if (!key.has_value()) {
            return fail(std::move(error));
        }
        const auto identity = std::make_tuple(
            static_cast<int>(key->kind), key->timeline_index, key->key_index);
        if (!identities.insert(identity).second) {
            return fail("A timeline key was selected more than once.");
        }
        ResolvedTimelineKey snapshot = *key;
        snapshot.original_time = resolved_stored_key_time(candidate, snapshot);
        resolved.push_back(snapshot);
    }
    const std::string_view animation_name = selectors.front().animation_name;

    // A partially pinned scale is not a scale, so this rejects where a retime
    // would collapse to `changed == false`. Checked before any target is
    // computed, so the project is untouched.
    for (const ResolvedTimelineKey& key : resolved) {
        if (!resolved_key_is_loop_pinned(candidate, key)) {
            continue;
        }
        return fail(loop_pinned_rejection(candidate, key, animation_name));
    }

    const auto [minimum, maximum] = std::minmax_element(
        resolved.begin(), resolved.end(), [](const auto& left, const auto& right) {
            return left.original_time < right.original_time;
        });
    const double minimum_time = minimum->original_time;
    const double maximum_time = maximum->original_time;
    const double original_span = maximum_time - minimum_time;
    if (original_span <= kKeyTimeEpsilon) {
        return fail(
            "Scaling requires a selection spanning at least two distinct key times.");
    }
    const double pivot_time =
        pivot == TimelineScalePivot::RangeStart ? minimum_time : maximum_time;

    // Every target reads the snapshot taken above, never a live value, so no
    // write can observe another write and step 11's order cannot matter.
    bool moved = false;
    for (const ResolvedTimelineKey& key : resolved) {
        const double target = pivot_time + (key.original_time - pivot_time) * scale;
        if (!std::isfinite(target)) {
            return fail(
                "Scaling would place a key at a non-finite time in animation '" +
                std::string(animation_name) + "'.");
        }
        if (target < 0.0) {
            return fail(
                "Scaling would place animation '" + std::string(animation_name) +
                "' " + scale_lane_label(candidate, key) + " at " +
                std::to_string(target) + " s, below zero; key times cannot be negative.");
        }
        if (!finite_animation_scalar(target)) {
            return fail(
                "Scaling would place animation '" + std::string(animation_name) +
                "' " + scale_lane_label(candidate, key) + " at " +
                std::to_string(target) + " s, which is not representable at runtime.");
        }
        if (std::abs(target - key.original_time) > 1e-12) {
            moved = true;
        }
    }

    // Validate the whole projected key set of every affected timeline before
    // writing anything, so a transient mid-write state is never observed.
    std::set<std::pair<int, std::size_t>> affected;
    for (const ResolvedTimelineKey& key : resolved) {
        affected.insert({static_cast<int>(key.kind), key.timeline_index});
    }
    for (const auto& [kind_token, timeline_index] : affected) {
        const auto kind = static_cast<TimelineKeyKind>(kind_token);
        std::set<std::size_t> selected_indices;
        ResolvedTimelineKey sample;
        for (const ResolvedTimelineKey& key : resolved) {
            if (key.kind == kind && key.timeline_index == timeline_index) {
                selected_indices.insert(key.key_index);
                sample = key;
            }
        }
        const std::string label = scale_lane_label(candidate, sample);
        std::string error;
        bool valid = true;
        switch (kind) {
        case TimelineKeyKind::Transform:
            valid = validate_projected_scale(
                candidate.transform_timeline_edits[timeline_index], selected_indices,
                kind, animation_name, label, pivot_time, scale, &error);
            break;
        case TimelineKeyKind::Deform:
            valid = validate_projected_scale(
                candidate.mesh_deform_timeline_edits[timeline_index], selected_indices,
                kind, animation_name, label, pivot_time, scale, &error);
            break;
        case TimelineKeyKind::DrawOrder:
            valid = validate_projected_scale(
                candidate.draw_order_timeline_edits[timeline_index], selected_indices,
                kind, animation_name, label, pivot_time, scale, &error);
            break;
        case TimelineKeyKind::Event:
            valid = validate_projected_scale(
                candidate.event_timeline_edits[timeline_index], selected_indices,
                kind, animation_name, label, pivot_time, scale, &error);
            break;
        case TimelineKeyKind::SlotColor:
            valid = validate_projected_scale(
                candidate.slot_color_timeline_edits[timeline_index], selected_indices,
                kind, animation_name, label, pivot_time, scale, &error);
            break;
        case TimelineKeyKind::SlotAttachment:
            valid = validate_projected_scale(
                candidate.slot_attachment_timeline_edits[timeline_index],
                selected_indices, kind, animation_name, label, pivot_time, scale,
                &error);
            break;
        }
        if (!valid) {
            return fail(std::move(error));
        }
    }

    // The times the selectors actually resolved to, so a reporting caller can
    // read what the project holds rather than echoing what it asked for.
    std::vector<double> previous_times;
    previous_times.reserve(resolved.size());
    for (const ResolvedTimelineKey& key : resolved) {
        previous_times.push_back(key.original_time);
    }

    const double scaled_span = original_span * scale;
    if (std::abs(scale - 1.0) <= 1e-12 || !moved) {
        return {{false, {}}, pivot_time, 1.0, original_span, original_span,
                resolved.size(), 0U, std::move(previous_times)};
    }

    std::size_t moved_key_count = 0U;
    for (const ResolvedTimelineKey& key : resolved) {
        const double target = pivot_time + (key.original_time - pivot_time) * scale;
        if (std::abs(target - key.original_time) > 1e-12) {
            ++moved_key_count;
        }
        apply_resolved_scale(&candidate, key, pivot_time, scale);
    }
    // Defence in depth, and provably a no-op: the projected sequence validated
    // above is non-decreasing in original index order, so nothing can reorder.
    sort_retimed_timelines(&candidate, resolved);
    *project = std::move(candidate);
    return {{true, {}}, pivot_time, scale, original_span, scaled_span,
            resolved.size(), moved_key_count, std::move(previous_times)};
}

TimelineScalarOffsetResult offset_keyframe_scalars(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    TimelineScalarComponent component,
    double requested_delta) {
    if (project == nullptr) {
        return {{false, "Timeline authoring requires an open project."}, 0.0, 0U};
    }
    if (selectors.empty()) {
        return {{false, "At least one timeline key is required."}, 0.0, 0U};
    }
    if (!std::isfinite(requested_delta)) {
        return {{false, "Timeline scalar delta must be finite."}, 0.0, 0U};
    }

    ProjectData candidate = *project;
    std::vector<ResolvedTimelineKey> resolved;
    resolved.reserve(selectors.size());
    std::vector<double> original_values;
    original_values.reserve(selectors.size());
    std::set<std::tuple<int, std::size_t, std::size_t>> identities;
    for (const TimelineKeySelector& selector : selectors) {
        if (selector.animation_name.empty() || !std::isfinite(selector.time) ||
            selector.time < 0.0) {
            return {{false, "Timeline selectors require an animation and non-negative finite time."},
                    0.0,
                    0U};
        }
        std::string error;
        const auto key = resolve_timeline_key(candidate, selector, &error);
        if (!key.has_value()) {
            return {{false, std::move(error)}, 0.0, 0U};
        }
        const auto identity = std::make_tuple(
            static_cast<int>(key->kind), key->timeline_index, key->key_index);
        if (!identities.insert(identity).second) {
            return {{false, "A timeline key was selected more than once."}, 0.0, 0U};
        }
        double current = 0.0;
        if (!read_scalar_component(candidate, *key, component, &current)) {
            return {{false, "The selected timeline key does not expose that scalar component."},
                    0.0,
                    0U};
        }
        if (!finite_animation_scalar(current)) {
            return {{false, "The selected timeline key holds a non-finite scalar value."},
                    0.0,
                    0U};
        }
        resolved.push_back(*key);
        original_values.push_back(current);
    }

    double applied_delta = requested_delta;
    if (component_is_color_channel(component)) {
        // The clamp is group-wide so a multi-key drag stops as one unit
        // instead of collapsing against the boundary. Both bounds are widened
        // to include zero, so a group that imported data already placed
        // outside [0, 1] can be dragged back toward the legal range but never
        // further out of it, and a zero-net drag always stays a no-op.
        const auto bounds = std::minmax_element(
            original_values.begin(), original_values.end());
        const double lower = std::min(0.0, -*bounds.first);
        const double upper = std::max(0.0, 1.0 - *bounds.second);
        applied_delta = std::clamp(requested_delta, lower, upper);
    }
    if (std::abs(applied_delta) <= 1e-12) {
        return {{false, {}}, 0.0, resolved.size()};
    }

    for (std::size_t index = 0U; index < resolved.size(); ++index) {
        double value = original_values[index] + applied_delta;
        if (component_is_color_channel(component)) {
            // Defensive only: the group bounds above already guarantee this
            // envelope. Clamping to a flat [0, 1] instead would truncate an
            // imported out-of-range key and silently destroy the authored
            // spacing the group clamp exists to protect.
            value = std::clamp(
                value,
                std::min(0.0, original_values[index]),
                std::max(1.0, original_values[index]));
        }
        if (!finite_animation_scalar(value)) {
            return {{false, "A timeline scalar edit left the finite float32 range."},
                    0.0,
                    0U};
        }
        write_scalar_component(&candidate, resolved[index], component, value);
    }
    *project = std::move(candidate);
    return {{true, {}}, applied_delta, resolved.size()};
}

namespace {

/**
 * @brief Value comparison for `runtime::Interpolation`, which has no `==`.
 *
 * Both sides are already float32, so an exact comparison is the right test:
 * a rewrite that narrows to the same bits genuinely changes nothing.
 */
bool same_interpolation(
    const runtime::Interpolation& left,
    const runtime::Interpolation& right) {
    if (left.kind() != right.kind()) return false;
    if (left.kind() != runtime::InterpolationKind::CubicBezier) return true;
    const auto& first = left.cubic_bezier();
    const auto& second = right.cubic_bezier();
    return first.cx1 == second.cx1 && first.cy1 == second.cy1 &&
        first.cx2 == second.cx2 && first.cy2 == second.cy2;
}

/**
 * @brief Reads the outgoing easing of a resolved key, or nullptr for kinds
 *        that carry none.
 */
const runtime::Interpolation* read_key_interpolation(
    const ProjectData& project,
    const ResolvedTimelineKey& resolved) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform:
        return &project.transform_timeline_edits[resolved.timeline_index]
                    .keyframes[resolved.key_index]
                    .interpolation;
    case TimelineKeyKind::Deform:
        return &project.mesh_deform_timeline_edits[resolved.timeline_index]
                    .keyframes[resolved.key_index]
                    .interpolation;
    case TimelineKeyKind::SlotColor:
        return &project.slot_color_timeline_edits[resolved.timeline_index]
                    .keyframes[resolved.key_index]
                    .interpolation;
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::Event:
    case TimelineKeyKind::SlotAttachment:
        return nullptr;
    }
    return nullptr;
}

/** @brief Writes the outgoing easing of a resolved key that carries one. */
void write_key_interpolation(
    ProjectData* project,
    const ResolvedTimelineKey& resolved,
    const runtime::Interpolation& interpolation) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform:
        project->transform_timeline_edits[resolved.timeline_index]
            .keyframes[resolved.key_index]
            .interpolation = interpolation;
        return;
    case TimelineKeyKind::Deform:
        project->mesh_deform_timeline_edits[resolved.timeline_index]
            .keyframes[resolved.key_index]
            .interpolation = interpolation;
        return;
    case TimelineKeyKind::SlotColor:
        project->slot_color_timeline_edits[resolved.timeline_index]
            .keyframes[resolved.key_index]
            .interpolation = interpolation;
        return;
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::Event:
    case TimelineKeyKind::SlotAttachment:
        return;
    }
}

/**
 * @brief Reads the recorded curve mode of a resolved key, or nullptr.
 *
 * Deform keys deliberately carry no curve mode: a vertex-offset vector has no
 * canonical scalar to drive a tangent, so the exclusion is enforced by the
 * missing member rather than by a branch. The discrete families carry no
 * easing at all.
 */
const TimelineCurveMode* read_key_curve_mode(
    const ProjectData& project,
    const ResolvedTimelineKey& resolved) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform:
        return &project.transform_timeline_edits[resolved.timeline_index]
                    .keyframes[resolved.key_index]
                    .curve_mode;
    case TimelineKeyKind::SlotColor:
        return &project.slot_color_timeline_edits[resolved.timeline_index]
                    .keyframes[resolved.key_index]
                    .curve_mode;
    case TimelineKeyKind::Deform:
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::Event:
    case TimelineKeyKind::SlotAttachment:
        return nullptr;
    }
    return nullptr;
}

/** @brief Writes the recorded curve mode of a key that carries one. */
void write_key_curve_mode(
    ProjectData* project,
    const ResolvedTimelineKey& resolved,
    TimelineCurveMode mode) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform:
        project->transform_timeline_edits[resolved.timeline_index]
            .keyframes[resolved.key_index]
            .curve_mode = mode;
        return;
    case TimelineKeyKind::SlotColor:
        project->slot_color_timeline_edits[resolved.timeline_index]
            .keyframes[resolved.key_index]
            .curve_mode = mode;
        return;
    case TimelineKeyKind::Deform:
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::Event:
    case TimelineKeyKind::SlotAttachment:
        return;
    }
}

/** @brief Reads the recorded curve driver of a resolved key, or nullptr. */
const TimelineScalarComponent* read_key_curve_driver(
    const ProjectData& project,
    const ResolvedTimelineKey& resolved) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform:
        return &project.transform_timeline_edits[resolved.timeline_index]
                    .keyframes[resolved.key_index]
                    .curve_driver;
    case TimelineKeyKind::SlotColor:
        return &project.slot_color_timeline_edits[resolved.timeline_index]
                    .keyframes[resolved.key_index]
                    .curve_driver;
    case TimelineKeyKind::Deform:
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::Event:
    case TimelineKeyKind::SlotAttachment:
        return nullptr;
    }
    return nullptr;
}

/** @brief Writes the recorded curve driver of a key that carries one. */
void write_key_curve_driver(
    ProjectData* project,
    const ResolvedTimelineKey& resolved,
    TimelineScalarComponent driver) {
    switch (resolved.kind) {
    case TimelineKeyKind::Transform:
        project->transform_timeline_edits[resolved.timeline_index]
            .keyframes[resolved.key_index]
            .curve_driver = driver;
        return;
    case TimelineKeyKind::SlotColor:
        project->slot_color_timeline_edits[resolved.timeline_index]
            .keyframes[resolved.key_index]
            .curve_driver = driver;
        return;
    case TimelineKeyKind::Deform:
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::Event:
    case TimelineKeyKind::SlotAttachment:
        return;
    }
}

/** @brief The Transform channel of a resolved key; Rotate for every other family. */
TransformTimelineChannel resolved_key_channel(
    const ProjectData& project,
    const ResolvedTimelineKey& resolved) {
    if (resolved.kind != TimelineKeyKind::Transform) {
        return TransformTimelineChannel::Rotate;
    }
    return project.transform_timeline_edits[resolved.timeline_index].channel;
}

/**
 * @brief Validates one cubic control point value.
 *
 * The finiteness test must precede the range test: `NaN < 0.0` and
 * `NaN > 1.0` are both false, so a naive range check would let NaN through.
 * The float32 narrowing is checked as well, so `1e300` is rejected rather
 * than silently stored as infinity.
 */
bool valid_bezier_control_point(double value, bool is_x, std::string* error_out) {
    if (!std::isfinite(value)) {
        *error_out = "Bezier control points must be finite.";
        return false;
    }
    if (std::abs(value) >
        static_cast<double>(std::numeric_limits<runtime::AnimationScalar>::max())) {
        *error_out = "Bezier control points must fit the runtime float32 range.";
        return false;
    }
    const double narrowed =
        static_cast<double>(static_cast<runtime::AnimationScalar>(value));
    if (!std::isfinite(narrowed)) {
        *error_out = "Bezier control points must stay finite after float32 narrowing.";
        return false;
    }
    if (is_x && (value < 0.0 || value > 1.0 || narrowed < 0.0 || narrowed > 1.0)) {
        *error_out = "bezier x control points must stay within [0, 1]";
        return false;
    }
    return true;
}

} // namespace

TimelineInterpolationResult set_keyframe_interpolation(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    runtime::InterpolationKind kind,
    const std::array<double, 4>& control_points) {
    if (project == nullptr) {
        return {{false, "Timeline authoring requires an open project."}, 0U, 0U};
    }
    if (selectors.empty()) {
        return {{false, "At least one timeline key is required."}, 0U, 0U};
    }
    if (kind == runtime::InterpolationKind::CubicBezier) {
        for (std::size_t index = 0U; index < control_points.size(); ++index) {
            std::string error;
            if (!valid_bezier_control_point(
                    control_points[index], index % 2U == 0U, &error)) {
                return {{false, std::move(error)}, 0U, 0U};
            }
        }
    }

    ProjectData candidate = *project;
    std::vector<ResolvedTimelineKey> resolved;
    resolved.reserve(selectors.size());
    std::set<std::tuple<int, std::size_t, std::size_t>> identities;
    for (const TimelineKeySelector& selector : selectors) {
        if (selector.animation_name.empty() || !std::isfinite(selector.time) ||
            selector.time < 0.0) {
            return {{false, "Timeline selectors require an animation and non-negative finite time."},
                    0U,
                    0U};
        }
        std::string error;
        const auto key = resolve_timeline_key(candidate, selector, &error);
        if (!key.has_value()) {
            return {{false, std::move(error)}, 0U, 0U};
        }
        const auto identity = std::make_tuple(
            static_cast<int>(key->kind), key->timeline_index, key->key_index);
        if (!identities.insert(identity).second) {
            return {{false, "A timeline key was selected more than once."}, 0U, 0U};
        }
        if (read_key_interpolation(candidate, *key) == nullptr) {
            return {{false, "The selected timeline key does not carry an outgoing easing."},
                    0U,
                    0U};
        }
        resolved.push_back(*key);
    }

    // The Interpolation is constructed only after every value passed, so a
    // rejected request never enters the process-wide cubic LUT cache.
    runtime::Interpolation interpolation;
    switch (kind) {
    case runtime::InterpolationKind::Linear:
        interpolation = runtime::Interpolation::linear();
        break;
    case runtime::InterpolationKind::Stepped:
        interpolation = runtime::Interpolation::stepped();
        break;
    case runtime::InterpolationKind::CubicBezier:
        interpolation = runtime::Interpolation::cubic_bezier(
            control_points[0], control_points[1], control_points[2], control_points[3]);
        break;
    }

    std::size_t changed_key_count = 0U;
    for (const ResolvedTimelineKey& key : resolved) {
        const runtime::Interpolation* current = read_key_interpolation(candidate, key);
        const bool easing_changed =
            current != nullptr && !same_interpolation(*current, interpolation);
        // MAR-171: writing an absolute easing takes the key off its
        // neighbours, so the demotion is itself an authored change even when
        // the four control points land on their previous values.
        const TimelineCurveMode* mode = read_key_curve_mode(candidate, key);
        const bool demoted = mode != nullptr && *mode != TimelineCurveMode::Manual;
        if (easing_changed || demoted) {
            ++changed_key_count;
        }
    }
    if (changed_key_count == 0U) {
        return {{false, {}}, resolved.size(), 0U};
    }

    for (const ResolvedTimelineKey& key : resolved) {
        write_key_interpolation(&candidate, key, interpolation);
        write_key_curve_mode(&candidate, key, TimelineCurveMode::Manual);
    }
    *project = std::move(candidate);
    return {{true, {}}, resolved.size(), changed_key_count};
}


namespace {

/**
 * @brief Compile-time proof that the preset table can never violate the format.
 *
 * Enum order, non-empty labels, and the `cx in [0, 1]` invariant both loaders
 * enforce are all checked here, so a preset that would be rejected on load is a
 * compile error rather than a runtime failure.
 */
constexpr bool curve_presets_are_well_formed() {
    for (std::size_t index = 0U; index < kCurvePresets.size(); ++index) {
        const CurvePresetDefinition& entry = kCurvePresets[index];
        if (static_cast<std::size_t>(entry.preset) != index) return false;
        if (entry.token.empty() || entry.display_name.empty()) return false;
        if (entry.kind == runtime::InterpolationKind::CubicBezier) {
            if (!(entry.control_points[0] >= 0.0 && entry.control_points[0] <= 1.0)) {
                return false;
            }
            if (!(entry.control_points[2] >= 0.0 && entry.control_points[2] <= 1.0)) {
                return false;
            }
        }
    }
    return true;
}

static_assert(curve_presets_are_well_formed(),
              "curve presets must be in enum order and keep cx inside the [0, 1] "
              "invariant both .marrow and .mskl loaders enforce");

static_assert(
    kCurvePresets.size() ==
        static_cast<std::size_t>(CurvePreset::EaseInOut) + 1U,
    "the preset table must carry exactly one entry per CurvePreset enumerator");

} // namespace

const CurvePresetDefinition& curve_preset_definition(CurvePreset preset) {
    // No `default` label, so adding an enumerator without a table row becomes a
    // compiler warning rather than a silent Linear fallback.
    switch (preset) {
    case CurvePreset::Linear: return kCurvePresets[0];
    case CurvePreset::Stepped: return kCurvePresets[1];
    case CurvePreset::Ease: return kCurvePresets[2];
    case CurvePreset::EaseIn: return kCurvePresets[3];
    case CurvePreset::EaseOut: return kCurvePresets[4];
    case CurvePreset::EaseInOut: return kCurvePresets[5];
    }
    return kCurvePresets[0];
}

runtime::Interpolation curve_preset_interpolation(CurvePreset preset) {
    const CurvePresetDefinition& definition = curve_preset_definition(preset);
    switch (definition.kind) {
    case runtime::InterpolationKind::Linear:
        return runtime::Interpolation::linear();
    case runtime::InterpolationKind::Stepped:
        return runtime::Interpolation::stepped();
    case runtime::InterpolationKind::CubicBezier:
        return runtime::Interpolation::cubic_bezier(
            definition.control_points[0],
            definition.control_points[1],
            definition.control_points[2],
            definition.control_points[3]);
    }
    return runtime::Interpolation::linear();
}

std::optional<CurvePreset> curve_preset_of(const runtime::Interpolation& interpolation) {
    switch (interpolation.kind()) {
    case runtime::InterpolationKind::Linear:
        return CurvePreset::Linear;
    case runtime::InterpolationKind::Stepped:
        return CurvePreset::Stepped;
    case runtime::InterpolationKind::CubicBezier:
        break;
    }
    // The stored curve is float32, so the table's doubles are narrowed before
    // the comparison. Comparing against the double literals would make a
    // just-applied preset read Custom.
    const runtime::CubicBezierControlPoints& stored = interpolation.cubic_bezier();
    for (const CurvePresetDefinition& entry : kCurvePresets) {
        if (entry.kind != runtime::InterpolationKind::CubicBezier) continue;
        if (stored.cx1 ==
                static_cast<runtime::AnimationScalar>(entry.control_points[0]) &&
            stored.cy1 ==
                static_cast<runtime::AnimationScalar>(entry.control_points[1]) &&
            stored.cx2 ==
                static_cast<runtime::AnimationScalar>(entry.control_points[2]) &&
            stored.cy2 ==
                static_cast<runtime::AnimationScalar>(entry.control_points[3])) {
            return entry.preset;
        }
    }
    return std::nullopt;
}

std::optional<CurvePreset> curve_preset_from_token(std::string_view token) {
    for (const CurvePresetDefinition& entry : kCurvePresets) {
        if (entry.token == token) return entry.preset;
    }
    return std::nullopt;
}

namespace {

/** @brief The seven driver tokens, in `TimelineScalarComponent` enum order. */
constexpr std::array<std::string_view, 7> kCurveDriverTokens{
    "angle", "x", "y", "r", "g", "b", "a"};

} // namespace

std::string_view curve_mode_token(TimelineCurveMode mode) {
    return mode == TimelineCurveMode::Auto ? "auto" : "manual";
}

std::optional<TimelineCurveMode> curve_mode_from_token(std::string_view token) {
    if (token == "manual") return TimelineCurveMode::Manual;
    if (token == "auto") return TimelineCurveMode::Auto;
    return std::nullopt;
}

std::string_view curve_driver_token(TimelineScalarComponent driver) {
    const auto index = static_cast<std::size_t>(driver);
    return index < kCurveDriverTokens.size() ? kCurveDriverTokens[index]
                                             : kCurveDriverTokens[0];
}

std::optional<TimelineScalarComponent> curve_driver_from_token(std::string_view token) {
    for (std::size_t index = 0U; index < kCurveDriverTokens.size(); ++index) {
        if (kCurveDriverTokens[index] == token) {
            return static_cast<TimelineScalarComponent>(index);
        }
    }
    return std::nullopt;
}

TimelineScalarComponent default_curve_driver(
    TimelineKeyKind kind,
    TransformTimelineChannel channel) {
    // The family's lowest-indexed component, which is the same rule the graph
    // uses when `graph_view.active_component` is unset.
    if (kind == TimelineKeyKind::SlotColor) return TimelineScalarComponent::Red;
    if (channel == TransformTimelineChannel::Rotate) {
        return TimelineScalarComponent::Angle;
    }
    return TimelineScalarComponent::X;
}

bool curve_driver_is_authorable(
    TimelineKeyKind kind,
    TransformTimelineChannel channel,
    TimelineScalarComponent driver) {
    return family_owns_scalar_component(kind, channel, driver);
}

namespace {

/** @brief A human-readable name for one track, used only in resolver errors. */
std::string transform_track_label(const TransformTimelineEdit& edit) {
    std::string channel;
    switch (edit.channel) {
    case TransformTimelineChannel::Rotate:
        channel = "rotate";
        break;
    case TransformTimelineChannel::Translate:
        channel = "translate";
        break;
    case TransformTimelineChannel::Scale:
        channel = "scale";
        break;
    case TransformTimelineChannel::Shear:
        channel = "shear";
        break;
    }
    return "bone '" + edit.bone_name + "' " + channel;
}

/**
 * @brief The first segment index the resolver cannot use, for the error text.
 *
 * `segment_control_points()` rejects the whole track without naming a segment,
 * so the message is built here from the same three conditions.
 */
std::size_t first_unusable_segment(const std::vector<curve_auto::Sample>& samples) {
    for (std::size_t index = 0U; index + 1U < samples.size(); ++index) {
        const curve_auto::Sample& from = samples[index];
        const curve_auto::Sample& to = samples[index + 1U];
        if (!std::isfinite(from.time_seconds) || !std::isfinite(from.value) ||
            !std::isfinite(to.time_seconds) || !std::isfinite(to.value)) {
            return index;
        }
        if (to.time_seconds - from.time_seconds <= curve_auto::kMinimumSegmentSeconds) {
            return index;
        }
    }
    return 0U;
}

/**
 * @brief Resolves one track's automatic segments in place.
 *
 * Segment `i` is resolved from key `i`'s own driver over the whole track's
 * series for that component, so mixed drivers on one track are well defined and
 * a driver change is local to its own segment. The four passes run once per
 * distinct driver present, at most four times.
 */
template <typename Keyframes, typename ReadComponent>
bool resolve_track_automatic_curves(
    Keyframes* keyframes,
    ReadComponent&& read_component,
    std::string_view animation_name,
    const std::string& track_label,
    std::size_t* auto_key_count,
    std::size_t* resolved_key_count,
    std::string* error_out) {
    const std::size_t count = keyframes->size();
    if (count < 2U) return true;

    std::set<TimelineScalarComponent> drivers;
    for (std::size_t index = 0U; index + 1U < count; ++index) {
        if ((*keyframes)[index].curve_mode != TimelineCurveMode::Auto) continue;
        drivers.insert((*keyframes)[index].curve_driver);
        ++*auto_key_count;
    }
    if (drivers.empty()) return true;

    std::map<TimelineScalarComponent, std::vector<std::array<double, 4>>> resolved;
    for (const TimelineScalarComponent driver : drivers) {
        std::vector<curve_auto::Sample> samples;
        samples.reserve(count);
        for (const auto& keyframe : *keyframes) {
            samples.push_back(
                curve_auto::Sample{keyframe.time, read_component(keyframe, driver)});
        }
        auto control_points = curve_auto::segment_control_points(samples);
        if (!control_points.has_value()) {
            *error_out = "Automatic curves for animation '" +
                std::string(animation_name) + "' " + track_label + " segment " +
                std::to_string(first_unusable_segment(samples)) +
                " need finite, strictly increasing key times at least 1 us apart.";
            return false;
        }
        resolved.emplace(driver, std::move(*control_points));
    }

    for (std::size_t index = 0U; index + 1U < count; ++index) {
        auto& keyframe = (*keyframes)[index];
        if (keyframe.curve_mode != TimelineCurveMode::Auto) continue;
        const auto& points = resolved.at(keyframe.curve_driver)[index];
        const runtime::Interpolation easing = runtime::Interpolation::cubic_bezier(
            points[0], points[1], points[2], points[3]);
        if (!same_interpolation(keyframe.interpolation, easing)) {
            keyframe.interpolation = easing;
            ++*resolved_key_count;
        }
    }
    return true;
}

} // namespace

TimelineAutoCurveResult resolve_automatic_curves(
    ProjectData* project,
    std::string_view animation_name) {
    if (project == nullptr) {
        return {{false, "Timeline authoring requires an open project."}, 0U, 0U};
    }

    ProjectData candidate = *project;
    std::size_t auto_key_count = 0U;
    std::size_t resolved_key_count = 0U;
    std::string error;

    for (TransformTimelineEdit& edit : candidate.transform_timeline_edits) {
        if (!animation_name.empty() && edit.animation_name != animation_name) continue;
        const bool rotate = edit.channel == TransformTimelineChannel::Rotate;
        const auto read_component = [rotate](
                                        const TransformKeyframeEdit& keyframe,
                                        TimelineScalarComponent driver) {
            if (rotate) return keyframe.angle;
            return driver == TimelineScalarComponent::Y ? keyframe.y : keyframe.x;
        };
        if (!resolve_track_automatic_curves(
                &edit.keyframes,
                read_component,
                edit.animation_name,
                transform_track_label(edit),
                &auto_key_count,
                &resolved_key_count,
                &error)) {
            return {{false, std::move(error)}, 0U, 0U};
        }
    }
    for (SlotColorTimelineEdit& edit : candidate.slot_color_timeline_edits) {
        if (!animation_name.empty() && edit.animation_name != animation_name) continue;
        const auto read_component = [](const SlotColorKeyframeEdit& keyframe,
                                       TimelineScalarComponent driver) {
            switch (driver) {
            case TimelineScalarComponent::Green:
                return static_cast<double>(keyframe.color.g);
            case TimelineScalarComponent::Blue:
                return static_cast<double>(keyframe.color.b);
            case TimelineScalarComponent::Alpha:
                return static_cast<double>(keyframe.color.a);
            case TimelineScalarComponent::Red:
            case TimelineScalarComponent::Angle:
            case TimelineScalarComponent::X:
            case TimelineScalarComponent::Y:
                break;
            }
            return static_cast<double>(keyframe.color.r);
        };
        if (!resolve_track_automatic_curves(
                &edit.keyframes,
                read_component,
                edit.animation_name,
                "slot '" + edit.slot_name + "' color",
                &auto_key_count,
                &resolved_key_count,
                &error)) {
            return {{false, std::move(error)}, 0U, 0U};
        }
    }

    if (resolved_key_count == 0U) {
        return {{false, {}}, auto_key_count, 0U};
    }
    *project = std::move(candidate);
    return {{true, {}}, auto_key_count, resolved_key_count};
}

TimelineCurveModeResult set_keyframe_curve_mode(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    TimelineCurveMode mode,
    std::optional<TimelineScalarComponent> driver) {
    if (project == nullptr) {
        return {{false, "Timeline authoring requires an open project."}, 0U, 0U, 0U};
    }
    if (selectors.empty()) {
        return {{false, "At least one timeline key is required."}, 0U, 0U, 0U};
    }
    if (mode != TimelineCurveMode::Auto && driver.has_value()) {
        return {{false, "A curve driver requires automatic curve mode."}, 0U, 0U, 0U};
    }

    ProjectData candidate = *project;
    std::vector<ResolvedTimelineKey> resolved;
    resolved.reserve(selectors.size());
    std::vector<TimelineScalarComponent> effective_drivers;
    effective_drivers.reserve(selectors.size());
    std::set<std::tuple<int, std::size_t, std::size_t>> identities;
    std::vector<std::string> animations;
    for (const TimelineKeySelector& selector : selectors) {
        if (selector.animation_name.empty() || !std::isfinite(selector.time) ||
            selector.time < 0.0) {
            return {{false, "Timeline selectors require an animation and non-negative finite time."},
                    0U,
                    0U,
                    0U};
        }
        std::string error;
        const auto key = resolve_timeline_key(candidate, selector, &error);
        if (!key.has_value()) {
            return {{false, std::move(error)}, 0U, 0U, 0U};
        }
        const auto identity = std::make_tuple(
            static_cast<int>(key->kind), key->timeline_index, key->key_index);
        if (!identities.insert(identity).second) {
            return {{false, "A timeline key was selected more than once."}, 0U, 0U, 0U};
        }
        if (read_key_curve_mode(candidate, *key) == nullptr) {
            return {{false,
                     "Only transform and slot colour keys carry a curve mode; a deform "
                     "key's value has no canonical scalar to drive a tangent."},
                    0U,
                    0U,
                    0U};
        }
        const TransformTimelineChannel channel =
            resolved_key_channel(candidate, *key);
        const TimelineScalarComponent effective =
            driver.value_or(default_curve_driver(key->kind, channel));
        if (!curve_driver_is_authorable(key->kind, channel, effective)) {
            return {{false, "The curve driver must name a component this timeline owns."},
                    0U,
                    0U,
                    0U};
        }
        if (std::find(animations.begin(), animations.end(), selector.animation_name) ==
            animations.end()) {
            animations.push_back(selector.animation_name);
        }
        resolved.push_back(*key);
        effective_drivers.push_back(effective);
    }

    std::size_t changed_key_count = 0U;
    for (std::size_t index = 0U; index < resolved.size(); ++index) {
        const TimelineCurveMode* current_mode =
            read_key_curve_mode(candidate, resolved[index]);
        if (*current_mode != mode) {
            ++changed_key_count;
            continue;
        }
        // A manual key records no driver at all, so re-applying `Manual` can
        // never differ and there is exactly one representation of "manual".
        if (mode != TimelineCurveMode::Auto) continue;
        const TimelineScalarComponent* current_driver =
            read_key_curve_driver(candidate, resolved[index]);
        if (*current_driver != effective_drivers[index]) ++changed_key_count;
    }
    for (std::size_t index = 0U; index < resolved.size(); ++index) {
        write_key_curve_mode(&candidate, resolved[index], mode);
        if (mode == TimelineCurveMode::Auto) {
            write_key_curve_driver(&candidate, resolved[index], effective_drivers[index]);
        }
    }

    // Resolving inside the candidate keeps the whole write atomic: a rejected
    // resolve leaves `*project` exactly as it was, mode included.
    std::size_t resolved_key_count = 0U;
    for (const std::string& animation : animations) {
        const TimelineAutoCurveResult result =
            resolve_automatic_curves(&candidate, animation);
        if (!result) {
            return {{false, result.error}, 0U, 0U, 0U};
        }
        resolved_key_count += result.resolved_key_count;
    }

    if (changed_key_count == 0U && resolved_key_count == 0U) {
        return {{false, {}}, resolved.size(), 0U, 0U};
    }
    *project = std::move(candidate);
    return {{true, {}}, resolved.size(), changed_key_count, resolved_key_count};
}

// ---------------------------------------------------------------------
// MAR-172 loop boundary key synchronization.
// ---------------------------------------------------------------------

std::string_view timeline_lane_kind_token(TimelineLaneKind kind) {
    switch (kind) {
    case TimelineLaneKind::Transform:
        return "transform";
    case TimelineLaneKind::SlotColor:
        return "slot_color";
    case TimelineLaneKind::Deform:
        return "deform";
    }
    return "transform";
}

std::optional<TimelineLaneKind> timeline_lane_kind_from_token(std::string_view token) {
    if (token == "transform") return TimelineLaneKind::Transform;
    if (token == "slot_color") return TimelineLaneKind::SlotColor;
    if (token == "deform") return TimelineLaneKind::Deform;
    return std::nullopt;
}

namespace {

/**
 * @brief The exact time a managed boundary key occupies for one duration.
 *
 * The runtime stores key times as float32, so the boundary is compared and
 * written in that precision; anything else would move on save and reload.
 */
double loop_boundary_time(double duration) {
    return static_cast<double>(static_cast<runtime::AnimationScalar>(duration));
}

/**
 * @brief The duration the boundary contract follows, pending edits included.
 *
 * A `SetDuration` edit authored earlier in the same transaction has not reached
 * the effective skeleton yet, so reading only `explicit_duration` would leave
 * the boundary at the previous duration for exactly one transaction.
 */
std::optional<double> animation_explicit_duration(
    const ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name) {
    if (const AnimationEdit* pending = pending_duration_edit(project, animation_name)) {
        return pending->duration;
    }
    const runtime::AnimationData* animation =
        effective_skeleton.find_animation(animation_name);
    if (animation == nullptr) {
        return std::nullopt;
    }
    return animation->explicit_duration;
}

std::string transform_channel_label(TransformTimelineChannel channel) {
    switch (channel) {
    case TransformTimelineChannel::Rotate:
        return "rotate";
    case TransformTimelineChannel::Translate:
        return "translate";
    case TransformTimelineChannel::Scale:
        return "scale";
    case TransformTimelineChannel::Shear:
        return "shear";
    }
    return "rotate";
}

std::string lane_label(const TransformTimelineEdit& edit) {
    return "'" + edit.bone_name + "/" + transform_channel_label(edit.channel) + "'";
}

std::string lane_label(const SlotColorTimelineEdit& edit) {
    return "slot '" + edit.slot_name + "' color";
}

std::string lane_label(const MeshDeformTimelineEdit& edit) {
    return "slot '" + edit.slot_name + "' deform '" + edit.attachment_name + "'";
}

std::string lane_selector_label(const TimelineLaneSelector& lane) {
    switch (lane.kind) {
    case TimelineLaneKind::Transform:
        return "'" + lane.bone_name + "/" +
            transform_channel_label(lane.transform_channel) + "'";
    case TimelineLaneKind::SlotColor:
        return "slot '" + lane.slot_name + "' color";
    case TimelineLaneKind::Deform:
        return "slot '" + lane.slot_name + "' deform '" + lane.attachment_name + "'";
    }
    return "'?'";
}

struct ResolvedTimelineLane {
    TimelineLaneKind kind{TimelineLaneKind::Transform};
    std::size_t timeline_index{0U};
};

std::optional<ResolvedTimelineLane> resolve_timeline_lane(
    const ProjectData& project,
    const TimelineLaneSelector& lane,
    std::string* error_out) {
    const auto fail = [&]() -> std::optional<ResolvedTimelineLane> {
        *error_out = "Persisted timeline lane not found: animation '" +
            lane.animation_name + "' timeline " + lane_selector_label(lane) + ".";
        return std::nullopt;
    };
    switch (lane.kind) {
    case TimelineLaneKind::Transform: {
        const auto index = matching_timeline_index(
            project.transform_timeline_edits,
            [&](const TransformTimelineEdit& edit) {
                return edit.animation_name == lane.animation_name &&
                    edit.bone_name == lane.bone_name &&
                    edit.channel == lane.transform_channel;
            });
        if (!index.has_value()) return fail();
        return ResolvedTimelineLane{lane.kind, *index};
    }
    case TimelineLaneKind::SlotColor: {
        const auto index = matching_timeline_index(
            project.slot_color_timeline_edits,
            [&](const SlotColorTimelineEdit& edit) {
                return edit.animation_name == lane.animation_name &&
                    edit.slot_name == lane.slot_name;
            });
        if (!index.has_value()) return fail();
        return ResolvedTimelineLane{lane.kind, *index};
    }
    case TimelineLaneKind::Deform: {
        const auto index = matching_timeline_index(
            project.mesh_deform_timeline_edits,
            [&](const MeshDeformTimelineEdit& edit) {
                return edit.animation_name == lane.animation_name &&
                    edit.slot_name == lane.slot_name &&
                    edit.attachment_name == lane.attachment_name;
            });
        if (!index.has_value()) return fail();
        return ResolvedTimelineLane{lane.kind, *index};
    }
    }
    return fail();
}

/** @brief Copies key `from`'s value components onto key `to`, per family. */
bool copy_boundary_value(TransformTimelineEdit* lane, std::size_t from, std::size_t to) {
    const TransformKeyframeEdit source = lane->keyframes[from];
    TransformKeyframeEdit& target = lane->keyframes[to];
    bool changed = false;
    if (lane->channel == TransformTimelineChannel::Rotate) {
        changed = target.angle != source.angle;
        target.angle = source.angle;
    } else {
        changed = target.x != source.x || target.y != source.y;
        target.x = source.x;
        target.y = source.y;
    }
    return changed;
}

bool copy_boundary_value(SlotColorTimelineEdit* lane, std::size_t from, std::size_t to) {
    const runtime::SlotColor source = lane->keyframes[from].color;
    SlotColorKeyframeEdit& target = lane->keyframes[to];
    const bool changed = target.color.r != source.r || target.color.g != source.g ||
        target.color.b != source.b || target.color.a != source.a;
    target.color = source;
    return changed;
}

bool copy_boundary_value(MeshDeformTimelineEdit* lane, std::size_t from, std::size_t to) {
    const std::vector<double> source = lane->keyframes[from].vertex_offsets;
    DeformKeyframeEdit& target = lane->keyframes[to];
    const bool changed = target.vertex_offsets != source;
    target.vertex_offsets = source;
    return changed;
}

/**
 * @brief Copies key `from`'s easing record onto key `to`.
 *
 * Deliberately not `set_keyframe_interpolation()`, which MAR-171 makes demote
 * every key it writes to `TimelineCurveMode::Manual`. Calling it here would
 * destroy the mirrored automatic intent on every single transaction.
 */
bool copy_boundary_easing(TransformTimelineEdit* lane, std::size_t from, std::size_t to) {
    const TransformKeyframeEdit source = lane->keyframes[from];
    TransformKeyframeEdit& target = lane->keyframes[to];
    const bool changed = !same_interpolation(target.interpolation, source.interpolation) ||
        target.curve_mode != source.curve_mode ||
        target.curve_driver != source.curve_driver;
    target.interpolation = source.interpolation;
    target.curve_mode = source.curve_mode;
    target.curve_driver = source.curve_driver;
    return changed;
}

bool copy_boundary_easing(SlotColorTimelineEdit* lane, std::size_t from, std::size_t to) {
    const SlotColorKeyframeEdit source = lane->keyframes[from];
    SlotColorKeyframeEdit& target = lane->keyframes[to];
    const bool changed = !same_interpolation(target.interpolation, source.interpolation) ||
        target.curve_mode != source.curve_mode ||
        target.curve_driver != source.curve_driver;
    target.interpolation = source.interpolation;
    target.curve_mode = source.curve_mode;
    target.curve_driver = source.curve_driver;
    return changed;
}

bool copy_boundary_easing(MeshDeformTimelineEdit* lane, std::size_t from, std::size_t to) {
    const runtime::Interpolation source = lane->keyframes[from].interpolation;
    DeformKeyframeEdit& target = lane->keyframes[to];
    const bool changed = !same_interpolation(target.interpolation, source);
    target.interpolation = source;
    return changed;
}

/** @brief Whether key `index` is still the bit-exact mirror of key 0. */
bool boundary_mirrors_first(const TransformTimelineEdit& lane, std::size_t index) {
    const TransformKeyframeEdit& first = lane.keyframes.front();
    const TransformKeyframeEdit& key = lane.keyframes[index];
    const bool value_matches = lane.channel == TransformTimelineChannel::Rotate
        ? key.angle == first.angle
        : key.x == first.x && key.y == first.y;
    return value_matches &&
        same_interpolation(key.interpolation, first.interpolation) &&
        key.curve_mode == first.curve_mode && key.curve_driver == first.curve_driver;
}

bool boundary_mirrors_first(const SlotColorTimelineEdit& lane, std::size_t index) {
    const SlotColorKeyframeEdit& first = lane.keyframes.front();
    const SlotColorKeyframeEdit& key = lane.keyframes[index];
    return key.color.r == first.color.r && key.color.g == first.color.g &&
        key.color.b == first.color.b && key.color.a == first.color.a &&
        same_interpolation(key.interpolation, first.interpolation) &&
        key.curve_mode == first.curve_mode && key.curve_driver == first.curve_driver;
}

bool boundary_mirrors_first(const MeshDeformTimelineEdit& lane, std::size_t index) {
    const DeformKeyframeEdit& first = lane.keyframes.front();
    const DeformKeyframeEdit& key = lane.keyframes[index];
    return key.vertex_offsets == first.vertex_offsets &&
        same_interpolation(key.interpolation, first.interpolation);
}

/**
 * @brief The index of the key the contract owns on an opted-in lane, or none.
 *
 * The managed boundary is derived, never stored. Clause 4 makes it the lane's
 * last key, but only when that key satisfies one half of the contract: clause 1
 * (it already sits at the boundary) or clauses 2 and 3 (it is still the
 * bit-exact mirror of key 0 a previous synchronization wrote, so a duration
 * change is moving it rather than stranding it).
 *
 * A last key that satisfies neither is authored data no synchronization
 * produced -- a hand-edited document, or a lane whose boundary key was removed
 * behind the GUI and Agent guards. Promoting it would move it to the duration
 * and overwrite its value from key 0, silently destroying it, so the boundary is
 * created beside it instead.
 */
template <typename Timeline>
std::optional<std::size_t> managed_boundary_index(
    const Timeline& lane,
    double boundary_time) {
    const std::size_t count = lane.keyframes.size();
    if (!lane.loop_sync || count < 2U) {
        return std::nullopt;
    }
    const std::size_t last = count - 1U;
    if (std::abs(lane.keyframes[last].time - boundary_time) <= kKeyTimeEpsilon ||
        boundary_mirrors_first(lane, last)) {
        return last;
    }
    return std::nullopt;
}

/**
 * @brief The boundary contract's structural checks over one lane's other keys.
 *
 * `managed` names the key the contract already owns. On an opted-in lane that
 * is its last key, because clause 4 put it there at the previous duration, so a
 * duration change moves it rather than stranding it past the new boundary.
 */
template <typename Timeline>
bool check_boundary_neighbours(
    const Timeline& lane,
    double boundary_time,
    std::optional<std::size_t> managed,
    std::string_view animation_name,
    const std::string& label,
    std::string* error_out) {
    const auto prefix = [&] {
        return "Animation '" + std::string(animation_name) + "' timeline " + label;
    };
    for (std::size_t index = 0U; index < lane.keyframes.size(); ++index) {
        if (managed.has_value() && index == *managed) {
            continue;
        }
        const double time = lane.keyframes[index].time;
        if (time > boundary_time + kKeyTimeEpsilon) {
            *error_out = prefix() + " is loop synchronized and cannot hold a key at " +
                std::to_string(time) + " seconds past its boundary at " +
                std::to_string(boundary_time) +
                " seconds; lengthen the duration or disable loop synchronization.";
            return false;
        }
        if (std::abs(time - boundary_time) <= kKeyTimeEpsilon) {
            *error_out = prefix() + " already holds a key at its loop boundary of " +
                std::to_string(boundary_time) +
                " seconds; move or remove it, or disable loop synchronization.";
            return false;
        }
        if (time > boundary_time - kNonEventKeySpacing) {
            *error_out = prefix() + " keeps a key at " + std::to_string(time) +
                " seconds within one millisecond of its loop boundary at " +
                std::to_string(boundary_time) +
                " seconds; move that key or disable loop synchronization.";
            return false;
        }
    }
    return true;
}

/**
 * @brief Phase 1 for one lane: the boundary key's existence, time, and value.
 *
 * It changes the key set MAR-171's resolver reads, which is why the resolver
 * runs after it and the easing mirror runs after the resolver.
 */
template <typename Timeline>
bool synchronize_lane_structure(
    Timeline* lane,
    double boundary_time,
    std::string_view animation_name,
    TimelineLoopBoundaryAction* action_out,
    std::size_t* created_key_count,
    std::size_t* moved_key_count,
    std::size_t* rewritten_key_count,
    bool* changed_out,
    std::string* error_out) {
    const std::string label = lane_label(*lane);
    if (lane->keyframes.empty() ||
        std::abs(lane->keyframes.front().time) > kKeyTimeEpsilon) {
        *error_out = "Animation '" + std::string(animation_name) + "' timeline " +
            label +
            " is loop synchronized and requires a key at time zero; disable loop "
            "synchronization before removing it.";
        return false;
    }

    const std::optional<std::size_t> managed =
        managed_boundary_index(*lane, boundary_time);
    if (!check_boundary_neighbours(
            *lane, boundary_time, managed, animation_name, label, error_out)) {
        return false;
    }

    if (!managed.has_value()) {
        auto key = lane->keyframes.front();
        key.time = boundary_time;
        lane->keyframes.push_back(std::move(key));
        ++*created_key_count;
        *action_out = TimelineLoopBoundaryAction::Created;
        *changed_out = true;
        return true;
    }

    const std::size_t index = *managed;
    const bool moved =
        std::abs(lane->keyframes[index].time - boundary_time) > kKeyTimeEpsilon;
    lane->keyframes[index].time = boundary_time;
    const bool rewritten = copy_boundary_value(lane, 0U, index);
    if (moved) {
        ++*moved_key_count;
        *action_out = TimelineLoopBoundaryAction::Moved;
    } else if (rewritten) {
        ++*rewritten_key_count;
        *action_out = TimelineLoopBoundaryAction::Rewritten;
    } else {
        *action_out = TimelineLoopBoundaryAction::Unchanged;
    }
    *changed_out = moved || rewritten;
    return true;
}

/** @brief Phase 2 for one lane: the easing mirror, after the resolver ran. */
template <typename Timeline>
bool mirror_lane_boundary_easing(Timeline* lane) {
    if (lane->keyframes.size() < 2U) {
        return false;
    }
    return copy_boundary_easing(lane, 0U, lane->keyframes.size() - 1U);
}

/** @brief Whether any lane of `animation_name` in `project` is opted in. */
bool project_has_opted_in_lane(
    const ProjectData& project,
    std::string_view animation_name) {
    const auto scan = [&](const auto& edits) {
        return std::any_of(edits.begin(), edits.end(), [&](const auto& edit) {
            return edit.loop_sync &&
                (animation_name.empty() || edit.animation_name == animation_name);
        });
    };
    return scan(project.transform_timeline_edits) ||
        scan(project.slot_color_timeline_edits) ||
        scan(project.mesh_deform_timeline_edits);
}

} // namespace

bool timeline_key_is_managed_loop_boundary(
    const ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    const TimelineKeySelector& selector) {
    std::string ignored;
    const auto key = resolve_timeline_key(project, selector, &ignored);
    if (!key.has_value()) {
        return false;
    }
    // Only the three continuous families carry the flag at all; the discrete
    // three have no member to read, which is the compile-enforced exclusion.
    const auto boundary_of = [&](const auto& lanes) {
        const auto& lane = lanes[key->timeline_index];
        if (!lane.loop_sync) return false;
        const auto duration = animation_explicit_duration(
            project, effective_skeleton, lane.animation_name);
        if (!duration.has_value()) return false;
        const auto managed =
            managed_boundary_index(lane, loop_boundary_time(*duration));
        return managed.has_value() && *managed == key->key_index;
    };
    switch (key->kind) {
    case TimelineKeyKind::Transform:
        return boundary_of(project.transform_timeline_edits);
    case TimelineKeyKind::SlotColor:
        return boundary_of(project.slot_color_timeline_edits);
    case TimelineKeyKind::Deform:
        return boundary_of(project.mesh_deform_timeline_edits);
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::Event:
    case TimelineKeyKind::SlotAttachment:
        return false;
    }
    return false;
}

double inferred_duration_excluding_loop_boundaries(
    const ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    const runtime::AnimationData& animation) {
    // Not an optimization: this is the guarantee that MAR-172 cannot change
    // duration validation for any project that does not use it.
    if (!project_has_opted_in_lane(project, animation.name)) {
        return animation.inferred_duration();
    }

    const auto bone_name = [&](std::size_t index) -> std::string_view {
        return index < effective_skeleton.bones().size()
            ? std::string_view(effective_skeleton.bones()[index].name)
            : std::string_view{};
    };
    const auto slot_name = [&](std::size_t index) -> std::string_view {
        return index < effective_skeleton.slots().size()
            ? std::string_view(effective_skeleton.slots()[index].name)
            : std::string_view{};
    };
    const auto owned_transform = [&](std::size_t index, TransformTimelineChannel channel) {
        const std::string_view name = bone_name(index);
        return std::any_of(
            project.transform_timeline_edits.begin(),
            project.transform_timeline_edits.end(),
            [&](const TransformTimelineEdit& edit) {
                return edit.loop_sync && edit.animation_name == animation.name &&
                    edit.bone_name == name && edit.channel == channel;
            });
    };

    double floor = 0.0;
    const auto include_last = [&](const auto& timeline) {
        if (!timeline.keyframes.empty()) {
            floor = std::max(floor, static_cast<double>(timeline.keyframes.back().time));
        }
    };
    for (const auto& timeline : animation.bone_rotate_timelines) {
        if (!owned_transform(timeline.bone_index, TransformTimelineChannel::Rotate)) {
            include_last(timeline);
        }
    }
    for (const auto& timeline : animation.bone_translate_timelines) {
        if (!owned_transform(timeline.bone_index, TransformTimelineChannel::Translate)) {
            include_last(timeline);
        }
    }
    for (const auto& timeline : animation.bone_scale_timelines) {
        if (!owned_transform(timeline.bone_index, TransformTimelineChannel::Scale)) {
            include_last(timeline);
        }
    }
    for (const auto& timeline : animation.bone_shear_timelines) {
        if (!owned_transform(timeline.bone_index, TransformTimelineChannel::Shear)) {
            include_last(timeline);
        }
    }
    for (const auto& timeline : animation.bone_inherit_timelines) {
        include_last(timeline);
    }
    for (const auto& timeline : animation.slot_color_timelines) {
        const std::string_view name = slot_name(timeline.slot_index);
        const bool owned = std::any_of(
            project.slot_color_timeline_edits.begin(),
            project.slot_color_timeline_edits.end(),
            [&](const SlotColorTimelineEdit& edit) {
                return edit.loop_sync && edit.animation_name == animation.name &&
                    edit.slot_name == name;
            });
        if (!owned) include_last(timeline);
    }
    for (const auto& timeline : animation.mesh_deform_timelines) {
        const std::string_view name = slot_name(timeline.slot_index);
        const bool owned = std::any_of(
            project.mesh_deform_timeline_edits.begin(),
            project.mesh_deform_timeline_edits.end(),
            [&](const MeshDeformTimelineEdit& edit) {
                return edit.loop_sync && edit.animation_name == animation.name &&
                    edit.slot_name == name &&
                    edit.attachment_name == timeline.attachment_name;
            });
        if (!owned) include_last(timeline);
    }
    // The discrete families can never be owned by an opted-in lane.
    for (const auto& timeline : animation.slot_attachment_timelines) {
        include_last(timeline);
    }
    if (animation.draw_order_timeline_data.has_value()) {
        include_last(*animation.draw_order_timeline_data);
    }
    if (animation.event_timeline_data.has_value()) {
        include_last(*animation.event_timeline_data);
    }

    // Every opted-in lane folds back in at its second-to-last key, which is the
    // last time the animator actually authored on it.
    const auto fold_opted_in = [&](const auto& edits) {
        for (const auto& edit : edits) {
            if (!edit.loop_sync || edit.animation_name != animation.name) continue;
            const std::size_t count = edit.keyframes.size();
            if (count >= 2U) {
                floor = std::max(floor, edit.keyframes[count - 2U].time);
            }
        }
    };
    fold_opted_in(project.transform_timeline_edits);
    fold_opted_in(project.slot_color_timeline_edits);
    fold_opted_in(project.mesh_deform_timeline_edits);
    return floor;
}

TimelineLoopSyncResult synchronize_loop_boundaries(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name) {
    if (project == nullptr) {
        return {{false, "Timeline authoring requires an open project."}};
    }
    // Step 0. A project with no opted-in lane in scope does nothing at all,
    // including no automatic-curve resolution, so its behaviour is identical to
    // a build that has never heard of MAR-172.
    if (!project_has_opted_in_lane(*project, animation_name)) {
        return {};
    }

    TimelineLoopSyncResult result;
    ProjectData candidate = *project;
    bool changed = false;
    std::string error;

    const auto in_scope = [&](const std::string& name) {
        return animation_name.empty() || name == animation_name;
    };

    // PHASE 1 -- structure and value.
    const auto phase_one = [&](auto& edits) {
        for (auto& edit : edits) {
            if (!edit.loop_sync || !in_scope(edit.animation_name)) continue;
            ++result.lane_count;
            const auto duration = animation_explicit_duration(
                candidate, effective_skeleton, edit.animation_name);
            if (!duration.has_value()) {
                error = "Animation '" + edit.animation_name + "' timeline " +
                    lane_label(edit) +
                    " is loop synchronized but the animation has no explicit duration; "
                    "author one or disable loop synchronization.";
                return false;
            }
            const double boundary = loop_boundary_time(*duration);
            if (!(boundary >= kNonEventKeySpacing)) {
                error = "Animation '" + edit.animation_name + "' timeline " +
                    lane_label(edit) + " is loop synchronized but the duration of " +
                    std::to_string(boundary) +
                    " seconds leaves no room for a key at time zero and a boundary "
                    "key one millisecond apart.";
                return false;
            }
            TimelineLoopBoundaryAction action = TimelineLoopBoundaryAction::Unchanged;
            bool lane_changed = false;
            if (!synchronize_lane_structure(
                    &edit,
                    boundary,
                    edit.animation_name,
                    &action,
                    &result.created_key_count,
                    &result.moved_key_count,
                    &result.rewritten_key_count,
                    &lane_changed,
                    &error)) {
                return false;
            }
            if (lane_changed) {
                ++result.synchronized_lane_count;
                changed = true;
            }
        }
        return true;
    };
    if (!phase_one(candidate.transform_timeline_edits) ||
        !phase_one(candidate.slot_color_timeline_edits) ||
        !phase_one(candidate.mesh_deform_timeline_edits)) {
        return {{false, std::move(error)}};
    }

    // Step 3. MAR-171's resolver, between the phases: phase 1 gave the
    // previously-last key a real outgoing segment, and phase 2 mirrors an
    // easing this may have just rewritten.
    const TimelineAutoCurveResult resolved = resolve_automatic_curves(&candidate, {});
    if (!resolved) {
        return {{false, resolved.error}};
    }
    result.resolved_key_count = resolved.resolved_key_count;
    changed = changed || resolved.changed;

    // PHASE 2 -- the easing mirror. The resolver never writes a lane's last
    // key, so this cannot invalidate step 3 and one pass of each is exact.
    const auto phase_two = [&](auto& edits) {
        for (auto& edit : edits) {
            if (!edit.loop_sync || !in_scope(edit.animation_name)) continue;
            if (mirror_lane_boundary_easing(&edit)) {
                changed = true;
            }
        }
    };
    phase_two(candidate.transform_timeline_edits);
    phase_two(candidate.slot_color_timeline_edits);
    phase_two(candidate.mesh_deform_timeline_edits);

    if (!changed) {
        return result;
    }
    *project = std::move(candidate);
    result.changed = true;
    return result;
}

TimelineLoopSyncResult set_timeline_loop_sync(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    const std::vector<TimelineLaneSelector>& lanes,
    bool enabled) {
    if (project == nullptr) {
        return {{false, "Timeline authoring requires an open project."}};
    }
    if (lanes.empty()) {
        return {{false, "Loop synchronization requires at least one timeline lane."}};
    }

    ProjectData candidate = *project;
    TimelineLoopSyncResult result;
    result.lane_count = lanes.size();
    result.lane_actions.assign(lanes.size(), TimelineLoopBoundaryAction::Unchanged);

    std::vector<ResolvedTimelineLane> resolved;
    resolved.reserve(lanes.size());
    for (const TimelineLaneSelector& lane : lanes) {
        std::string error;
        const auto found = resolve_timeline_lane(candidate, lane, &error);
        if (!found.has_value()) {
            return {{false, std::move(error)}};
        }
        for (const ResolvedTimelineLane& existing : resolved) {
            if (existing.kind == found->kind &&
                existing.timeline_index == found->timeline_index) {
                return {{false,
                         "Loop synchronization selectors must name distinct timeline "
                         "lanes: " + lane_selector_label(lane) + " appears twice."}};
            }
        }
        resolved.push_back(*found);
    }

    std::vector<std::string> animations;
    std::size_t changed_lane_count = 0U;
    bool mutated = false;

    // Enabling evaluates every prerequisite of the contract before it writes
    // anything; disabling deliberately evaluates none, so a project that has
    // reached an unsatisfiable state always has an escape.
    for (std::size_t index = 0U; index < lanes.size(); ++index) {
        const TimelineLaneSelector& lane = lanes[index];
        const ResolvedTimelineLane& target = resolved[index];
        if (std::find(animations.begin(), animations.end(), lane.animation_name) ==
            animations.end()) {
            animations.push_back(lane.animation_name);
        }

        const auto apply = [&](auto& edits) {
            auto& edit = edits[target.timeline_index];
            if (!enabled) {
                if (edit.loop_sync) {
                    edit.loop_sync = false;
                    ++changed_lane_count;
                    mutated = true;
                }
                result.lane_actions[index] = TimelineLoopBoundaryAction::Released;
                return std::string{};
            }

            const auto duration = animation_explicit_duration(
                candidate, effective_skeleton, edit.animation_name);
            const std::string prefix =
                "Animation '" + edit.animation_name + "' timeline " + lane_label(edit);
            if (!duration.has_value()) {
                return prefix +
                    " cannot be loop synchronized without an explicit animation "
                    "duration; author one first.";
            }
            const double boundary = loop_boundary_time(*duration);
            if (!(boundary >= kNonEventKeySpacing)) {
                return prefix + " cannot be loop synchronized at a duration of " +
                    std::to_string(boundary) +
                    " seconds; the boundary key needs one millisecond of room after "
                    "the key at time zero.";
            }
            if (edit.keyframes.empty() ||
                std::abs(edit.keyframes.front().time) > kKeyTimeEpsilon) {
                return prefix +
                    " cannot be loop synchronized without a key at time zero; author "
                    "one first.";
            }

            std::optional<std::size_t> at_boundary;
            std::size_t boundary_key_count = 0U;
            for (std::size_t key = 0U; key < edit.keyframes.size(); ++key) {
                if (std::abs(edit.keyframes[key].time - boundary) <= kKeyTimeEpsilon) {
                    ++boundary_key_count;
                    if (!at_boundary.has_value()) at_boundary = key;
                }
            }
            if (boundary_key_count > 1U) {
                return prefix + " holds " + std::to_string(boundary_key_count) +
                    " keys at its loop boundary of " + std::to_string(boundary) +
                    " seconds; remove the duplicates first.";
            }
            std::string error;
            if (!check_boundary_neighbours(
                    edit, boundary, at_boundary, edit.animation_name, lane_label(edit),
                    &error)) {
                return error;
            }

            const bool flag_changed = !edit.loop_sync;
            edit.loop_sync = true;
            if (flag_changed) {
                ++changed_lane_count;
                mutated = true;
            }
            if (at_boundary.has_value()) {
                // Adoption overwrites an authored key's value: the animator
                // asked for the last key to become the loop boundary, and the
                // boundary's value is defined by the contract.
                const bool value_changed = copy_boundary_value(&edit, 0U, *at_boundary);
                const bool easing_changed = copy_boundary_easing(&edit, 0U, *at_boundary);
                if (value_changed || easing_changed) {
                    ++result.rewritten_key_count;
                    mutated = true;
                }
                if (flag_changed || value_changed || easing_changed) {
                    result.lane_actions[index] = TimelineLoopBoundaryAction::Adopted;
                }
            } else {
                auto key = edit.keyframes.front();
                key.time = boundary;
                edit.keyframes.push_back(std::move(key));
                ++result.created_key_count;
                result.lane_actions[index] = TimelineLoopBoundaryAction::Created;
                mutated = true;
            }
            return std::string{};
        };

        std::string error;
        switch (target.kind) {
        case TimelineLaneKind::Transform:
            error = apply(candidate.transform_timeline_edits);
            break;
        case TimelineLaneKind::SlotColor:
            error = apply(candidate.slot_color_timeline_edits);
            break;
        case TimelineLaneKind::Deform:
            error = apply(candidate.mesh_deform_timeline_edits);
            break;
        }
        if (!error.empty()) {
            return {{false, std::move(error)}};
        }
    }

    // The operation's own dry run must report the resulting boundary key, not a
    // promise that the session seam will produce one later.
    for (const std::string& animation : animations) {
        const TimelineLoopSyncResult synchronized =
            synchronize_loop_boundaries(&candidate, effective_skeleton, animation);
        if (!synchronized) {
            return {{false, synchronized.error}};
        }
        result.synchronized_lane_count += synchronized.synchronized_lane_count;
        result.created_key_count += synchronized.created_key_count;
        result.moved_key_count += synchronized.moved_key_count;
        result.rewritten_key_count += synchronized.rewritten_key_count;
        result.resolved_key_count += synchronized.resolved_key_count;
        mutated = mutated || synchronized.changed;
    }

    result.changed_lane_count = changed_lane_count;
    if (!mutated) {
        return result;
    }
    *project = std::move(candidate);
    result.changed = true;
    return result;
}

// ── Mesh vertex weights ────────────────────────────────────────────────────

namespace {

MeshWeightResult mesh_weight_failure(std::string message) {
    MeshWeightResult result;
    result.error = std::move(message);
    return result;
}

/// Materializes the attachment's weight overlay on first mutation. Deliberately
/// not canonicalized: opening a project must not silently rewrite weights the
/// user never touched.
MeshWeightAttachmentEdit& ensure_weight_edit(
    ProjectData& project,
    const runtime::SkeletonData& skeleton,
    const MeshWeightTarget& target,
    const runtime::AttachmentData& attachment) {
    if (auto* existing = project.find_mesh_weight_attachment_edit(
            target.skin_name, target.slot_name, target.attachment_name)) {
        return *existing;
    }
    project.mesh_weight_attachment_edits.push_back(
        mesh_weight_model::mesh_weight_edit_from_runtime(
            skeleton,
            target.skin_name,
            target.slot_name,
            target.attachment_name,
            attachment));
    return project.mesh_weight_attachment_edits.back();
}

bool weight_vertices_equal(
    const MeshWeightVertexEdit& left,
    const MeshWeightVertexEdit& right) {
    if (left.influences.size() != right.influences.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.influences.size(); ++index) {
        const MeshWeightInfluenceEdit& lhs = left.influences[index];
        const MeshWeightInfluenceEdit& rhs = right.influences[index];
        if (lhs.bone_name != rhs.bone_name || lhs.x != rhs.x || lhs.y != rhs.y ||
            lhs.weight != rhs.weight) {
            return false;
        }
    }
    return true;
}

/// Validates an explicit vertex scope against the materialized edit. An empty
/// scope means every vertex, which is the shipped `normalize_weights` behaviour.
std::string resolve_weight_scope(
    const std::vector<std::size_t>& scope,
    std::size_t vertex_count,
    std::vector<std::size_t>* resolved_out) {
    resolved_out->clear();
    if (scope.empty()) {
        resolved_out->reserve(vertex_count);
        for (std::size_t index = 0; index < vertex_count; ++index) {
            resolved_out->push_back(index);
        }
        return {};
    }
    for (const std::size_t index : scope) {
        if (index >= vertex_count) {
            return "vertex index is outside the target mesh.";
        }
        if (std::find(resolved_out->begin(), resolved_out->end(), index) !=
            resolved_out->end()) {
            return "A vertex was selected more than once.";
        }
        resolved_out->push_back(index);
    }
    std::sort(resolved_out->begin(), resolved_out->end());
    return {};
}

} // namespace

MeshWeightResult set_mesh_vertex_weights(
    ProjectData* project,
    const runtime::SkeletonData& skeleton,
    const runtime::AttachmentData& attachment,
    const MeshWeightTarget& target,
    const std::vector<std::pair<std::size_t, MeshWeightVertexEdit>>& vertices) {
    if (project == nullptr) {
        return mesh_weight_failure("Mesh weight authoring requires an open project.");
    }
    if (vertices.empty()) {
        return mesh_weight_failure("set_vertex_weights requires at least one vertex.");
    }

    ProjectData candidate = *project;
    MeshWeightAttachmentEdit& edit =
        ensure_weight_edit(candidate, skeleton, target, attachment);

    MeshWeightResult result;
    result.vertex_count = edit.vertices.size();

    // Preflight: validate and canonicalize every entry before writing any of
    // them, so a rejection on the last entry cannot leave the earlier ones
    // applied.
    std::vector<std::size_t> seen;
    std::vector<std::pair<std::size_t, MeshWeightVertexEdit>> staged;
    staged.reserve(vertices.size());
    for (const auto& [vertex_index, requested] : vertices) {
        if (vertex_index >= edit.vertices.size()) {
            return mesh_weight_failure("vertex index is outside the target mesh.");
        }
        if (std::find(seen.begin(), seen.end(), vertex_index) != seen.end()) {
            return mesh_weight_failure("A vertex was selected more than once.");
        }
        seen.push_back(vertex_index);
        MeshWeightVertexEdit canonical = requested;
        if (const std::string error =
                mesh_weight_model::canonicalize_mesh_weight_vertex(skeleton, &canonical);
            !error.empty()) {
            return mesh_weight_failure(error);
        }
        staged.emplace_back(vertex_index, std::move(canonical));
    }

    result.scoped_vertex_count = staged.size();
    for (auto& [vertex_index, canonical] : staged) {
        if (!weight_vertices_equal(edit.vertices[vertex_index], canonical)) {
            result.affected_vertices.push_back(vertex_index);
            edit.vertices[vertex_index] = std::move(canonical);
        }
    }
    std::sort(result.affected_vertices.begin(), result.affected_vertices.end());
    result.changed = !result.affected_vertices.empty();
    if (result.changed) {
        *project = std::move(candidate);
    }
    return result;
}

MeshWeightResult normalize_mesh_weights(
    ProjectData* project,
    const runtime::SkeletonData& skeleton,
    const runtime::AttachmentData& attachment,
    const MeshWeightTarget& target,
    const std::vector<std::size_t>& scope) {
    if (project == nullptr) {
        return mesh_weight_failure("Mesh weight authoring requires an open project.");
    }

    ProjectData candidate = *project;
    MeshWeightAttachmentEdit& edit =
        ensure_weight_edit(candidate, skeleton, target, attachment);

    MeshWeightResult result;
    result.vertex_count = edit.vertices.size();
    std::vector<std::size_t> resolved;
    if (const std::string error =
            resolve_weight_scope(scope, edit.vertices.size(), &resolved);
        !error.empty()) {
        return mesh_weight_failure(error);
    }
    result.scoped_vertex_count = resolved.size();

    std::vector<std::pair<std::size_t, MeshWeightVertexEdit>> staged;
    staged.reserve(resolved.size());
    for (const std::size_t vertex_index : resolved) {
        MeshWeightVertexEdit canonical = edit.vertices[vertex_index];
        if (const std::string error =
                mesh_weight_model::canonicalize_mesh_weight_vertex(skeleton, &canonical);
            !error.empty()) {
            return mesh_weight_failure(error);
        }
        staged.emplace_back(vertex_index, std::move(canonical));
    }

    for (auto& [vertex_index, canonical] : staged) {
        if (!weight_vertices_equal(edit.vertices[vertex_index], canonical)) {
            result.affected_vertices.push_back(vertex_index);
            edit.vertices[vertex_index] = std::move(canonical);
        }
    }
    result.changed = !result.affected_vertices.empty();
    // Materialization alone is a change worth keeping only when a vertex moved;
    // otherwise the overlay the candidate grew is discarded with the candidate.
    if (result.changed) {
        *project = std::move(candidate);
    }
    return result;
}

MeshWeightResult rebind_mesh_weights(
    ProjectData* project,
    const runtime::SkeletonData& skeleton,
    const runtime::AttachmentData& attachment,
    const MeshWeightTarget& target,
    const std::vector<std::size_t>& scope) {
    if (project == nullptr) {
        return mesh_weight_failure("Mesh weight authoring requires an open project.");
    }

    const std::vector<runtime::BoneWorldTransform> setup_transforms =
        mesh_weight_model::setup_pose_bone_world_transforms(skeleton);
    if (setup_transforms.empty()) {
        return mesh_weight_failure("The skeleton has no bones to rebind against.");
    }

    ProjectData candidate = *project;
    MeshWeightAttachmentEdit& edit =
        ensure_weight_edit(candidate, skeleton, target, attachment);

    MeshWeightResult result;
    result.vertex_count = edit.vertices.size();
    std::vector<std::size_t> resolved;
    if (const std::string error =
            resolve_weight_scope(scope, edit.vertices.size(), &resolved);
        !error.empty()) {
        return mesh_weight_failure(error);
    }
    result.scoped_vertex_count = resolved.size();

    std::vector<std::pair<std::size_t, MeshWeightVertexEdit>> staged;
    staged.reserve(resolved.size());
    for (const std::size_t vertex_index : resolved) {
        MeshWeightVertexEdit rebound = edit.vertices[vertex_index];
        if (const std::string error = mesh_weight_model::rebind_mesh_weight_vertex(
                skeleton, setup_transforms, &rebound);
            !error.empty()) {
            return mesh_weight_failure(error);
        }
        staged.emplace_back(vertex_index, std::move(rebound));
    }

    for (auto& [vertex_index, rebound] : staged) {
        if (!weight_vertices_equal(edit.vertices[vertex_index], rebound)) {
            result.affected_vertices.push_back(vertex_index);
            edit.vertices[vertex_index] = std::move(rebound);
        }
    }
    result.changed = !result.affected_vertices.empty();
    if (result.changed) {
        *project = std::move(candidate);
    }
    return result;
}

MeshWeightResult generate_mesh_weights(
    ProjectData* project,
    const runtime::SkeletonData& skeleton,
    const runtime::AttachmentData& attachment,
    const MeshWeightTarget& target,
    const std::vector<std::string>& candidate_bone_names,
    const std::vector<std::size_t>& scope) {
    if (project == nullptr) {
        return mesh_weight_failure("Mesh weight authoring requires an open project.");
    }

    const std::vector<runtime::BoneWorldTransform> setup_transforms =
        mesh_weight_model::setup_pose_bone_world_transforms(skeleton);
    if (setup_transforms.empty()) {
        return mesh_weight_failure("The skeleton has no bones to generate weights against.");
    }

    // Candidate validation runs once per call and BEFORE anything is copied or
    // staged. A singular candidate is fatal for the whole call rather than
    // silently excluded: dropping a bone the caller explicitly named is the
    // mirror image of the silent expansion the story forbids, and a per-vertex
    // exclusion would make the outcome depend on which vertices were in scope.
    if (candidate_bone_names.empty()) {
        return mesh_weight_failure("mesh.generate_weights requires at least one candidate bone.");
    }
    std::vector<std::size_t> candidate_indices;
    candidate_indices.reserve(candidate_bone_names.size());
    for (const std::string& bone_name : candidate_bone_names) {
        const auto bone_index = skeleton.find_bone_index(bone_name);
        if (!bone_index.has_value()) {
            return mesh_weight_failure("Bone not found: " + bone_name);
        }
        if (*bone_index >= setup_transforms.size()) {
            return mesh_weight_failure("Bone '" + bone_name + "' is outside the setup pose.");
        }
        if (std::find(candidate_indices.begin(), candidate_indices.end(), *bone_index) !=
            candidate_indices.end()) {
            return mesh_weight_failure("A candidate bone was listed more than once.");
        }
        if (!mesh_weight_model::inverse_transform_point_safe(
                 setup_transforms[*bone_index], 0.0, 0.0)
                 .has_value()) {
            return mesh_weight_failure(
                "Bone '" + bone_name +
                "' has a singular setup transform and cannot be used as a weight candidate.");
        }
        candidate_indices.push_back(*bone_index);
    }
    // Sorting here, rather than preserving the caller's order, is what makes
    // "the result does not depend on the order the candidates were listed in"
    // true by construction instead of by accident of the later distance sort.
    std::sort(candidate_indices.begin(), candidate_indices.end());

    // Built once per call, never per vertex.
    const std::vector<mesh_weight_model::BoneSetupSegment> segments =
        mesh_weight_model::bone_setup_segments(skeleton, setup_transforms);

    ProjectData candidate = *project;
    MeshWeightAttachmentEdit& edit =
        ensure_weight_edit(candidate, skeleton, target, attachment);
    if (edit.vertices.empty()) {
        return mesh_weight_failure("mesh.generate_weights requires a weighted mesh attachment.");
    }

    MeshWeightResult result;
    result.vertex_count = edit.vertices.size();
    std::vector<std::size_t> resolved;
    if (const std::string error =
            resolve_weight_scope(scope, edit.vertices.size(), &resolved);
        !error.empty()) {
        return mesh_weight_failure(error);
    }
    result.scoped_vertex_count = resolved.size();

    std::vector<std::pair<std::size_t, MeshWeightVertexEdit>> staged;
    staged.reserve(resolved.size());
    for (const std::size_t vertex_index : resolved) {
        MeshWeightVertexEdit generated = edit.vertices[vertex_index];
        if (const std::string error = mesh_weight_model::generate_mesh_weight_vertex(
                skeleton, setup_transforms, segments, candidate_indices, &generated);
            !error.empty()) {
            return mesh_weight_failure(error);
        }
        staged.emplace_back(vertex_index, std::move(generated));
    }

    for (auto& [vertex_index, generated] : staged) {
        if (!weight_vertices_equal(edit.vertices[vertex_index], generated)) {
            result.affected_vertices.push_back(vertex_index);
            edit.vertices[vertex_index] = std::move(generated);
        }
    }
    result.changed = !result.affected_vertices.empty();
    if (result.changed) {
        *project = std::move(candidate);
    }
    return result;
}

} // namespace marrow::editor
