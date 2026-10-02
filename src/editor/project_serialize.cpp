#include "marrow/editor/project.hpp"
#include "project_json.hpp"
#include "project_internal.hpp"
#include "marrow/editor/authoring.hpp"
#include <utility>

namespace marrow::editor {
using namespace project_detail;
using marrow::runtime::json::Document;
using marrow::runtime::json::LoadError;
using marrow::runtime::json::SourceLocation;
using marrow::runtime::json::Value;

namespace {

void emit_curve_intent(
    Value::Object* keyframe_object,
    TimelineCurveMode mode,
    TimelineScalarComponent driver) {
    if (mode != TimelineCurveMode::Auto) return;
    keyframe_object->emplace(
        "curve_mode", make_string_value(std::string(curve_mode_token(mode))));
    keyframe_object->emplace(
        "curve_driver", make_string_value(std::string(curve_driver_token(driver))));
}

Value build_constraint_lifecycle_operations_value(
    const std::vector<ConstraintLifecycleOperation>& operations) {
    Value::Array records;
    records.reserve(operations.size());
    for (const ConstraintLifecycleOperation& operation : operations) {
        Value::Object record;
        record.emplace(
            "family",
            make_string_value(std::string(constraint_family_json_key(operation.family))));
        if (operation.kind == ConstraintLifecycleKind::Rename) {
            record.emplace("from", make_string_value(operation.name));
            record.emplace("op", make_string_value(std::string("rename")));
            record.emplace("to", make_string_value(operation.new_name));
        } else {
            record.emplace("name", make_string_value(operation.name));
            record.emplace("op", make_string_value(std::string("delete")));
        }
        records.push_back(make_object_value(std::move(record)));
    }
    return make_array_value(std::move(records));
}

Value build_transform_keyframes_value(
    const TransformTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const TransformKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));
        if (is_vector_channel(edit.channel)) {
            keyframe_object.emplace("x", make_number_value(keyframe.x));
            keyframe_object.emplace("y", make_number_value(keyframe.y));
        } else {
            keyframe_object.emplace("angle", make_number_value(keyframe.angle));
        }
        keyframe_object.emplace("curve", build_interpolation_value(keyframe.interpolation));
        emit_curve_intent(&keyframe_object, keyframe.curve_mode, keyframe.curve_driver);
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }

    return make_array_value(std::move(keyframes));
}

Value build_slot_color_keyframes_value(const SlotColorTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const SlotColorKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));
        Value::Object color_object;
        color_object.emplace("r", make_number_value(keyframe.color.r));
        color_object.emplace("g", make_number_value(keyframe.color.g));
        color_object.emplace("b", make_number_value(keyframe.color.b));
        color_object.emplace("a", make_number_value(keyframe.color.a));
        keyframe_object.emplace("color", make_object_value(std::move(color_object)));
        keyframe_object.emplace("curve", build_interpolation_value(keyframe.interpolation));
        emit_curve_intent(&keyframe_object, keyframe.curve_mode, keyframe.curve_driver);
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }
    return make_array_value(std::move(keyframes));
}

/**
 * @brief Projects the three continuous families' `loop_sync` flags into a tree.
 *
 * A pure projection: only opted-in lanes appear, `false` is never written, and
 * an animation, category, bone, or slot object with nothing under it is not
 * emitted. That is what makes an orphan entry unrepresentable on the write
 * side and what keeps a project with no opted-in lane byte-identical.
 */
