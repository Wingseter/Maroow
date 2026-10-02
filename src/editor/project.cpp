#include "marrow/editor/project.hpp"
#include "project_json.hpp"
#include "project_internal.hpp"

// The curve-mode and driver tokens have exactly one definition, shared by this
// parser/serializer, the Agent handler, and the shell.
#include "marrow/editor/authoring.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>


namespace marrow::editor {
using namespace project_detail;
namespace {

using marrow::runtime::json::Document;
using marrow::runtime::json::LoadError;
using marrow::runtime::json::SourceLocation;
using marrow::runtime::json::Value;

std::filesystem::path absolutize_path(const std::filesystem::path& path) {
    if (path.empty()) {
        return path;
    }
    if (path.is_absolute()) {
        return path.lexically_normal();
    }

    std::error_code error;
    const std::filesystem::path current_directory = std::filesystem::current_path(error);
    if (error) {
        return path.lexically_normal();
    }

    return (current_directory / path).lexically_normal();
}

std::filesystem::path make_project_relative_path(
    const std::filesystem::path& project_path,
    const std::filesystem::path& referenced_path) {
    const std::filesystem::path absolute_project = absolutize_path(project_path);
    const std::filesystem::path absolute_reference = absolutize_path(referenced_path);
    if (absolute_project.empty() || absolute_reference.empty()) {
        return referenced_path.lexically_normal();
    }

    const std::filesystem::path project_directory = absolute_project.parent_path();
    if (project_directory.empty()) {
        return referenced_path.lexically_normal();
    }

    std::error_code error;
    const std::filesystem::path relative_path =
        std::filesystem::relative(absolute_reference, project_directory, error);
    if (error || relative_path.empty()) {
        return absolute_reference;
    }
    const std::string relative_text = relative_path.generic_string();
    if (relative_text == ".." || relative_text.rfind("../", 0) == 0) {
        return absolute_reference;
    }

    return relative_path.lexically_normal();
}

std::string default_project_name(
    const std::filesystem::path& project_path,
    const std::filesystem::path& skeleton_path) {
    if (!project_path.stem().empty()) {
        return project_path.stem().string();
    }
    if (!skeleton_path.stem().empty()) {
        return skeleton_path.stem().string();
    }
    return "marrow_project";
}

std::filesystem::path default_export_filename(const ProjectData& project) {
    if (!project.runtime_assets.skeleton_path.filename().empty()) {
        return project.runtime_assets.skeleton_path.filename();
    }
    if (!project.source_path.stem().empty()) {
        return project.source_path.stem().string() + ".mskl";
    }
    return "exported_skeleton.mskl";
}

} // namespace

bool ParameterModel::empty() const noexcept {
    if (!parameters.empty() || !groups.empty() || !deformers.empty() ||
        !blend_shapes.empty() || !art_paths.empty() || !expressions.empty()) {
        return false;
    }
    if (!lip_sync.empty()) {
        return false;
    }
    if (source.is_object()) {
        static constexpr std::array<std::string_view, 7U> kKnownKeys{
            "parameters",
            "groups",
            "deformers",
            "blend_shapes",
            "art_paths",
            "expressions",
            "lip_sync",
        };
        for (const auto& [key, unused] : source.as_object()) {
            (void)unused;
            if (std::find(kKnownKeys.begin(), kKnownKeys.end(), key) == kKnownKeys.end()) {
                return false;
            }
        }
    }
    return true;
}

const ParameterAuthoringDefinition* ParameterModel::find_parameter(
    std::string_view id) const {
    const auto found = std::find_if(
        parameters.begin(), parameters.end(),
        [&](const ParameterAuthoringDefinition& parameter) { return parameter.id == id; });
    return found == parameters.end() ? nullptr : &*found;
}

ParameterAuthoringDefinition* ParameterModel::find_parameter(std::string_view id) {
    return const_cast<ParameterAuthoringDefinition*>(
        std::as_const(*this).find_parameter(id));
}

const ParameterGroupAuthoringDefinition* ParameterModel::find_group(
    std::string_view id) const {
    const auto found = std::find_if(
        groups.begin(), groups.end(),
        [&](const ParameterGroupAuthoringDefinition& group) { return group.id == id; });
    return found == groups.end() ? nullptr : &*found;
}

ParameterGroupAuthoringDefinition* ParameterModel::find_group(std::string_view id) {
    return const_cast<ParameterGroupAuthoringDefinition*>(std::as_const(*this).find_group(id));
}

const ParameterShapeAuthoringDefinition* ParameterModel::find_shape(
    std::string_view id) const {
    const auto found = std::find_if(
        blend_shapes.begin(), blend_shapes.end(),
        [&](const ParameterShapeAuthoringDefinition& shape) { return shape.id == id; });
    return found == blend_shapes.end() ? nullptr : &*found;
}

ParameterShapeAuthoringDefinition* ParameterModel::find_shape(std::string_view id) {
    return const_cast<ParameterShapeAuthoringDefinition*>(std::as_const(*this).find_shape(id));
}

const ParameterDeformerAuthoringDefinition* ParameterModel::find_deformer(
    std::string_view id) const {
    const auto found = std::find_if(
        deformers.begin(), deformers.end(),
        [&](const ParameterDeformerAuthoringDefinition& deformer) {
            return deformer.id == id;
        });
    return found == deformers.end() ? nullptr : &*found;
}

ParameterDeformerAuthoringDefinition* ParameterModel::find_deformer(std::string_view id) {
    return const_cast<ParameterDeformerAuthoringDefinition*>(
        std::as_const(*this).find_deformer(id));
}

const ArtPathAuthoringDefinition* ParameterModel::find_art_path(std::string_view id) const {
    const auto found = std::find_if(
        art_paths.begin(), art_paths.end(),
        [&](const ArtPathAuthoringDefinition& art_path) { return art_path.id == id; });
    return found == art_paths.end() ? nullptr : &*found;
}

ArtPathAuthoringDefinition* ParameterModel::find_art_path(std::string_view id) {
    return const_cast<ArtPathAuthoringDefinition*>(std::as_const(*this).find_art_path(id));
}

const ExpressionAuthoringDefinition* ParameterModel::find_expression(
    std::string_view id) const {
    const auto found = std::find_if(
        expressions.begin(), expressions.end(),
        [&](const ExpressionAuthoringDefinition& expression) {
            return expression.id == id;
        });
    return found == expressions.end() ? nullptr : &*found;
}

ExpressionAuthoringDefinition* ParameterModel::find_expression(std::string_view id) {
    return const_cast<ExpressionAuthoringDefinition*>(
        std::as_const(*this).find_expression(id));
}

const LipSyncMappingAuthoringDefinition* ParameterModel::find_lip_mapping(
    std::string_view parameter_id) const {
    const auto found = std::find_if(
        lip_sync.mappings.begin(), lip_sync.mappings.end(),
        [&](const LipSyncMappingAuthoringDefinition& mapping) {
            return mapping.parameter == parameter_id;
        });
    return found == lip_sync.mappings.end() ? nullptr : &*found;
}

LipSyncMappingAuthoringDefinition* ParameterModel::find_lip_mapping(
    std::string_view parameter_id) {
    return const_cast<LipSyncMappingAuthoringDefinition*>(
        std::as_const(*this).find_lip_mapping(parameter_id));
}

std::filesystem::path ProjectData::resolve_path(const std::filesystem::path& referenced_path) const {
    if (referenced_path.empty()) {
        return referenced_path;
    }
    if (referenced_path.is_absolute() || source_path.empty()) {
        return referenced_path.lexically_normal();
    }

    return (source_path.parent_path() / referenced_path).lexically_normal();
}

std::filesystem::path ProjectData::resolved_skeleton_path() const {
    return resolve_path(runtime_assets.skeleton_path);
}

std::vector<std::filesystem::path> ProjectData::resolved_atlas_paths() const {
    std::vector<std::filesystem::path> resolved_paths;
    resolved_paths.reserve(runtime_assets.atlas_paths.size());
    for (const auto& atlas_path : runtime_assets.atlas_paths) {
        resolved_paths.push_back(resolve_path(atlas_path));
    }
    return resolved_paths;
}

std::filesystem::path ProjectData::resolved_export_skeleton_path() const {
    const std::filesystem::path export_path =
        editor_metadata.export_directory / default_export_filename(*this);
    return resolve_path(export_path);
}

std::filesystem::path ProjectData::resolved_export_binary_path() const {
    return default_export_binary_path(resolved_export_skeleton_path());
}

const TransformTimelineEdit* ProjectData::find_transform_timeline_edit(
    std::string_view animation_name,
    std::string_view bone_name,
    TransformTimelineChannel channel) const {
    const auto iterator = std::find_if(
        transform_timeline_edits.begin(),
        transform_timeline_edits.end(),
        [&](const TransformTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.bone_name == bone_name &&
                edit.channel == channel;
        });
    return iterator == transform_timeline_edits.end() ? nullptr : &(*iterator);
}

TransformTimelineEdit* ProjectData::find_transform_timeline_edit(
    std::string_view animation_name,
    std::string_view bone_name,
    TransformTimelineChannel channel) {
    const auto iterator = std::find_if(
        transform_timeline_edits.begin(),
        transform_timeline_edits.end(),
        [&](const TransformTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.bone_name == bone_name &&
                edit.channel == channel;
        });
    return iterator == transform_timeline_edits.end() ? nullptr : &(*iterator);
}

// The one mode vocabulary, exposed for `merge_inherit_timeline`. Forwards to
// the single bidirectional table in this file's anonymous namespace.
std::optional<runtime::BoneInherit> inherit_mode_from_key(std::string_view key) {
    return inherit_mode_from_key_impl(key);
}

std::string_view inherit_mode_json_key(runtime::BoneInherit inherit) {
    return inherit_mode_json_key_impl(inherit);
}

TransformTimelineEdit* ensure_transform_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view bone_name,
    TransformTimelineChannel channel) {
    if (TransformTimelineEdit* existing = project.find_transform_timeline_edit(
            animation_name, bone_name, channel)) {
        return existing;
    }
    const auto bone_index = effective_skeleton.find_bone_index(bone_name);
    const runtime::AnimationData* animation =
        effective_skeleton.find_animation(animation_name);
    if (!bone_index.has_value() || animation == nullptr) {
        return nullptr;
    }

    TransformTimelineEdit materialized{
        std::string(animation_name), std::string(bone_name), channel, {}};
    const auto copy_rotate = [&](const auto* timeline) {
        if (timeline == nullptr) return;
        materialized.keyframes.reserve(timeline->keyframes.size());
        for (const runtime::RotateKeyframe& source : timeline->keyframes) {
            materialized.keyframes.push_back(TransformKeyframeEdit{
                static_cast<double>(source.time),
                static_cast<double>(source.angle),
                0.0,
                0.0,
                source.interpolation});
        }
    };
    const auto copy_vector = [&](const auto* timeline) {
        if (timeline == nullptr) return;
        materialized.keyframes.reserve(timeline->keyframes.size());
        for (const runtime::VectorKeyframe& source : timeline->keyframes) {
            materialized.keyframes.push_back(TransformKeyframeEdit{
                static_cast<double>(source.time),
                0.0,
                static_cast<double>(source.x),
                static_cast<double>(source.y),
                source.interpolation});
        }
    };
    switch (channel) {
    case TransformTimelineChannel::Rotate:
        copy_rotate(animation->find_rotate_timeline(*bone_index));
        break;
    case TransformTimelineChannel::Translate:
        copy_vector(animation->find_translate_timeline(*bone_index));
        break;
    case TransformTimelineChannel::Scale:
        copy_vector(animation->find_scale_timeline(*bone_index));
        break;
    case TransformTimelineChannel::Shear:
        copy_vector(animation->find_shear_timeline(*bone_index));
        break;
    }
    project.transform_timeline_edits.push_back(std::move(materialized));
    return &project.transform_timeline_edits.back();
}

