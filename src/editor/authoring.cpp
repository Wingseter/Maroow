#include "marrow/editor/authoring.hpp"

#include <algorithm>
#include <array>
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

AnimationEdit* coalescible_duration_edit(
    ProjectData* project,
    std::string_view animation_name) {
    for (auto iterator = project->animation_edits.rbegin();
         iterator != project->animation_edits.rend();
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

template <typename TimelineEdit>
void include_animation_timeline_maximum(
    const std::vector<TimelineEdit>& edits,
    std::string_view animation_name,
    double* maximum_time) {
    for (const TimelineEdit& edit : edits) {
        if (edit.animation_name != animation_name) {
            continue;
        }
        for (const auto& keyframe : edit.keyframes) {
            *maximum_time = std::max(*maximum_time, keyframe.time);
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

    double inferred_duration = animation->inferred_duration();
    include_animation_timeline_maximum(
        project->transform_timeline_edits, animation_name, &inferred_duration);
    include_animation_timeline_maximum(
        project->mesh_deform_timeline_edits, animation_name, &inferred_duration);
    include_animation_timeline_maximum(
        project->draw_order_timeline_edits, animation_name, &inferred_duration);
    include_animation_timeline_maximum(
        project->event_timeline_edits, animation_name, &inferred_duration);
    include_animation_timeline_maximum(
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
        animation->inferred_duration(),
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
        double maximum_time = authored_boundary;
        include_animation_timeline_maximum(
            candidate.transform_timeline_edits, animation.name, &maximum_time);
        include_animation_timeline_maximum(
            candidate.mesh_deform_timeline_edits, animation.name, &maximum_time);
        include_animation_timeline_maximum(
            candidate.draw_order_timeline_edits, animation.name, &maximum_time);
        include_animation_timeline_maximum(
            candidate.event_timeline_edits, animation.name, &maximum_time);
        include_animation_timeline_maximum(
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
        const double frame_seconds = 1.0 / frames_per_second;
        applied_delta =
            std::round((earliest_original_time + applied_delta) / frame_seconds) *
                frame_seconds -
            earliest_original_time;
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

} // namespace marrow::editor