Value build_loop_sync_value(
    const std::vector<TransformTimelineEdit>& transform_edits,
    const std::vector<MeshDeformTimelineEdit>& mesh_deform_edits,
    const std::vector<SlotColorTimelineEdit>& slot_color_edits) {
    Value::Object animations_object;
    const auto ensure_animation = [&](const std::string& animation_name) {
        auto& animation_value = animations_object[animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }
        return &animation_value;
    };

    for (const TransformTimelineEdit& edit : transform_edits) {
        if (!edit.loop_sync) {
            continue;
        }
        Value* animation_value = ensure_animation(edit.animation_name);
        Value* bones_value = ensure_object_member(animation_value, "bones");
        Value* bone_value = ensure_object_member(bones_value, edit.bone_name);
        if (bone_value != nullptr) {
            bone_value->as_object()[std::string(transform_channel_json_key(edit.channel))] =
                make_boolean_value(true);
        }
    }
    for (const SlotColorTimelineEdit& edit : slot_color_edits) {
        if (!edit.loop_sync) {
            continue;
        }
        Value* animation_value = ensure_animation(edit.animation_name);
        Value* slots_value = ensure_object_member(animation_value, "slots");
        Value* slot_value = ensure_object_member(slots_value, edit.slot_name);
        if (slot_value != nullptr) {
            slot_value->as_object()["color"] = make_boolean_value(true);
        }
    }
    for (const MeshDeformTimelineEdit& edit : mesh_deform_edits) {
        if (!edit.loop_sync) {
            continue;
        }
        Value* animation_value = ensure_animation(edit.animation_name);
        Value* deform_value = ensure_object_member(animation_value, "deform");
        Value* slot_value = ensure_object_member(deform_value, edit.slot_name);
        if (slot_value != nullptr) {
            slot_value->as_object()[edit.attachment_name] = make_boolean_value(true);
        }
    }

    Value::Object loop_sync;
    loop_sync["animations"] = make_object_value(std::move(animations_object));
    return make_object_value(std::move(loop_sync));
}

Value build_timeline_edits_value(
    const std::vector<TransformTimelineEdit>& transform_edits,
    const std::vector<BoneInheritTimelineEdit>& bone_inherit_edits,
    const std::vector<MeshDeformTimelineEdit>& mesh_deform_edits,
    const std::vector<DrawOrderTimelineEdit>& draw_order_edits,
    const std::vector<EventTimelineEdit>& event_edits,
    const std::vector<SlotColorTimelineEdit>& slot_color_edits,
    const std::vector<SlotAttachmentTimelineEdit>& slot_attachment_edits) {
    Value::Object animations_object;
    for (const TransformTimelineEdit& edit : transform_edits) {
        auto& animation_value = animations_object[edit.animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }

        Value* bones_value = ensure_object_member(&animation_value, "bones");
        Value* bone_value = ensure_object_member(bones_value, edit.bone_name);
        if (bone_value != nullptr) {
            bone_value->as_object()[std::string(transform_channel_json_key(edit.channel))] =
                build_transform_keyframes_value(edit);
        }
    }

    for (const BoneInheritTimelineEdit& edit : bone_inherit_edits) {
        if (edit.keyframes.empty()) {
            // `ensure_bone_inherit_timeline_edit` legitimately creates an empty
            // edit for a bone with no base track -- that is what a project-only
            // timeline is before its first key lands. Writing `"inherit": []`
            // would produce a `.marrow` this parser itself refuses to reload.
            continue;
        }
        auto& animation_value = animations_object[edit.animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }

        Value* bones_value = ensure_object_member(&animation_value, "bones");
        Value* bone_value = ensure_object_member(bones_value, edit.bone_name);
        if (bone_value != nullptr) {
            bone_value->as_object()["inherit"] = build_inherit_keyframes_value(edit);
        }
    }

    for (const MeshDeformTimelineEdit& edit : mesh_deform_edits) {
        auto& animation_value = animations_object[edit.animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }

        Value* deform_value = ensure_object_member(&animation_value, "deform");
        Value* slot_value = ensure_object_member(deform_value, edit.slot_name);
        if (slot_value != nullptr) {
            slot_value->as_object()[edit.attachment_name] =
                build_deform_keyframes_value(edit);
        }
    }

    for (const DrawOrderTimelineEdit& edit : draw_order_edits) {
        auto& animation_value = animations_object[edit.animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }

        animation_value.as_object()["drawOrder"] = build_draw_order_keyframes_value(edit);
    }

    for (const EventTimelineEdit& edit : event_edits) {
        auto& animation_value = animations_object[edit.animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }

        animation_value.as_object()["events"] = build_event_keyframes_value(edit);
    }

    for (const SlotColorTimelineEdit& edit : slot_color_edits) {
        auto& animation_value = animations_object[edit.animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }
        Value* slots_value = ensure_object_member(&animation_value, "slots");
        Value* slot_value = ensure_object_member(slots_value, edit.slot_name);
        if (slot_value != nullptr) {
            slot_value->as_object()["color"] = build_slot_color_keyframes_value(edit);
        }
    }

    for (const SlotAttachmentTimelineEdit& edit : slot_attachment_edits) {
        auto& animation_value = animations_object[edit.animation_name];
        if (!animation_value.is_object()) {
            animation_value = make_object_value();
        }
        Value* slots_value = ensure_object_member(&animation_value, "slots");
        Value* slot_value = ensure_object_member(slots_value, edit.slot_name);
        if (slot_value != nullptr) {
            slot_value->as_object()["attachment"] =
                build_slot_attachment_keyframes_value(edit);
        }
    }

    Value::Object timeline_edits;
    timeline_edits.emplace("animations", make_object_value(std::move(animations_object)));
    return make_object_value(std::move(timeline_edits));
}