TransformKeyframeEdit& upsert_transform_keyframe(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view bone_name,
    TransformTimelineChannel channel,
    double time,
    const TransformKeyframePatch& patch,
    runtime::Interpolation new_key_interpolation) {
    constexpr double kKeyTimeEpsilon = 1e-6;

    TransformTimelineEdit* edit = ensure_transform_timeline_edit(
        project, effective_skeleton, animation_name, bone_name, channel);
    // Callers validate animation and bone targets before reaching this shared
    // authoring primitive. Keep a defensive fallback for direct project use.
    if (edit == nullptr) {
        project.transform_timeline_edits.push_back(TransformTimelineEdit{
            std::string(animation_name), std::string(bone_name), channel, {}});
        edit = &project.transform_timeline_edits.back();
    }

    auto key_it = find_keyframe_near_time(
        edit->keyframes, time, kKeyTimeEpsilon);
    if (key_it == edit->keyframes.end()) {
        const auto insertion = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            time,
            [](const TransformKeyframeEdit& keyframe, double key_time) {
                return keyframe.time < key_time;
            });
        TransformKeyframeEdit inserted;
        inserted.time = time;
        // Seeds the new key only; the update path below never touches the
        // interpolation of a key that already exists.
        inserted.interpolation = std::move(new_key_interpolation);
        key_it = edit->keyframes.insert(insertion, std::move(inserted));
    }

    if (patch.angle.has_value()) {
        key_it->angle = setup_relative_rotation_key(
            effective_skeleton, bone_name, *patch.angle);
    }
    if (patch.x.has_value()) {
        key_it->x = *patch.x;
    }
    if (patch.y.has_value()) {
        key_it->y = *patch.y;
    }
    return *key_it;
}

