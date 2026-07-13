#include "marrow/editor/authoring.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

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

template <typename Keyframe>
void include_retime_bounds(
    const std::vector<Keyframe>& keyframes,
    const ResolvedTimelineKey& resolved,
    const std::set<std::size_t>& selected_indices,
    double spacing,
    double* minimum_delta,
    double* maximum_delta) {
    *minimum_delta = std::max(*minimum_delta, -resolved.original_time);
    for (std::size_t index = resolved.key_index; index > 0U; --index) {
        const std::size_t neighbor = index - 1U;
        if (selected_indices.find(neighbor) != selected_indices.end()) continue;
        *minimum_delta = std::max(
            *minimum_delta,
            keyframes[neighbor].time + spacing - resolved.original_time);
        break;
    }
    for (std::size_t neighbor = resolved.key_index + 1U;
         neighbor < keyframes.size();
         ++neighbor) {
        if (selected_indices.find(neighbor) != selected_indices.end()) continue;
        *maximum_delta = std::min(
            *maximum_delta,
            keyframes[neighbor].time - spacing - resolved.original_time);
        break;
    }
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
            kNonEventKeySpacing,
            minimum_delta,
            maximum_delta);
        return;
    case TimelineKeyKind::Deform:
        include_timeline_retime_bounds(
            project.mesh_deform_timeline_edits,
            resolved,
            all_resolved,
            kNonEventKeySpacing,
            minimum_delta,
            maximum_delta);
        return;
    case TimelineKeyKind::DrawOrder:
        include_timeline_retime_bounds(
            project.draw_order_timeline_edits,
            resolved,
            all_resolved,
            kNonEventKeySpacing,
            minimum_delta,
            maximum_delta);
        return;
    case TimelineKeyKind::Event:
        include_timeline_retime_bounds(
            project.event_timeline_edits,
            resolved,
            all_resolved,
            0.0,
            minimum_delta,
            maximum_delta);
        return;
    case TimelineKeyKind::SlotColor:
        include_timeline_retime_bounds(
            project.slot_color_timeline_edits,
            resolved,
            all_resolved,
            kNonEventKeySpacing,
            minimum_delta,
            maximum_delta);
        return;
    case TimelineKeyKind::SlotAttachment:
        include_timeline_retime_bounds(
            project.slot_attachment_timeline_edits,
            resolved,
            all_resolved,
            kNonEventKeySpacing,
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

} // namespace

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
    if (snap_to_frames) {
        const auto earliest = std::min_element(
            resolved.begin(), resolved.end(), [](const auto& left, const auto& right) {
                return left.original_time < right.original_time;
            });
        const double frame_seconds = 1.0 / frames_per_second;
        applied_delta =
            std::round((earliest->original_time + applied_delta) / frame_seconds) *
                frame_seconds -
            earliest->original_time;
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
    applied_delta = std::clamp(applied_delta, minimum_delta, maximum_delta);
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

} // namespace marrow::editor
