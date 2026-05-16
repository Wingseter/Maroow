#include "marrow/editor/agent_dispatch.hpp"

#include <string_view>

#include "shell_types.hpp"
#include "marrow/editor/project.hpp"

namespace marrow::editor::shell {

namespace json = marrow::runtime::json;

namespace {

AgentDispatchResult make_error(std::string message) {
    return {false, std::move(message), json::Value()};
}

AgentDispatchResult make_success(std::string message, json::Value delta = json::Value()) {
    return {true, std::move(message), std::move(delta)};
}

} // namespace

AgentDispatchResult AgentCommandDispatcher::dispatch(ShellState* state, const json::Value& cmd) {
    if (state == nullptr) {
        return make_error("Shell state is null.");
    }

    if (!cmd.is_object()) {
        return make_error("Command must be an object.");
    }

    const json::Value* op_val = json::find_member(cmd, "op");
    if (!op_val || !op_val->is_string()) {
        return make_error("Command must have a string 'op' field.");
    }

    const std::string_view op = op_val->as_string();

    if (op == "undo") {
        std::string label;
        if (state->command_stack.undo(state, &label)) {
            update_project_dirty_state(state);
            state->status_message = "Undone: " + label;
            return make_success(state->status_message);
        }
        return make_error("Nothing to undo.");
    }

    if (op == "redo") {
        std::string label;
        if (state->command_stack.redo(state, &label)) {
            update_project_dirty_state(state);
            state->status_message = "Redone: " + label;
            return make_success(state->status_message);
        }
        return make_error("Nothing to redo.");
    }

    if (op == "save") {
        if (!state->load_result || state->load_result.project == nullptr) {
            return make_error("No project loaded.");
        }
        if (save_project_file(state, false)) {
            state->status_message = "Project saved successfully.";
            return make_success(state->status_message);
        } else {
            return make_error("Failed to save project: " + state->error_message);
        }
    }

    if (op == "export_runtime") {
        if (!state->load_result || state->load_result.project == nullptr) {
            return make_error("No project loaded.");
        }
        
        const json::Value* args = json::find_member(cmd, "args");
        if (args && args->is_object()) {
            const json::Value* binary_val = json::find_member(*args, "binary");
            if (binary_val && binary_val->is_boolean()) {
                state->export_binary_output = binary_val->as_boolean();
            }
        }

        if (export_runtime_assets_file(state, false)) {
            state->status_message = "Project exported successfully to " + state->load_result.project->editor_metadata.export_directory.string();
            return make_success(state->status_message);
        } else {
            return make_error("Failed to export project: " + state->error_message);
        }
    }

    if (op == "set_transform") {
        const json::Value* args = json::find_member(cmd, "args");
        if (!args || !args->is_object()) return make_error("set_transform requires 'args' object.");

        const json::Value* anim_val = json::find_member(*args, "animation");
        const json::Value* bone_val = json::find_member(*args, "bone");
        const json::Value* channel_val = json::find_member(*args, "channel");
        const json::Value* time_val = json::find_member(*args, "time");
        
        if (!anim_val || !anim_val->is_string() || 
            !bone_val || !bone_val->is_string() ||
            !channel_val || !channel_val->is_string() ||
            !time_val || !time_val->is_number()) {
            return make_error("set_transform requires animation(str), bone(str), channel(str), time(num).");
        }

        std::string_view anim_name = anim_val->as_string();
        std::string_view bone_name = bone_val->as_string();
        std::string_view channel_str = channel_val->as_string();
        double time = time_val->as_number();

        TransformTimelineChannel channel;
        if (channel_str == "rotate") channel = TransformTimelineChannel::Rotate;
        else if (channel_str == "translate") channel = TransformTimelineChannel::Translate;
        else if (channel_str == "scale") channel = TransformTimelineChannel::Scale;
        else if (channel_str == "shear") channel = TransformTimelineChannel::Shear;
        else return make_error("Invalid channel. Must be rotate, translate, scale, or shear.");

        if (!state->load_result || state->load_result.project == nullptr) {
            return make_error("No project loaded.");
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        
        // Find bone index to verify
        auto bone_index_opt = state->load_result.skeleton_data->find_bone_index(bone_name);
        if (!bone_index_opt) {
            return make_error("Bone not found: " + std::string(bone_name));
        }

        // Find or create the edit
        TransformTimelineEdit* edit = state->load_result.project->find_transform_timeline_edit(anim_name, bone_name, channel);
        if (!edit) {
            state->load_result.project->transform_timeline_edits.push_back({
                std::string(anim_name), std::string(bone_name), channel, {}
            });
            edit = &state->load_result.project->transform_timeline_edits.back();
        }

        // Find or create keyframe at exact time (or insert)
        auto key_it = std::lower_bound(edit->keyframes.begin(), edit->keyframes.end(), time,
            [](const TransformKeyframeEdit& k, double t) { return k.time < t; });
        
        TransformKeyframeEdit* key = nullptr;
        if (key_it != edit->keyframes.end() && std::abs(key_it->time - time) < 1e-6) {
            key = &(*key_it);
        } else {
            key_it = edit->keyframes.insert(key_it, TransformKeyframeEdit{});
            key = &(*key_it);
            key->time = time;
            key->interpolation = marrow::runtime::Interpolation::linear();
        }

        if (channel == TransformTimelineChannel::Rotate) {
            const json::Value* angle_val = json::find_member(*args, "angle");
            if (angle_val && angle_val->is_number()) {
                key->angle = angle_val->as_number();
            } else return make_error("rotate channel requires 'angle' number.");
        } else {
            const json::Value* x_val = json::find_member(*args, "x");
            const json::Value* y_val = json::find_member(*args, "y");
            if (x_val && x_val->is_number()) key->x = x_val->as_number();
            if (y_val && y_val->is_number()) key->y = y_val->as_number();
        }

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply transform: " + state->error_message);
        }

        if (!record_action_from_snapshots(
                state, before, EditActionKind::AddKeyframe, 
                "Set transform keyframe via Agent", "Agent", 
                json::find_member(*args, "merge") ? json::find_member(*args, "merge")->as_boolean() : false)) {
            return make_error("No changes made.");
        }

        return make_success("Set transform keyframe successfully.");
    }

    if (op == "remove_transform_keyframe") {
        const json::Value* args = json::find_member(cmd, "args");
        if (!args || !args->is_object()) return make_error("remove_transform_keyframe requires 'args' object.");

        const json::Value* anim_val = json::find_member(*args, "animation");
        const json::Value* bone_val = json::find_member(*args, "bone");
        const json::Value* channel_val = json::find_member(*args, "channel");
        const json::Value* time_val = json::find_member(*args, "time");

        if (!anim_val || !anim_val->is_string() || 
            !bone_val || !bone_val->is_string() ||
            !channel_val || !channel_val->is_string() ||
            !time_val || !time_val->is_number()) {
            return make_error("remove_transform_keyframe requires animation, bone, channel, time.");
        }

        std::string_view anim_name = anim_val->as_string();
        std::string_view bone_name = bone_val->as_string();
        std::string_view channel_str = channel_val->as_string();
        double time = time_val->as_number();

        TransformTimelineChannel channel;
        if (channel_str == "rotate") channel = TransformTimelineChannel::Rotate;
        else if (channel_str == "translate") channel = TransformTimelineChannel::Translate;
        else if (channel_str == "scale") channel = TransformTimelineChannel::Scale;
        else if (channel_str == "shear") channel = TransformTimelineChannel::Shear;
        else return make_error("Invalid channel.");

        if (!state->load_result || state->load_result.project == nullptr) {
            return make_error("No project loaded.");
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        
        TransformTimelineEdit* edit = state->load_result.project->find_transform_timeline_edit(anim_name, bone_name, channel);
        if (!edit) return make_error("Timeline edit not found.");

        auto key_it = std::find_if(edit->keyframes.begin(), edit->keyframes.end(),
            [&](const TransformKeyframeEdit& k) { return std::abs(k.time - time) < 1e-6; });
        
        if (key_it == edit->keyframes.end()) return make_error("Keyframe not found at that time.");

        edit->keyframes.erase(key_it);

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to rebuild runtime: " + state->error_message);
        }

        if (!record_action_from_snapshots(
                state, before, EditActionKind::RemoveKeyframe, 
                "Remove transform keyframe via Agent", "Agent", false)) {
            return make_error("No changes made.");
        }

        return make_success("Removed transform keyframe successfully.");
    }
    
    if (op == "edit_ik_constraint") {
        const json::Value* args = json::find_member(cmd, "args");
        if (!args || !args->is_object()) return make_error("edit_ik_constraint requires 'args' object.");

        const json::Value* name_val = json::find_member(*args, "name");
        if (!name_val || !name_val->is_string()) return make_error("edit_ik_constraint requires 'name' string.");

        std::string_view name = name_val->as_string();

        if (!state->load_result || state->load_result.project == nullptr) {
            return make_error("No project loaded.");
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        
        IkConstraintEdit* edit = state->load_result.project->find_ik_constraint_edit(name);
        if (!edit) {
            // Find runtime constraint to populate defaults
            const marrow::runtime::IkConstraintData* runtime_constraint = nullptr;
            for (const auto& c : state->load_result.skeleton_data->ik_constraints()) {
                if (c.name == name) {
                    runtime_constraint = &c;
                    break;
                }
            }
            if (!runtime_constraint) {
                return make_error("IK constraint not found in runtime skeleton.");
            }

            IkConstraintEdit new_edit;
            new_edit.name = std::string(name);
            for (const std::size_t bone_index : runtime_constraint->bone_indices) {
                if (bone_index < state->load_result.skeleton_data->bones().size()) {
                    new_edit.bone_names.push_back(state->load_result.skeleton_data->bones()[bone_index].name);
                }
            }
            if (runtime_constraint->target_bone_index < state->load_result.skeleton_data->bones().size()) {
                new_edit.target_bone_name = state->load_result.skeleton_data->bones()[runtime_constraint->target_bone_index].name;
            }
            new_edit.mix = runtime_constraint->mix;
            new_edit.bend_positive = runtime_constraint->bend_positive;
            new_edit.softness = runtime_constraint->softness;
            new_edit.compress = runtime_constraint->compress;
            new_edit.stretch = runtime_constraint->stretch;

            state->load_result.project->ik_constraint_edits.push_back(std::move(new_edit));
            edit = &state->load_result.project->ik_constraint_edits.back();
        }

        if (const json::Value* target_val = json::find_member(*args, "target")) {
            if (target_val->is_string()) edit->target_bone_name = target_val->as_string();
            else if (target_val->type() != json::Value::Type::Null) return make_error("target must be string or null.");
        }

        if (const json::Value* bones_val = json::find_member(*args, "bone_names")) {
            if (bones_val->is_array()) {
                edit->bone_names.clear();
                for (const auto& b : bones_val->as_array()) {
                    if (b.is_string()) edit->bone_names.push_back(std::string(b.as_string()));
                }
            } else return make_error("bone_names must be an array of strings.");
        }

        if (const json::Value* mix_val = json::find_member(*args, "mix")) {
            if (mix_val->is_number()) edit->mix = mix_val->as_number();
            else if (mix_val->type() != json::Value::Type::Null) return make_error("mix must be number or null.");
        }

        if (const json::Value* bend_val = json::find_member(*args, "bend_positive")) {
            if (bend_val->type() == json::Value::Type::Boolean) edit->bend_positive = bend_val->as_boolean();
            else if (bend_val->type() != json::Value::Type::Null) return make_error("bend_positive must be bool or null.");
        }

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply IK constraint edit: " + state->error_message);
        }

        if (!record_action_from_snapshots(
                state, before, EditActionKind::EditProperty, 
                "Edit IK Constraint via Agent", "Agent", 
                json::find_member(*args, "merge") ? json::find_member(*args, "merge")->as_boolean() : false)) {
            return make_error("No changes made.");
        }

        return make_success("Edited IK constraint successfully.");
    }
    
    if (op == "scene.describe") {
        if (!state->load_result || state->load_result.project == nullptr) {
            return make_error("No project loaded.");
        }
        
        json::Value::Object scene_desc;
        scene_desc.emplace("path", json::Value(state->project_path.string(), {}));
        scene_desc.emplace("name", json::Value(state->load_result.project->editor_metadata.name, {}));
        scene_desc.emplace("bone_count", json::Value(static_cast<double>(state->load_result.skeleton_data->bones().size()), {}));
        scene_desc.emplace("animation_count", json::Value(static_cast<double>(state->load_result.skeleton_data->animations().size()), {}));
        
        return make_success("Scene described", json::Value(std::move(scene_desc), {}));
    }

    if (op == "bones.list") {
        if (!state->load_result || state->load_result.project == nullptr) {
            return make_error("No project loaded.");
        }
        
        json::Value::Array bones_arr;
        for (const auto& bone : state->load_result.skeleton_data->bones()) {
            bones_arr.push_back(json::Value(bone.name, {}));
        }
        
        return make_success("Bones listed", json::Value(std::move(bones_arr), {}));
    }

    if (op == "animation.list") {
        if (!state->load_result || state->load_result.project == nullptr) {
            return make_error("No project loaded.");
        }
        
        json::Value::Array anim_arr;
        for (const auto& anim : state->load_result.skeleton_data->animations()) {
            anim_arr.push_back(json::Value(anim.name, {}));
        }
        
        return make_success("Animations listed", json::Value(std::move(anim_arr), {}));
    }
    
    return make_error("Unknown operation: " + std::string(op));
}

} // namespace marrow::editor::shell