double setup_relative_rotation_key(
    const runtime::SkeletonData& effective_skeleton,
    std::string_view bone_name,
    double absolute_local_rotation) {
    const auto bone_index = effective_skeleton.find_bone_index(bone_name);
    const double setup_rotation = bone_index.has_value()
        ? static_cast<double>(
              effective_skeleton.bones()[*bone_index].setup_pose.rotation)
        : 0.0;
    return absolute_local_rotation - setup_rotation;
}

const BoneInheritTimelineEdit* ProjectData::find_bone_inherit_timeline_edit(
    std::string_view animation_name,
    std::string_view bone_name) const {
    const auto iterator = std::find_if(
        bone_inherit_timeline_edits.begin(),
        bone_inherit_timeline_edits.end(),
        [&](const BoneInheritTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.bone_name == bone_name;
        });
    return iterator == bone_inherit_timeline_edits.end() ? nullptr : &(*iterator);
}

BoneInheritTimelineEdit* ProjectData::find_bone_inherit_timeline_edit(
    std::string_view animation_name,
    std::string_view bone_name) {
    const auto iterator = std::find_if(
        bone_inherit_timeline_edits.begin(),
        bone_inherit_timeline_edits.end(),
        [&](const BoneInheritTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.bone_name == bone_name;
        });
    return iterator == bone_inherit_timeline_edits.end() ? nullptr : &(*iterator);
}

const MeshDeformTimelineEdit* ProjectData::find_mesh_deform_timeline_edit(
    std::string_view animation_name,
    std::string_view slot_name,
    std::string_view attachment_name) const {
    const auto iterator = std::find_if(
        mesh_deform_timeline_edits.begin(),
        mesh_deform_timeline_edits.end(),
        [&](const MeshDeformTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.slot_name == slot_name &&
                edit.attachment_name == attachment_name;
        });
    return iterator == mesh_deform_timeline_edits.end() ? nullptr : &(*iterator);
}

MeshDeformTimelineEdit* ProjectData::find_mesh_deform_timeline_edit(
    std::string_view animation_name,
    std::string_view slot_name,
    std::string_view attachment_name) {
    const auto iterator = std::find_if(
        mesh_deform_timeline_edits.begin(),
        mesh_deform_timeline_edits.end(),
        [&](const MeshDeformTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.slot_name == slot_name &&
                edit.attachment_name == attachment_name;
    });
    return iterator == mesh_deform_timeline_edits.end() ? nullptr : &(*iterator);
}