Value build_constraint_edits_value(
    const std::vector<IkConstraintEdit>& ik_edits,
    const std::vector<PathConstraintEdit>& path_edits,
    const std::vector<TransformConstraintEdit>& transform_edits,
    const std::vector<PhysicsConstraintEdit>& physics_edits,
    const std::vector<ConstraintLifecycleOperation>& lifecycle_operations) {
    Value::Object constraint_edits;
    if (!lifecycle_operations.empty()) {
        constraint_edits.emplace(
            "operations",
            build_constraint_lifecycle_operations_value(lifecycle_operations));
    }
    if (!ik_edits.empty()) {
        constraint_edits.emplace("ik", build_ik_constraint_edits_value(ik_edits));
    }
    if (!path_edits.empty()) {
        constraint_edits.emplace("path", build_path_constraint_edits_value(path_edits));
    }
    if (!transform_edits.empty()) {
        constraint_edits.emplace(
            "transform",
            build_transform_constraint_edits_value(transform_edits));
    }
    if (!physics_edits.empty()) {
        constraint_edits.emplace("physics", build_physics_constraint_edits_value(physics_edits));
    }
    return make_object_value(std::move(constraint_edits));
}

/**
 * @brief Builds `$.editor.import_sources` from MAR-188's typed provenance.
 *
 * Paths go through `.generic_string()`, matching every other serialized path
 * family in this function. `image_file` is a bare name by construction and is
 * emitted verbatim.
 */
Value build_import_sources_value(const ProjectImportSources& sources) {
    Value::Object sources_object;
    if (sources.psd.has_value()) {
        Value::Object psd_object;
        psd_object["path"] = make_string_value(sources.psd->source_path.generic_string());
        psd_object["layers_directory"] =
            make_string_value(sources.psd->layers_directory.generic_string());

        Value::Array layers;
        layers.reserve(sources.psd->layers.size());
        for (const PsdLayerProvenance& layer : sources.psd->layers) {
            Value::Object layer_object;
            Value::Array group_path;
            group_path.reserve(layer.group_path.size());
            for (const std::string& segment : layer.group_path) {
                group_path.push_back(make_string_value(segment));
            }
            layer_object["group_path"] = make_array_value(std::move(group_path));
            layer_object["layer"] = make_string_value(layer.layer_name);
            layer_object["slot"] = make_string_value(layer.slot_name);
            layer_object["attachment"] = make_string_value(layer.attachment_name);
            layer_object["bone"] = make_string_value(layer.bone_name);
            layer_object["image"] = make_string_value(layer.image_file);
            layers.push_back(make_object_value(std::move(layer_object)));
        }
        psd_object["layers"] = make_array_value(std::move(layers));
        sources_object["psd"] = make_object_value(std::move(psd_object));
    }
    return make_object_value(std::move(sources_object));
}