const MeshWeightAttachmentEdit* ProjectData::find_mesh_weight_attachment_edit(
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name) const {
    const auto iterator = std::find_if(
        mesh_weight_attachment_edits.begin(),
        mesh_weight_attachment_edits.end(),
        [&](const MeshWeightAttachmentEdit& edit) {
            return edit.skin_name == skin_name &&
                edit.slot_name == slot_name &&
                edit.attachment_name == attachment_name;
        });
    return iterator == mesh_weight_attachment_edits.end() ? nullptr : &(*iterator);
}

MeshWeightAttachmentEdit* ProjectData::find_mesh_weight_attachment_edit(
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name) {
    const auto iterator = std::find_if(
        mesh_weight_attachment_edits.begin(),
        mesh_weight_attachment_edits.end(),
        [&](const MeshWeightAttachmentEdit& edit) {
            return edit.skin_name == skin_name &&
                edit.slot_name == slot_name &&
                edit.attachment_name == attachment_name;
        });
    return iterator == mesh_weight_attachment_edits.end() ? nullptr : &(*iterator);
}

const DrawOrderTimelineEdit* ProjectData::find_draw_order_timeline_edit(
    std::string_view animation_name) const {
    const auto iterator = std::find_if(
        draw_order_timeline_edits.begin(),
        draw_order_timeline_edits.end(),
        [&](const DrawOrderTimelineEdit& edit) {
            return edit.animation_name == animation_name;
        });
    return iterator == draw_order_timeline_edits.end() ? nullptr : &(*iterator);
}

DrawOrderTimelineEdit* ProjectData::find_draw_order_timeline_edit(
    std::string_view animation_name) {
    const auto iterator = std::find_if(
        draw_order_timeline_edits.begin(),
        draw_order_timeline_edits.end(),
        [&](const DrawOrderTimelineEdit& edit) {
            return edit.animation_name == animation_name;
        });
    return iterator == draw_order_timeline_edits.end() ? nullptr : &(*iterator);
}

const EventTimelineEdit* ProjectData::find_event_timeline_edit(
    std::string_view animation_name) const {
    const auto iterator = std::find_if(
        event_timeline_edits.begin(),
        event_timeline_edits.end(),
        [&](const EventTimelineEdit& edit) {
            return edit.animation_name == animation_name;
        });
    return iterator == event_timeline_edits.end() ? nullptr : &(*iterator);
}

EventTimelineEdit* ProjectData::find_event_timeline_edit(
    std::string_view animation_name) {
    const auto iterator = std::find_if(
        event_timeline_edits.begin(),
        event_timeline_edits.end(),
        [&](const EventTimelineEdit& edit) {
            return edit.animation_name == animation_name;
        });
    return iterator == event_timeline_edits.end() ? nullptr : &(*iterator);
}

const SlotColorTimelineEdit* ProjectData::find_slot_color_timeline_edit(
    std::string_view animation_name,
    std::string_view slot_name) const {
    const auto iterator = std::find_if(
        slot_color_timeline_edits.begin(),
        slot_color_timeline_edits.end(),
        [&](const SlotColorTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.slot_name == slot_name;
        });
    return iterator == slot_color_timeline_edits.end() ? nullptr : &(*iterator);
}

SlotColorTimelineEdit* ProjectData::find_slot_color_timeline_edit(
    std::string_view animation_name,
    std::string_view slot_name) {
    const auto iterator = std::find_if(
        slot_color_timeline_edits.begin(),
        slot_color_timeline_edits.end(),
        [&](const SlotColorTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.slot_name == slot_name;
        });
    return iterator == slot_color_timeline_edits.end() ? nullptr : &(*iterator);
}

const SlotAttachmentTimelineEdit* ProjectData::find_slot_attachment_timeline_edit(
    std::string_view animation_name,
    std::string_view slot_name) const {
    const auto iterator = std::find_if(
        slot_attachment_timeline_edits.begin(),
        slot_attachment_timeline_edits.end(),
        [&](const SlotAttachmentTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.slot_name == slot_name;
        });
    return iterator == slot_attachment_timeline_edits.end() ? nullptr : &(*iterator);
}

SlotAttachmentTimelineEdit* ProjectData::find_slot_attachment_timeline_edit(
    std::string_view animation_name,
    std::string_view slot_name) {
    const auto iterator = std::find_if(
        slot_attachment_timeline_edits.begin(),
        slot_attachment_timeline_edits.end(),
        [&](const SlotAttachmentTimelineEdit& edit) {
            return edit.animation_name == animation_name &&
                edit.slot_name == slot_name;
        });
    return iterator == slot_attachment_timeline_edits.end() ? nullptr : &(*iterator);
}

MeshDeformTimelineEdit* ensure_mesh_deform_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view slot_name,
    std::string_view attachment_name) {
    if (MeshDeformTimelineEdit* existing = project.find_mesh_deform_timeline_edit(
            animation_name, slot_name, attachment_name)) {
        return existing;
    }
    const auto slot_index = effective_skeleton.find_slot_index(slot_name);
    const runtime::AnimationData* animation =
        effective_skeleton.find_animation(animation_name);
    if (!slot_index.has_value() || animation == nullptr) return nullptr;

    MeshDeformTimelineEdit materialized{
        std::string(animation_name),
        std::string(slot_name),
        std::string(attachment_name),
        {}};
    const auto* timeline =
        animation->find_deform_timeline(*slot_index, attachment_name);
    if (timeline != nullptr) {
        materialized.keyframes.reserve(timeline->keyframes.size());
        for (const runtime::DeformKeyframe& source : timeline->keyframes) {
            DeformKeyframeEdit keyframe;
            keyframe.time = static_cast<double>(source.time);
            keyframe.interpolation = source.interpolation;
            keyframe.vertex_offsets.reserve(source.vertex_offsets.size());
            for (const runtime::AnimationScalar value : source.vertex_offsets) {
                keyframe.vertex_offsets.push_back(static_cast<double>(value));
            }
            materialized.keyframes.push_back(std::move(keyframe));
        }
    }
    project.mesh_deform_timeline_edits.push_back(std::move(materialized));
    return &project.mesh_deform_timeline_edits.back();
}

DrawOrderTimelineEdit* ensure_draw_order_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name) {
    if (DrawOrderTimelineEdit* existing =
            project.find_draw_order_timeline_edit(animation_name)) {
        return existing;
    }
    const runtime::AnimationData* animation =
        effective_skeleton.find_animation(animation_name);
    if (animation == nullptr) return nullptr;

    DrawOrderTimelineEdit materialized{std::string(animation_name), {}};
    const auto* timeline = animation->find_draw_order_timeline();
    if (timeline != nullptr) {
        materialized.keyframes.reserve(timeline->keyframes.size());
        for (const auto& source : timeline->keyframes) {
            DrawOrderKeyframeEdit keyframe;
            keyframe.time = static_cast<double>(source.time);
            keyframe.slot_names.reserve(source.slot_indices.size());
            for (const std::size_t slot_index : source.slot_indices) {
                if (slot_index >= effective_skeleton.slots().size()) return nullptr;
                keyframe.slot_names.push_back(
                    effective_skeleton.slots()[slot_index].name);
            }
            materialized.keyframes.push_back(std::move(keyframe));
        }
    }
    project.draw_order_timeline_edits.push_back(std::move(materialized));
    return &project.draw_order_timeline_edits.back();
}

EventTimelineEdit* ensure_event_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name) {
    if (EventTimelineEdit* existing = project.find_event_timeline_edit(animation_name)) {
        return existing;
    }
    const runtime::AnimationData* animation =
        effective_skeleton.find_animation(animation_name);
    if (animation == nullptr) return nullptr;

    EventTimelineEdit materialized{std::string(animation_name), {}};
    const auto* timeline = animation->find_event_timeline();
    if (timeline != nullptr) {
        materialized.keyframes.reserve(timeline->keyframes.size());
        const auto widen = [](const auto& value) -> std::optional<double> {
            return value.has_value()
                ? std::optional<double>(static_cast<double>(*value))
                : std::nullopt;
        };
        for (const auto& source : timeline->keyframes) {
            if (source.event_index >= effective_skeleton.events().size()) return nullptr;
            EventKeyframeEdit keyframe;
            keyframe.time = static_cast<double>(source.time);
            keyframe.event_name =
                effective_skeleton.events()[source.event_index].name;
            keyframe.int_value = source.int_value;
            keyframe.float_value = widen(source.float_value);
            keyframe.string_value = source.string_value;
            keyframe.audio_path = source.audio_path;
            keyframe.volume = widen(source.volume);
            keyframe.balance = widen(source.balance);
            materialized.keyframes.push_back(std::move(keyframe));
        }
    }
    project.event_timeline_edits.push_back(std::move(materialized));
    return &project.event_timeline_edits.back();
}

SlotColorTimelineEdit* ensure_slot_color_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view slot_name) {
    if (SlotColorTimelineEdit* existing =
            project.find_slot_color_timeline_edit(animation_name, slot_name)) {
        return existing;
    }
    const auto slot_index = effective_skeleton.find_slot_index(slot_name);
    const runtime::AnimationData* animation =
        effective_skeleton.find_animation(animation_name);
    if (!slot_index.has_value() || animation == nullptr) return nullptr;

    SlotColorTimelineEdit materialized{
        std::string(animation_name), std::string(slot_name), {}};
    const auto* timeline = animation->find_color_timeline(*slot_index);
    if (timeline != nullptr) {
        materialized.keyframes.reserve(timeline->keyframes.size());
        for (const auto& source : timeline->keyframes) {
            materialized.keyframes.push_back(SlotColorKeyframeEdit{
                static_cast<double>(source.time),
                source.color,
                source.interpolation});
        }
    }
    project.slot_color_timeline_edits.push_back(std::move(materialized));
    return &project.slot_color_timeline_edits.back();
}

SlotAttachmentTimelineEdit* ensure_slot_attachment_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view slot_name) {
    if (SlotAttachmentTimelineEdit* existing =
            project.find_slot_attachment_timeline_edit(animation_name, slot_name)) {
        return existing;
    }
    const auto slot_index = effective_skeleton.find_slot_index(slot_name);
    const runtime::AnimationData* animation =
        effective_skeleton.find_animation(animation_name);
    if (!slot_index.has_value() || animation == nullptr) return nullptr;

    SlotAttachmentTimelineEdit materialized{
        std::string(animation_name), std::string(slot_name), {}};
    const auto* timeline = animation->find_attachment_timeline(*slot_index);
    if (timeline != nullptr) {
        materialized.keyframes.reserve(timeline->keyframes.size());
        for (const auto& source : timeline->keyframes) {
            materialized.keyframes.push_back(SlotAttachmentKeyframeEdit{
                static_cast<double>(source.time), source.attachment_name});
        }
    }
    project.slot_attachment_timeline_edits.push_back(std::move(materialized));
    return &project.slot_attachment_timeline_edits.back();
}