Value build_atlas_pack_definitions_value(
    const std::vector<AtlasPackDefinition>& atlas_pack_definitions) {
    Value::Array definitions;
    definitions.reserve(atlas_pack_definitions.size());
    for (const AtlasPackDefinition& definition : atlas_pack_definitions) {
        Value::Object definition_object;
        definition_object.emplace(
            "atlas",
            make_string_value(definition.atlas_path.generic_string()));
        if (!definition.atlas_name.empty()) {
            definition_object.emplace("atlas_name", make_string_value(definition.atlas_name));
        }
        definition_object.emplace("filter_min", make_string_value(definition.filter_min));
        definition_object.emplace("filter_mag", make_string_value(definition.filter_mag));
        definition_object.emplace("wrap_x", make_string_value(definition.wrap_x));
        definition_object.emplace("wrap_y", make_string_value(definition.wrap_y));
        definition_object.emplace(
            "premultiplied_alpha",
            make_boolean_value(definition.premultiplied_alpha));
        definition_object.emplace("padding", make_number_value(definition.padding));
        definition_object.emplace("trim", make_boolean_value(definition.trim));
        definition_object.emplace("bleed", make_number_value(definition.bleed));

        Value::Array sprites;
        sprites.reserve(definition.sprites.size());
        for (const AtlasPackSprite& sprite : definition.sprites) {
            Value::Object sprite_object;
            sprite_object.emplace("name", make_string_value(sprite.region_name));
            sprite_object.emplace("image", make_string_value(sprite.image_path.generic_string()));
            if (sprite.origin_x.has_value()) {
                sprite_object.emplace("origin_x", make_number_value(*sprite.origin_x));
            }
            if (sprite.origin_y.has_value()) {
                sprite_object.emplace("origin_y", make_number_value(*sprite.origin_y));
            }
            sprites.push_back(make_object_value(std::move(sprite_object)));
        }
        definition_object.emplace("sprites", make_array_value(std::move(sprites)));
        definitions.push_back(make_object_value(std::move(definition_object)));
    }

    return make_array_value(std::move(definitions));
}

Value build_mesh_weight_edits_value(
    const std::vector<MeshWeightAttachmentEdit>& mesh_weight_attachment_edits) {
    Value::Array weights;
    weights.reserve(mesh_weight_attachment_edits.size());
    for (const MeshWeightAttachmentEdit& edit : mesh_weight_attachment_edits) {
        Value::Object edit_object;
        edit_object.emplace("skin", make_string_value(edit.skin_name));
        edit_object.emplace("slot", make_string_value(edit.slot_name));
        edit_object.emplace("attachment", make_string_value(edit.attachment_name));
        edit_object.emplace("weights", build_mesh_weight_vertices_value(edit));
        weights.push_back(make_object_value(std::move(edit_object)));
    }

    Value::Object mesh_edits;
    mesh_edits.emplace("weights", make_array_value(std::move(weights)));
    return make_object_value(std::move(mesh_edits));
}

Value build_animation_edits_value(const std::vector<AnimationEdit>& edits) {
    Value::Array values;
    values.reserve(edits.size());
    for (const AnimationEdit& edit : edits) {
        if (edit.kind == AnimationEditKind::Unknown) {
            values.push_back(edit.preserved_source);
            continue;
        }
        Value::Object object = edit.preserved_source.is_object()
            ? edit.preserved_source.as_object()
            : Value::Object{};
        switch (edit.kind) {
        case AnimationEditKind::Create:
            object["op"] = make_string_value("create");
            object["name"] = make_string_value(edit.name);
            object["animation"] = edit.animation;
            break;
        case AnimationEditKind::Rename:
            object["op"] = make_string_value("rename");
            object["from"] = make_string_value(edit.name);
            object["to"] = make_string_value(edit.new_name);
            break;
        case AnimationEditKind::Delete:
            object["op"] = make_string_value("delete");
            object["name"] = make_string_value(edit.name);
            break;
        case AnimationEditKind::SetDuration:
            object["op"] = make_string_value("set_duration");
            object["name"] = make_string_value(edit.name);
            object["duration"] = make_number_value(edit.duration);
            break;
        case AnimationEditKind::Unknown:
            break;
        }
        values.push_back(make_object_value(std::move(object)));
    }
    return make_array_value(std::move(values));
}