BoneInheritTimelineEdit* ensure_bone_inherit_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view bone_name) {
    if (BoneInheritTimelineEdit* existing =
            project.find_bone_inherit_timeline_edit(animation_name, bone_name)) {
        return existing;
    }
    const auto bone_index = effective_skeleton.find_bone_index(bone_name);
    const runtime::AnimationData* animation =
        effective_skeleton.find_animation(animation_name);
    if (!bone_index.has_value() || animation == nullptr) return nullptr;

    BoneInheritTimelineEdit materialized{
        std::string(animation_name), std::string(bone_name), {}};
    // MAR-185 note: the effective skeleton handed here has already been through
    // `prune_constant_timelines`, so a base track that was a single constant key
    // at the origin is legitimately ABSENT and materializes as empty. That is
    // not a lost track -- it is a track the runtime proved redundant.
    const auto* timeline = animation->find_inherit_timeline(*bone_index);
    if (timeline != nullptr) {
        materialized.keyframes.reserve(timeline->keyframes.size());
        for (const auto& source : timeline->keyframes) {
            materialized.keyframes.push_back(InheritKeyframeEdit{
                static_cast<double>(source.time), source.inherit});
        }
    }
    project.bone_inherit_timeline_edits.push_back(std::move(materialized));
    return &project.bone_inherit_timeline_edits.back();
}