Value build_parameter_model_value(const ParameterModel& model) {
    Value::Object object = model.source.is_object()
        ? model.source.as_object()
        : Value::Object{};

    Value::Array parameters;
    parameters.reserve(model.parameters.size());
    for (const ParameterAuthoringDefinition& parameter : model.parameters) {
        parameters.push_back(build_parameter_definition_value(model, parameter));
    }
    object["parameters"] = make_array_value(std::move(parameters));

    Value::Array groups;
    groups.reserve(model.groups.size());
    for (const ParameterGroupAuthoringDefinition& group : model.groups) {
        groups.push_back(build_parameter_group_value(model, group));
    }
    object["groups"] = make_array_value(std::move(groups));

    const auto typed_array = [](const auto& values, const auto& build) {
        Value::Array array;
        array.reserve(values.size());
        for (const auto& value : values) {
            array.push_back(build(value));
        }
        return make_array_value(std::move(array));
    };
    object["deformers"] = typed_array(
        model.deformers, build_parameter_deformer_authoring_value);
    object["blend_shapes"] = typed_array(
        model.blend_shapes, build_parameter_shape_authoring_value);
    object["art_paths"] = typed_array(
        model.art_paths, build_art_path_authoring_value);
    object["expressions"] = typed_array(
        model.expressions, build_expression_authoring_value);
    object["lip_sync"] = build_lip_sync_authoring_value(model.lip_sync);
    return make_object_value(std::move(object));
}

Value::Object preserved_object_member(const Value& root, std::string_view key) {
    const Value* member = find_optional_member(root, key);
    return member != nullptr && member->is_object()
        ? member->as_object()
        : Value::Object{};
}

Value build_project_value(const ProjectData& project) {
    Value::Object root = project.preserved_root.is_object()
        ? project.preserved_root.as_object()
        : Value::Object{};
    root["marrow"] = make_string_value(project.marrow_version);

    Value::Object runtime_object = preserved_object_member(project.preserved_root, "runtime");
    runtime_object["skeleton"] =
        make_string_value(project.runtime_assets.skeleton_path.generic_string());
    Value::Array atlas_paths;
    atlas_paths.reserve(project.runtime_assets.atlas_paths.size());
    for (const auto& atlas_path : project.runtime_assets.atlas_paths) {
        atlas_paths.push_back(make_string_value(atlas_path.generic_string()));
    }
    runtime_object["atlases"] = make_array_value(std::move(atlas_paths));
    root["runtime"] = make_object_value(std::move(runtime_object));

    Value::Object editor_object = preserved_object_member(project.preserved_root, "editor");
    editor_object["name"] = make_string_value(project.editor_metadata.name);
    editor_object["active_animation"] =
        make_string_value(project.editor_metadata.active_animation);
    Value::Array preview_skins;
    preview_skins.reserve(project.editor_metadata.preview_skins.size());
    for (const std::string& preview_skin : project.editor_metadata.preview_skins) {
        preview_skins.push_back(make_string_value(preview_skin));
    }
    editor_object["preview_skins"] = make_array_value(std::move(preview_skins));
    editor_object["export_directory"] =
        make_string_value(project.editor_metadata.export_directory.generic_string());
    editor_object["notes"] = make_string_value(project.editor_metadata.notes);
    Value::Object timeline_object;
    if (const auto timeline = editor_object.find("timeline");
        timeline != editor_object.end() && timeline->second.is_object()) {
        timeline_object = timeline->second.as_object();
    }
    timeline_object["fps"] =
        make_number_value(project.editor_metadata.timeline.frames_per_second);
    editor_object["timeline"] = make_object_value(std::move(timeline_object));
    Value::Object viewport_object;
    if (const auto viewport = editor_object.find("viewport");
        viewport != editor_object.end() && viewport->second.is_object()) {
        viewport_object = viewport->second.as_object();
    }
    viewport_object["pan_x"] = make_number_value(project.editor_metadata.viewport.pan_x);
    viewport_object["pan_y"] = make_number_value(project.editor_metadata.viewport.pan_y);
    viewport_object["zoom"] = make_number_value(project.editor_metadata.viewport.zoom);
    Value::Object onion_skin_object;
    if (const auto onion_skin = viewport_object.find("onion_skin");
        onion_skin != viewport_object.end() && onion_skin->second.is_object()) {
        onion_skin_object = onion_skin->second.as_object();
    }
    onion_skin_object["enabled"] =
        make_boolean_value(project.editor_metadata.viewport.onion_skin.enabled);
    onion_skin_object["mode"] = make_string_value(
        std::string(onion_skin_mode_json_key(project.editor_metadata.viewport.onion_skin.mode)));
    onion_skin_object["anchor"] =
        make_boolean_value(project.editor_metadata.viewport.onion_skin.anchor_to_zero);
    onion_skin_object["before"] =
        make_number_value(project.editor_metadata.viewport.onion_skin.before_count);
    onion_skin_object["after"] =
        make_number_value(project.editor_metadata.viewport.onion_skin.after_count);
    onion_skin_object["step"] =
        make_number_value(project.editor_metadata.viewport.onion_skin.step);
    viewport_object["onion_skin"] = make_object_value(std::move(onion_skin_object));
    Value::Object debug_overlay_object;
    if (const auto debug_overlay = viewport_object.find("debug_overlay");
        debug_overlay != viewport_object.end() && debug_overlay->second.is_object()) {
        debug_overlay_object = debug_overlay->second.as_object();
    }
    debug_overlay_object["bones"] =
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.bones);
    debug_overlay_object["ik"] =
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.ik_constraints);
    debug_overlay_object["path"] =
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.path_constraints);
    debug_overlay_object["physics"] =
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.physics_constraints);
    debug_overlay_object["meshes"] =
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.mesh_wireframes);
    debug_overlay_object["bounds"] =
        make_boolean_value(project.editor_metadata.viewport.debug_overlay.bounding_boxes);
    viewport_object["debug_overlay"] = make_object_value(std::move(debug_overlay_object));
    editor_object["viewport"] = make_object_value(std::move(viewport_object));
    // MAR-188. BOTH arms are load-bearing, and neither is a no-op.
    //
    // `$.editor.import_sources` already round-tripped verbatim through
    // `preserved_root` before this field existed, and the preserved copy is still
    // sitting in `editor_object` right now. Assigning without erasing leaves a
    // cleared provenance on disk forever; parsing without assigning silently
    // discards every in-memory edit and writes the STALE preserved copy instead,
    // which would make Save As worse than before the field existed. The
    // erase-on-empty idiom is the one already used for `constraint_edits`,
    // `parameter_model` and `atlas_packs` below.
    //
    // The same rule applies one level down: an `import_sources` whose `psd` is
    // disengaged serialises as an ABSENT key, not as `{}`.
    if (project.editor_metadata.import_sources.has_value() &&
        project.editor_metadata.import_sources->psd.has_value()) {
        editor_object["import_sources"] =
            build_import_sources_value(*project.editor_metadata.import_sources);
    } else {
        editor_object.erase("import_sources");
    }

    root["editor"] = make_object_value(std::move(editor_object));

    if (project.snap_settings.has_value()) {
        Value::Object snap_object = project.snap_settings->preserved_source.is_object()
            ? project.snap_settings->preserved_source.as_object()
            : Value::Object{};
        snap_object["world_grid_enabled"] =
            make_boolean_value(project.snap_settings->world_grid_enabled);
        snap_object["local_angle_enabled"] =
            make_boolean_value(project.snap_settings->local_angle_enabled);
        snap_object["absolute_scale_enabled"] =
            make_boolean_value(project.snap_settings->absolute_scale_enabled);
        snap_object["magnetic_vertex_enabled"] =
            make_boolean_value(project.snap_settings->magnetic_vertex_enabled);
        snap_object["world_grid_step"] =
            make_number_value(project.snap_settings->world_grid_step);
        snap_object["local_angle_step_degrees"] =
            make_number_value(project.snap_settings->local_angle_step_degrees);
        snap_object["absolute_scale_step"] =
            make_number_value(project.snap_settings->absolute_scale_step);
        root["snap"] = make_object_value(std::move(snap_object));
    } else {
        root.erase("snap");
    }

    if (!project.animation_edits.empty()) {
        root["animation_edits"] = build_animation_edits_value(project.animation_edits);
    } else {
        root.erase("animation_edits");
    }

    // MAR-184 adds the SEVENTH disjunct as well as the seventh argument. The
    // gate is what decides whether `timeline_edits` is written at all, so an
    // inherit-only project whose family is missing from this condition
    // serializes no overlay whatsoever -- a silent, save-side total loss with
    // every other assertion in the suite still green. It tests the effective
    // edits rather than the raw vector so that an all-empty inherit vector
    // cannot make a project that previously wrote no `timeline_edits` start
    // emitting an empty one.
    const bool has_inherit_timeline_edits = std::any_of(
        project.bone_inherit_timeline_edits.begin(),
        project.bone_inherit_timeline_edits.end(),
        [](const BoneInheritTimelineEdit& edit) { return !edit.keyframes.empty(); });
    if (!project.transform_timeline_edits.empty() ||
        has_inherit_timeline_edits ||
        !project.mesh_deform_timeline_edits.empty() ||
        !project.draw_order_timeline_edits.empty() ||
        !project.event_timeline_edits.empty() ||
        !project.slot_color_timeline_edits.empty() ||
        !project.slot_attachment_timeline_edits.empty()) {
        root["timeline_edits"] =
            build_timeline_edits_value(
                project.transform_timeline_edits,
                project.bone_inherit_timeline_edits,
                project.mesh_deform_timeline_edits,
                project.draw_order_timeline_edits,
                project.event_timeline_edits,
                project.slot_color_timeline_edits,
                project.slot_attachment_timeline_edits);
    } else {
        root.erase("timeline_edits");
    }

    // MAR-172: an optional projection of the three continuous families' lane
    // flags. The `erase` branch is what keeps a project with no opted-in lane
    // byte-identical to a pre-MAR-172 build's output.
    const auto lane_is_opted_in = [](const auto& edits) {
        return std::any_of(edits.begin(), edits.end(), [](const auto& edit) {
            return edit.loop_sync;
        });
    };
    if (lane_is_opted_in(project.transform_timeline_edits) ||
        lane_is_opted_in(project.slot_color_timeline_edits) ||
        lane_is_opted_in(project.mesh_deform_timeline_edits)) {
        root["loop_sync"] = build_loop_sync_value(
            project.transform_timeline_edits,
            project.mesh_deform_timeline_edits,
            project.slot_color_timeline_edits);
    } else {
        root.erase("loop_sync");
    }
    if (!project.mesh_weight_attachment_edits.empty()) {
        root["mesh_edits"] =
            build_mesh_weight_edits_value(project.mesh_weight_attachment_edits);
    } else {
        root.erase("mesh_edits");
    }
    // The lifecycle vector is part of this gate: a project whose only
    // constraint content is a tombstone has four empty upsert vectors, and
    // without the fifth clause its `constraint_edits` key -- and with it the
    // tombstone -- is erased on save.
    if (!project.ik_constraint_edits.empty() ||
        !project.path_constraint_edits.empty() ||
        !project.transform_constraint_edits.empty() ||
        !project.physics_constraint_edits.empty() ||
        !project.constraint_lifecycle_operations.empty()) {
        root["constraint_edits"] = build_constraint_edits_value(
                project.ik_constraint_edits,
                project.path_constraint_edits,
                project.transform_constraint_edits,
                project.physics_constraint_edits,
                project.constraint_lifecycle_operations);
    } else {
        root.erase("constraint_edits");
    }
    if (project.parameter_model.has_value() && !project.parameter_model->empty()) {
        root["parameter_model"] = build_parameter_model_value(*project.parameter_model);
    } else {
        root.erase("parameter_model");
    }
    if (!project.atlas_pack_definitions.empty()) {
        root["atlas_packs"] =
            build_atlas_pack_definitions_value(project.atlas_pack_definitions);
    } else {
        root.erase("atlas_packs");
    }

    return make_object_value(std::move(root));
}

std::string serialize_project_snapshot(const ProjectData& project) {
    return marrow::runtime::json::serialize_pretty(build_project_value(project));
}

} // namespace

std::string serialize_project(const ProjectData& project) {
    return serialize_project_snapshot(project);
}

} // namespace marrow::editor