const IkConstraintEdit* ProjectData::find_ik_constraint_edit(std::string_view name) const {
    const auto iterator = std::find_if(
        ik_constraint_edits.begin(),
        ik_constraint_edits.end(),
        [&](const IkConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == ik_constraint_edits.end() ? nullptr : &(*iterator);
}

IkConstraintEdit* ProjectData::find_ik_constraint_edit(std::string_view name) {
    const auto iterator = std::find_if(
        ik_constraint_edits.begin(),
        ik_constraint_edits.end(),
        [&](const IkConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == ik_constraint_edits.end() ? nullptr : &(*iterator);
}

const PathConstraintEdit* ProjectData::find_path_constraint_edit(std::string_view name) const {
    const auto iterator = std::find_if(
        path_constraint_edits.begin(),
        path_constraint_edits.end(),
        [&](const PathConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == path_constraint_edits.end() ? nullptr : &(*iterator);
}

PathConstraintEdit* ProjectData::find_path_constraint_edit(std::string_view name) {
    const auto iterator = std::find_if(
        path_constraint_edits.begin(),
        path_constraint_edits.end(),
        [&](const PathConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == path_constraint_edits.end() ? nullptr : &(*iterator);
}

const TransformConstraintEdit* ProjectData::find_transform_constraint_edit(
    std::string_view name) const {
    const auto iterator = std::find_if(
        transform_constraint_edits.begin(),
        transform_constraint_edits.end(),
        [&](const TransformConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == transform_constraint_edits.end() ? nullptr : &(*iterator);
}

TransformConstraintEdit* ProjectData::find_transform_constraint_edit(std::string_view name) {
    const auto iterator = std::find_if(
        transform_constraint_edits.begin(),
        transform_constraint_edits.end(),
        [&](const TransformConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == transform_constraint_edits.end() ? nullptr : &(*iterator);
}

const PhysicsConstraintEdit* ProjectData::find_physics_constraint_edit(std::string_view name) const {
    const auto iterator = std::find_if(
        physics_constraint_edits.begin(),
        physics_constraint_edits.end(),
        [&](const PhysicsConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == physics_constraint_edits.end() ? nullptr : &(*iterator);
}

PhysicsConstraintEdit* ProjectData::find_physics_constraint_edit(std::string_view name) {
    const auto iterator = std::find_if(
        physics_constraint_edits.begin(),
        physics_constraint_edits.end(),
        [&](const PhysicsConstraintEdit& edit) {
            return edit.name == name;
        });
    return iterator == physics_constraint_edits.end() ? nullptr : &(*iterator);
}

const AtlasPackDefinition* ProjectData::find_atlas_pack_definition(
    const std::filesystem::path& atlas_path) const {
    const std::filesystem::path resolved_path = resolve_path(atlas_path);
    const auto iterator = std::find_if(
        atlas_pack_definitions.begin(),
        atlas_pack_definitions.end(),
        [&](const AtlasPackDefinition& definition) {
            return resolve_path(definition.atlas_path) == resolved_path;
        });
    return iterator == atlas_pack_definitions.end() ? nullptr : &(*iterator);
}

AtlasPackDefinition* ProjectData::find_atlas_pack_definition(
    const std::filesystem::path& atlas_path) {
    const std::filesystem::path resolved_path = resolve_path(atlas_path);
    const auto iterator = std::find_if(
        atlas_pack_definitions.begin(),
        atlas_pack_definitions.end(),
        [&](const AtlasPackDefinition& definition) {
            return resolve_path(definition.atlas_path) == resolved_path;
        });
    return iterator == atlas_pack_definitions.end() ? nullptr : &(*iterator);
}

std::string ProjectSaveError::format() const {
    if (path.empty()) {
        return "Failed to save project: " + message;
    }

    return "Failed to save project '" + path.string() + "': " + message;
}

std::string ProjectExportError::format() const {
    if (path.empty()) {
        return "Failed to export runtime assets: " + message;
    }

    return "Failed to export runtime assets '" + path.string() + "': " + message;
}

ProjectData create_minimal_project(const MinimalProjectOptions& options) {
    ProjectData project;
    project.source_path = options.project_path;
    project.runtime_assets.skeleton_path =
        make_project_relative_path(options.project_path, options.skeleton_path);
    project.runtime_assets.atlas_paths.reserve(options.atlas_paths.size());
    for (const auto& atlas_path : options.atlas_paths) {
        project.runtime_assets.atlas_paths.push_back(
            make_project_relative_path(options.project_path, atlas_path));
    }

    project.editor_metadata.name =
        options.name.empty() ? default_project_name(options.project_path, options.skeleton_path)
                             : options.name;
    project.editor_metadata.active_animation = options.active_animation;
    project.editor_metadata.preview_skins = options.preview_skins;
    project.editor_metadata.export_directory = options.export_directory;
    if (project.editor_metadata.preview_skins.empty()) {
        project.editor_metadata.preview_skins.push_back("default");
    }
    if (project.editor_metadata.active_animation.empty()) {
        project.editor_metadata.active_animation = "idle";
    }
    project.editor_metadata.notes =
        options.notes.empty()
            ? "Minimal Marrow editor project referencing the canonical runtime fixtures."
            : options.notes;
    return project;
}

std::filesystem::path project_relative_path(
    const std::filesystem::path& project_path,
    const std::filesystem::path& referenced_path) {
    return make_project_relative_path(project_path, referenced_path);
}

ProjectData rebase_project_paths(
    const ProjectData& project,
    const std::filesystem::path& new_project_path) {
    ProjectData result = project;
    result.source_path = new_project_path;

    // Resolve against the OLD directory, then relativize against the new one.
    // The composition is what makes the rewrite identity-preserving. An empty
    // reference has nothing to resolve; an absolute one already names its file
    // and is preserved verbatim, which is what "preserving absolute paths" means.
    const auto rebase = [&project, &new_project_path](
                            const std::filesystem::path& reference) {
        if (reference.empty() || reference.is_absolute()) {
            return reference;
        }
        return make_project_relative_path(
            new_project_path,
            project.resolve_path(reference));
    };

    result.runtime_assets.skeleton_path = rebase(project.runtime_assets.skeleton_path);
    for (auto& atlas_path : result.runtime_assets.atlas_paths) {
        atlas_path = rebase(atlas_path);
    }
    result.editor_metadata.export_directory =
        rebase(project.editor_metadata.export_directory);
    // Atlas packs must move with `runtime.atlases` or find_atlas_pack_definition
    // stops matching them by resolved path and packed export silently degrades.
    for (auto& definition : result.atlas_pack_definitions) {
        definition.atlas_path = rebase(definition.atlas_path);
        for (auto& sprite : definition.sprites) {
            sprite.image_path = rebase(sprite.image_path);
        }
    }
    // The sixth family (MAR-188). Two paths, both project-relative; the per-layer
    // `image_file` is a bare file name and is deliberately NOT a path, which holds
    // this family to two fields -- see `PsdLayerProvenance::image_file`.
    //
    // Reads from `project` and writes to `result`, matching the five above: the
    // lambda captures `project` by reference precisely because `result.source_path`
    // was already overwritten at the top of this function, and rebasing against the
    // new location would resolve every reference into the NEW directory.
    if (result.editor_metadata.import_sources.has_value() &&
        result.editor_metadata.import_sources->psd.has_value()) {
        const PsdImportProvenance& source =
            *project.editor_metadata.import_sources->psd;
        PsdImportProvenance& target = *result.editor_metadata.import_sources->psd;
        target.source_path = rebase(source.source_path);
        target.layers_directory = rebase(source.layers_directory);
    }

    return result;
}

namespace {

/** @brief Runs `visitor` on the upsert vector that belongs to `family`. */
template <typename Visitor>
bool visit_constraint_edits(ProjectData* project, ConstraintKind family, Visitor&& visitor) {
    switch (family) {
    case ConstraintKind::Ik:
        return visitor(project->ik_constraint_edits);
    case ConstraintKind::Path:
        return visitor(project->path_constraint_edits);
    case ConstraintKind::Transform:
        return visitor(project->transform_constraint_edits);
    case ConstraintKind::Physics:
        return visitor(project->physics_constraint_edits);
    }
    return false;
}

/** @brief Names of the upserts a family carries, in vector order. */
std::vector<std::string> constraint_edit_names(
    const ProjectData& project,
    ConstraintKind family) {
    std::vector<std::string> names;
    const auto collect = [&names](const auto& edits) {
        for (const auto& edit : edits) {
            names.push_back(edit.name);
        }
        return true;
    };
    visit_constraint_edits(const_cast<ProjectData*>(&project), family, collect);
    return names;
}

/** @brief The `name` of every element of one base constraint array. */
std::vector<std::string> constraint_array_names(const Value& root, std::string_view key) {
    std::vector<std::string> names;
    const Value* array = find_optional_member(root, key);
    if (array == nullptr || !array->is_array()) {
        return names;
    }
    for (const Value& element : array->as_array()) {
        const Value* name = find_optional_member(element, "name");
        if (name != nullptr && name->is_string()) {
            names.push_back(name->as_string());
        }
    }
    return names;
}

/**
 * @brief Applies the ownership rule for one constraint, preflight-then-mutate.
 *
 * `new_name` empty means delete. The name set the request resolves against is
 * the one the user sees: the base family array with the project's existing
 * records already replayed, plus the family's upserts. It is produced by
 * running the same `apply_constraint_lifecycle_operations()` the materializer
 * runs, so "what names exist" has exactly one definition.
 */
ConstraintLifecycleResult apply_constraint_lifecycle_request(
    ProjectData* project,
    const Document& base_skeleton_document,
    ConstraintKind family,
    std::string_view from,
    std::string_view new_name) {
    ConstraintLifecycleResult result;
    if (project == nullptr) {
        result.message = "no project";
        return result;
    }
    const bool is_rename = !new_name.empty();
    const std::string family_key(constraint_family_json_key(family));
    const std::string source(from);
    const std::string target(new_name);

    if (source.empty()) {
        result.message = family_key + " constraint name must not be empty";
        return result;
    }

    ProjectData candidate = *project;

    Value replayed_root = base_skeleton_document.root;
    apply_constraint_lifecycle_operations(
        &replayed_root, candidate.constraint_lifecycle_operations);
    const std::vector<std::string> base_names =
        constraint_array_names(replayed_root, family_key);
    const std::vector<std::string> upsert_names = constraint_edit_names(candidate, family);
    const auto holds = [](const std::vector<std::string>& names, const std::string& name) {
        return std::find(names.begin(), names.end(), name) != names.end();
    };

    const bool in_base = holds(base_names, source);
    const bool in_upserts = holds(upsert_names, source);
    if (!in_base && !in_upserts) {
        result.message = family_key + " constraint '" + source + "' does not exist";
        return result;
    }

    if (is_rename) {
        if (target == source) {
            result.message = family_key + " constraint rename target '" + target +
                "' must differ from its source";
            return result;
        }
        // A collision is refused rather than auto-suffixed. `unique_constraint_name()`
        // owns auto-suffixing for the create path; a rename is the user's choice
        // of a specific name, and quietly giving them a different one is worse
        // than declining.
        if (holds(base_names, target) || holds(upsert_names, target)) {
            result.message = family_key + " constraint rename target '" + target +
                "' is already taken by a live " + family_key + " constraint";
            return result;
        }
    } else if (!target.empty()) {
        result.message = family_key + " constraint delete must not carry a new name";
        return result;
    }

    // The ownership rule. A base-backed constraint cannot be renamed or removed
    // by upsert, so it needs a record; a shadowing upsert needs both, because
    // rewriting only the record would leave the upsert's old name to reappear
    // in Phase B, and erasing only the upsert would resurrect the base element.
    if (in_base) {
        ConstraintLifecycleOperation operation;
        operation.kind = is_rename ? ConstraintLifecycleKind::Rename
                                   : ConstraintLifecycleKind::Delete;
        operation.family = family;
        operation.name = source;
        operation.new_name = is_rename ? target : std::string{};
        candidate.constraint_lifecycle_operations.push_back(std::move(operation));
        result.used_operation = true;
    }
    if (in_upserts) {
        const auto rewrite = [&](auto& edits) {
            const auto found = std::find_if(
                edits.begin(), edits.end(), [&](const auto& edit) {
                    return edit.name == source;
                });
            if (found == edits.end()) {
                return false;
            }
            if (is_rename) {
                found->name = target;
            } else {
                edits.erase(found);
            }
            return true;
        };
        result.changed_upsert = visit_constraint_edits(&candidate, family, rewrite);
    }

    // Both the ordered records and the resulting upserts must survive the save
    // validator, so no primitive can return success and leave a project
    // `save_project()` refuses.
    ProjectSaveError save_error;
    if (!validate_project_for_save(candidate, &save_error)) {
        return ConstraintLifecycleResult{false, save_error.message, false, false};
    }
    // And the whole record sequence must still resolve against the base, so no
    // primitive can return success and leave a project that cannot be opened.
    if (const auto lifecycle_error =
            validate_constraint_lifecycle_operations(candidate, base_skeleton_document)) {
        return ConstraintLifecycleResult{false, lifecycle_error->message, false, false};
    }

    *project = std::move(candidate);
    result.ok = true;
    return result;
}

} // namespace

ConstraintLifecycleResult rename_constraint(
    ProjectData* project,
    const runtime::json::Document& base_skeleton_document,
    ConstraintKind family,
    std::string_view from,
    std::string_view to) {
    if (to.empty()) {
        ConstraintLifecycleResult result;
        result.message = std::string(constraint_family_json_key(family)) +
            " constraint rename target must not be empty";
        return result;
    }
    return apply_constraint_lifecycle_request(
        project, base_skeleton_document, family, from, to);
}

ConstraintLifecycleResult delete_constraint(
    ProjectData* project,
    const runtime::json::Document& base_skeleton_document,
    ConstraintKind family,
    std::string_view name) {
    return apply_constraint_lifecycle_request(
        project, base_skeleton_document, family, name, std::string_view{});
}

namespace project_detail {

std::filesystem::path default_export_binary_path(const std::filesystem::path& skeleton_path) {
    std::filesystem::path binary_path = skeleton_path;
    binary_path.replace_extension(marrow::runtime::skeleton_binary_extension());
    return binary_path;
}

} // namespace project_detail

} // namespace marrow::editor
