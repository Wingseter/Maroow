#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "atomic_file_write.hpp"
#include "shell_constraints.hpp"
#include "shell_asset_watch.hpp"
#include "shell_agent_panel.hpp"
#include "shell_coalesced_edit.hpp"
#include "shell_derived_cache.hpp"
#include "shell_file_paths.hpp"
#include "shell_inspector.hpp"
#include "shell_project_panels.hpp"
#include "shell_parameters.hpp"
#include "shell_smoke_scenarios.hpp"
#include "shell_preferences.hpp"
#include "shell_recent_projects.hpp"
#include "shell_preview.hpp"
#include "shell_selection.hpp"
#include "shell_timeline.hpp"
#include "shell_weight_paint.hpp"
#include "shell_viewport_ui.hpp"
#include "shell_problems.hpp"
#include "marrow/editor/safe_fix.hpp"
#include "marrow/editor/problems_model.hpp"
#include "mesh_weight_model.hpp"
#include "shell_state.hpp"
#include "viewport_renderer.hpp"
#include "marrow/allocator.hpp"
#include "marrow/editor/module.hpp"
#include "marrow/editor/authoring.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/editor/recent_projects.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/profiler.hpp"

namespace marrow::editor::shell {

bool read_text_file(
    const std::filesystem::path& path,
    std::string* text_out,
    std::string* error_out) {
    std::ifstream stream(path);
    if (!stream) {
        if (error_out != nullptr) {
            *error_out = "Failed to open " + path.string();
        }
        return false;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        if (error_out != nullptr) {
            *error_out = "Failed to read " + path.string();
        }
        return false;
    }

    if (text_out != nullptr) {
        *text_out = buffer.str();
    }
    return true;
}

bool write_text_file(
    const std::filesystem::path& path,
    const std::string& text,
    std::string* error_out) {
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        if (error_out != nullptr) {
            *error_out = "Failed to open " + path.string() + " for writing";
        }
        return false;
    }

    stream << text;
    if (!stream.good()) {
        if (error_out != nullptr) {
            *error_out = "Failed to write " + path.string();
        }
        return false;
    }

    std::error_code error;
    std::filesystem::last_write_time(
        path,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(1),
        error);
    return true;
}

enum class SelectionSourceMutation {
    SeedTemporaryItems,
    ReorderSurvivors,
    RemoveTemporaryItems,
};

bool rewrite_selection_source_for_reload(
    const std::filesystem::path& skeleton_path,
    SelectionSourceMutation mutation,
    std::string* error_out) {
    std::string text;
    if (!read_text_file(skeleton_path, &text, error_out)) {
        return false;
    }

    auto parsed = marrow::runtime::json::parse_document(text, skeleton_path);
    if (!parsed || !parsed.document->root.is_object()) {
        if (error_out != nullptr) {
            *error_out = parsed.error.has_value()
                ? parsed.error->format()
                : "MAR-158 source mutation requires a JSON object root";
        }
        return false;
    }

    using marrow::runtime::json::SourceLocation;
    using marrow::runtime::json::Value;
    Value& root = parsed.document->root;
    Value* bones_value = marrow::runtime::json::find_member(root, "bones");
    Value* slots_value = marrow::runtime::json::find_member(root, "slots");
    Value* skins_value = marrow::runtime::json::find_member(root, "skins");
    if (bones_value == nullptr || !bones_value->is_array() ||
        slots_value == nullptr || !slots_value->is_array() ||
        skins_value == nullptr || !skins_value->is_object()) {
        if (error_out != nullptr) {
            *error_out = "MAR-158 source mutation requires bones, slots, and skins";
        }
        return false;
    }

    const auto find_named_array_item = [](Value::Array& values, std::string_view name) {
        return std::find_if(
            values.begin(),
            values.end(),
            [&](Value& value) {
                Value* name_value = marrow::runtime::json::find_member(value, "name");
                return name_value != nullptr && name_value->is_string() &&
                    name_value->as_string() == name;
            });
    };

    auto& bones = bones_value->as_array();
    auto& slots = slots_value->as_array();
    auto& root_object = root.as_object();
    if (mutation == SelectionSourceMutation::SeedTemporaryItems) {
        if (find_named_array_item(bones, "mar158_leaf") == bones.end()) {
            Value::Object leaf;
            leaf.emplace("name", Value(std::string("mar158_leaf"), SourceLocation{}));
            leaf.emplace("parent", Value(std::string("root"), SourceLocation{}));
            leaf.emplace("x", Value(24.0, SourceLocation{}));
            leaf.emplace("y", Value(-36.0, SourceLocation{}));
            bones.emplace_back(std::move(leaf), SourceLocation{});
        }

        Value::Object constraint;
        constraint.emplace("name", Value(std::string("mar158_temp_ik"), SourceLocation{}));
        Value::Array chain;
        chain.emplace_back(std::string("mar158_leaf"), SourceLocation{});
        constraint.emplace("bones", Value(std::move(chain), SourceLocation{}));
        constraint.emplace("target", Value(std::string("arm_l"), SourceLocation{}));
        constraint.emplace("mix", Value(1.0, SourceLocation{}));
        Value::Array constraints;
        constraints.emplace_back(std::move(constraint), SourceLocation{});
        root_object["ik"] = Value(std::move(constraints), SourceLocation{});
    } else if (mutation == SelectionSourceMutation::ReorderSurvivors) {
        const auto arm = find_named_array_item(bones, "arm_l");
        const auto body = find_named_array_item(slots, "body");
        if (arm == bones.end() || body == slots.end()) {
            if (error_out != nullptr) {
                *error_out = "MAR-159 source mutation could not find reorder targets";
            }
            return false;
        }

        const auto reordered_arm = find_named_array_item(bones, "arm_l");
        Value arm_value = std::move(*reordered_arm);
        bones.erase(reordered_arm);
        bones.push_back(std::move(arm_value));

        Value body_value = std::move(*body);
        slots.erase(body);
        slots.push_back(std::move(body_value));

        Value* animations = marrow::runtime::json::find_member(root, "animations");
        Value* attack = animations != nullptr
            ? marrow::runtime::json::find_member(*animations, "attack")
            : nullptr;
        Value* attack_bones = attack != nullptr
            ? marrow::runtime::json::find_member(*attack, "bones")
            : nullptr;
        Value* attack_arm = attack_bones != nullptr
            ? marrow::runtime::json::find_member(*attack_bones, "arm_l")
            : nullptr;
        Value* rotate = attack_arm != nullptr
            ? marrow::runtime::json::find_member(*attack_arm, "rotate")
            : nullptr;
        if (rotate == nullptr || !rotate->is_array() || rotate->as_array().size() < 2U) {
            if (error_out != nullptr) {
                *error_out = "MAR-158 source mutation could not find the attack peak";
            }
            return false;
        }
        auto& peak = rotate->as_array()[1U].as_object();
        peak["angle"] = Value(90.0, SourceLocation{});
    } else {
        const auto leaf = find_named_array_item(bones, "mar158_leaf");
        if (leaf == bones.end()) {
            if (error_out != nullptr) {
                *error_out = "MAR-159 source mutation could not find removal target";
            }
            return false;
        }
        bones.erase(leaf);

        Value* mage = marrow::runtime::json::find_member(*skins_value, "mage");
        if (mage == nullptr || !mage->is_object() ||
            mage->as_object().erase("arm_l") != 1U) {
            if (error_out != nullptr) {
                *error_out = "MAR-159 source mutation could not remove mage/arm_l";
            }
            return false;
        }
        root_object.erase("ik");
    }

    return write_text_file(
        skeleton_path,
        marrow::runtime::json::serialize_pretty_round_trip(root) + "\n",
        error_out);
}

bool validate_runtime_asset_hot_reload_smoke(const ShellState& source_state) {
    if (!source_state.load_result || source_state.load_result.project == nullptr) {
        std::cerr << "Hot-reload smoke requires a loaded project.\n";
        return false;
    }

    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "marrow_editor_hot_reload_smoke";
    std::error_code filesystem_error;
    std::filesystem::remove_all(temp_root, filesystem_error);
    filesystem_error.clear();
    std::filesystem::create_directories(temp_root, filesystem_error);
    if (filesystem_error) {
        std::cerr << "Hot-reload smoke could not create " << temp_root.string() << ".\n";
        return false;
    }

    const std::filesystem::path source_skeleton =
        source_state.load_result.project->resolved_skeleton_path();
    const std::vector<std::filesystem::path> source_atlases =
        source_state.load_result.project->resolved_atlas_paths();
    if (source_atlases.empty()) {
        std::cerr << "Hot-reload smoke requires at least one atlas.\n";
        return false;
    }

    const std::filesystem::path temp_skeleton = temp_root / source_skeleton.filename();
    const std::filesystem::path temp_atlas = temp_root / source_atlases.front().filename();
    std::filesystem::copy_file(
        source_skeleton,
        temp_skeleton,
        std::filesystem::copy_options::overwrite_existing,
        filesystem_error);
    if (filesystem_error) {
        std::cerr << "Hot-reload smoke could not copy " << source_skeleton.string() << ".\n";
        return false;
    }
    filesystem_error.clear();
    std::filesystem::copy_file(
        source_atlases.front(),
        temp_atlas,
        std::filesystem::copy_options::overwrite_existing,
        filesystem_error);
    if (filesystem_error) {
        std::cerr << "Hot-reload smoke could not copy " << source_atlases.front().string() << ".\n";
        return false;
    }

    std::string rewrite_error;
    if (!rewrite_selection_source_for_reload(
            temp_skeleton,
            SelectionSourceMutation::SeedTemporaryItems,
            &rewrite_error)) {
        std::cerr << rewrite_error << '\n';
        return false;
    }

    const std::filesystem::path temp_project = temp_root / "hot_reload_smoke.marrow";
    marrow::editor::MinimalProjectOptions project_options;
    project_options.project_path = temp_project;
    project_options.skeleton_path = temp_skeleton;
    project_options.atlas_paths = {temp_atlas};
    project_options.name = "Hot Reload Smoke";
    project_options.active_animation = "attack";
    project_options.preview_skins = {"default"};
    project_options.notes = "Generated by marrow_editor_shell hot-reload smoke validation.";
    const marrow::editor::ProjectData temp_project_data =
        marrow::editor::create_minimal_project(project_options);
    const auto save_result = marrow::editor::save_project(temp_project_data, temp_project);
    if (!save_result) {
        std::cerr << save_result.error->format() << '\n';
        return false;
    }

    ShellState hot_reload_state;
    hot_reload_state.project_path = temp_project;
    if (!reload_project(&hot_reload_state)) {
        std::cerr << hot_reload_state.error_message << '\n';
        return false;
    }

    const auto arm_index = hot_reload_state.load_result.skeleton_data->find_bone_index("arm_l");
    const auto body_slot_index =
        hot_reload_state.load_result.skeleton_data->find_slot_index("body");
    const auto leaf_index =
        hot_reload_state.load_result.skeleton_data->find_bone_index("mar158_leaf");
    const auto mage_skin_index =
        hot_reload_state.load_result.skeleton_data->find_skin_index("mage");
    const auto initial_arm_slot_index =
        hot_reload_state.load_result.skeleton_data->find_slot_index("arm_l");
    if (!arm_index.has_value() || !body_slot_index.has_value() ||
        !leaf_index.has_value() || !mage_skin_index.has_value() ||
        !initial_arm_slot_index.has_value() ||
        hot_reload_state.load_result.skeleton_data->find_attachment(
            *mage_skin_index,
            *initial_arm_slot_index,
            "mage_arm_l") == nullptr) {
        std::cerr << "Hot-reload smoke requires MAR-158 selection source identities.\n";
        return false;
    }
    (void)cached_timeline_tracks(&hot_reload_state);
    (void)cached_slot_attachments(&hot_reload_state, *initial_arm_slot_index);
    const std::uint64_t initial_timeline_cache_generation =
        hot_reload_state.timeline_track_cache.generation;
    const std::uint64_t initial_slot_cache_generation =
        hot_reload_state.slot_derived_cache.generation;

    const marrow::editor::BoneSelection selected_arm{"arm_l"};
    const marrow::editor::AttachmentSelection selected_mage_arm{
        "arm_l", "mage", "mage_arm_l"};
    const marrow::editor::BoneSelection selected_leaf{"mar158_leaf"};
    const marrow::editor::SlotSelection selected_body{"body"};
    const marrow::editor::ConstraintSelection selected_temp_ik{
        ConstraintKind::Ik, "mar158_temp_ik"};
    if (!hot_reload_state.selection.add_range(
            {selected_arm,
             selected_mage_arm,
             selected_leaf,
             selected_body,
             selected_temp_ik},
            selected_temp_ik)) {
        std::cerr << "Hot-reload smoke could not prepare mixed selection identities.\n";
        return false;
    }

    hot_reload_state.animation_state()->clear_tracks();
    hot_reload_state.animation_state()->set_animation(0, "idle", true, 0.0);
    hot_reload_state.animation_state()->update(0.5);
    hot_reload_state.selected_animation_name = "idle";
    hot_reload_state.timeline_time_seconds = 0.5;
    if (!apply_current_animation_state_to_preview(&hot_reload_state)) {
        std::cerr << hot_reload_state.error_message << '\n';
        return false;
    }
    hot_reload_state.animation_state()->set_animation(0, "attack", false, 0.2);
    hot_reload_state.animation_state()->update(0.1);
    hot_reload_state.selected_animation_name = "attack";
    hot_reload_state.timeline_time_seconds = 0.1;
    hot_reload_state.timeline_playing = true;

    if (!apply_current_animation_state_to_preview(&hot_reload_state)) {
        std::cerr << hot_reload_state.error_message << '\n';
        return false;
    }

    std::shared_ptr<marrow::runtime::TrackEntry> current =
        hot_reload_state.animation_state()->get_current(0);
    if (current == nullptr || current->mixing_from == nullptr ||
        current->animation_name != "attack" ||
        current->mixing_from->animation_name != "idle") {
        std::cerr << "Hot-reload smoke did not build the expected attack<-idle mix chain.\n";
        return false;
    }

    const double pre_reload_track_time = current->track_time;
    const double pre_reload_mix_time = current->mix_time;
    const double pre_reload_rotation =
        static_cast<double>(
            hot_reload_state.preview_skeleton()->bone_poses()[*arm_index].local_pose.rotation);
    if (std::abs(pre_reload_rotation - 15.0) > 1e-3) {
        std::cerr << "Hot-reload smoke expected the pre-reload mixed attack pose at 15 degrees.\n";
        return false;
    }

    hot_reload_state.hierarchy_selection_anchor = selected_arm;
    hot_reload_state.viewport_box_selection = ViewportBoxSelectionGesture{
        ImVec2(10.0f, 20.0f), ImVec2(80.0f, 90.0f), false, true};
    hot_reload_state.viewport_ffd_selection = ViewportFfdSelection{
        ViewportFfdSelectionScope{
            0U, std::nullopt, "source-display", "source-deform", 2U},
        {0U, 1U}};
    hot_reload_state.viewport_ffd_box_selection = ViewportFfdBoxSelectionGesture{
        hot_reload_state.viewport_ffd_selection->scope,
        ImVec2(12.0f, 22.0f),
        ImVec2(82.0f, 92.0f),
        false,
        true};
    if (!rewrite_selection_source_for_reload(
            temp_skeleton,
            SelectionSourceMutation::ReorderSurvivors,
            &rewrite_error)) {
        std::cerr << rewrite_error << '\n';
        return false;
    }

    const RuntimeAssetPollOutcome poll_outcome =
        poll_runtime_asset_changes(&hot_reload_state);
    if (poll_outcome != RuntimeAssetPollOutcome::Reloaded) {
        std::cerr << "Hot-reload smoke did not detect the modified skeleton file.\n";
        if (!hot_reload_state.error_message.empty()) {
            std::cerr << hot_reload_state.error_message << '\n';
        }
        return false;
    }

    const auto remapped_arm_index =
        hot_reload_state.load_result.skeleton_data->find_bone_index("arm_l");
    const auto remapped_body_slot_index =
        hot_reload_state.load_result.skeleton_data->find_slot_index("body");
    const auto reordered_active = hot_reload_state.selection.active();
    (void)cached_timeline_tracks(&hot_reload_state);
    if (remapped_arm_index.has_value()) {
        const auto remapped_arm_slot =
            hot_reload_state.load_result.skeleton_data->find_slot_index("arm_l");
        if (remapped_arm_slot.has_value()) {
            (void)cached_slot_attachments(&hot_reload_state, *remapped_arm_slot);
        }
    }
    if (!remapped_arm_index.has_value() || !remapped_body_slot_index.has_value() ||
        *remapped_arm_index == *arm_index ||
        *remapped_body_slot_index == *body_slot_index ||
        hot_reload_state.selection.items() !=
            std::vector<marrow::editor::SelectionItem>{
                selected_arm,
                selected_mage_arm,
                selected_leaf,
                selected_body,
                selected_temp_ik} ||
        reordered_active == nullptr ||
        *reordered_active != marrow::editor::SelectionItem(selected_temp_ik) ||
        hot_reload_state.viewport_ffd_selection.has_value() ||
        hot_reload_state.viewport_ffd_box_selection.has_value() ||
        hot_reload_state.viewport_box_selection.has_value() ||
        hot_reload_state.hierarchy_selection_anchor !=
            std::optional<marrow::editor::SelectionItem>(selected_arm) ||
        !hot_reload_state.load_result.skeleton_data
             ->find_bone_index("mar158_leaf")
             .has_value() ||
        !marrow::editor::selection_item_exists(
            selected_mage_arm,
            *hot_reload_state.load_result.skeleton_data) ||
        !marrow::editor::selection_item_exists(
            selected_temp_ik,
            *hot_reload_state.load_result.skeleton_data) ||
        hot_reload_state.timeline_track_cache.generation !=
            initial_timeline_cache_generation + 1U ||
        hot_reload_state.slot_derived_cache.generation !=
            initial_slot_cache_generation + 1U ||
        hot_reload_state.slot_derived_cache.runtime.get() !=
            hot_reload_state.load_result.skeleton_data.get()) {
        std::cerr << "Hot reload did not preserve exact selection and anchor identities across reorder.\n";
        return false;
    }

    current = hot_reload_state.animation_state()->get_current(0);
    if (current == nullptr || current->mixing_from == nullptr ||
        current->animation_name != "attack" ||
        current->mixing_from->animation_name != "idle") {
        std::cerr << "Hot-reload smoke lost the active mix chain after reload.\n";
        return false;
    }
    if (std::abs(current->track_time - pre_reload_track_time) > 1e-6 ||
        std::abs(current->mix_time - pre_reload_mix_time) > 1e-6) {
        std::cerr << "Hot-reload smoke did not preserve track and mix time across reload.\n";
        return false;
    }

    const double post_reload_rotation =
        static_cast<double>(
            hot_reload_state.preview_skeleton()
                ->bone_poses()[*remapped_arm_index]
                .local_pose.rotation);
    if (std::abs(post_reload_rotation - 22.5) > 1e-3) {
        std::cerr << "Hot-reload smoke did not sample the updated attack pose after reload.\n";
        return false;
    }

    hot_reload_state.animation_state()->update(1.0 / 60.0);
    hot_reload_state.timeline_time_seconds =
        hot_reload_state.animation_state()->get_current(0)->track_time;
    if (!apply_current_animation_state_to_preview(&hot_reload_state)) {
        std::cerr << hot_reload_state.error_message << '\n';
        return false;
    }
    if (hot_reload_state.animation_state()->get_current(0)->track_time <= pre_reload_track_time) {
        std::cerr << "Hot-reload smoke playback did not continue after reload.\n";
        return false;
    }

    hot_reload_state.hierarchy_selection_anchor = selected_leaf;
    if (!rewrite_selection_source_for_reload(
            temp_skeleton,
            SelectionSourceMutation::RemoveTemporaryItems,
            &rewrite_error)) {
        std::cerr << rewrite_error << '\n';
        return false;
    }
    if (poll_runtime_asset_changes(&hot_reload_state) !=
        RuntimeAssetPollOutcome::Reloaded) {
        std::cerr << "Hot reload did not adopt the source deletion stage.\n";
        return false;
    }

    const auto final_arm_index =
        hot_reload_state.load_result.skeleton_data->find_bone_index("arm_l");
    const auto final_body_slot_index =
        hot_reload_state.load_result.skeleton_data->find_slot_index("body");
    const auto surviving_active = hot_reload_state.selection.active();
    const auto alternate_mage_arm_skin =
        hot_reload_state.load_result.skeleton_data->find_skin_index("mage_arm");
    const auto arm_slot_index =
        hot_reload_state.load_result.skeleton_data->find_slot_index("arm_l");
    (void)cached_timeline_tracks(&hot_reload_state);
    if (arm_slot_index.has_value()) {
        (void)cached_slot_attachments(&hot_reload_state, *arm_slot_index);
    }
    if (!final_arm_index.has_value() || !final_body_slot_index.has_value() ||
        hot_reload_state.selection.items() !=
            std::vector<marrow::editor::SelectionItem>{selected_arm, selected_body} ||
        surviving_active == nullptr ||
        *surviving_active != marrow::editor::SelectionItem(selected_body) ||
        hot_reload_state.hierarchy_selection_anchor.has_value() ||
        hot_reload_state.load_result.skeleton_data
            ->find_bone_index("mar158_leaf")
            .has_value() ||
        marrow::editor::selection_item_exists(
            selected_mage_arm,
            *hot_reload_state.load_result.skeleton_data) ||
        marrow::editor::selection_item_exists(
            selected_temp_ik,
            *hot_reload_state.load_result.skeleton_data) ||
        !alternate_mage_arm_skin.has_value() || !arm_slot_index.has_value() ||
        hot_reload_state.load_result.skeleton_data->find_attachment(
            *alternate_mage_arm_skin,
            *arm_slot_index,
            "mage_arm_l") == nullptr ||
        hot_reload_state.timeline_track_cache.generation !=
            initial_timeline_cache_generation + 2U ||
        hot_reload_state.slot_derived_cache.generation !=
            initial_slot_cache_generation + 2U) {
        std::cerr << "Hot reload did not prune exact missing selection and anchor identities.\n";
        return false;
    }

    hot_reload_state.selection.add_range({selected_arm}, selected_arm);
    const ResolvedSelection remapped_arm = resolve_shell_selection(hot_reload_state);
    hot_reload_state.selection.add_range({selected_body}, selected_body);
    const ResolvedSelection remapped_body = resolve_shell_selection(hot_reload_state);
    if (remapped_arm.active_bone_index != final_arm_index ||
        remapped_body.active_slot_index != final_body_slot_index ||
        hot_reload_state.selection.items() !=
            std::vector<marrow::editor::SelectionItem>{selected_arm, selected_body}) {
        std::cerr << "Surviving selection names did not resolve to reordered runtime indices.\n";
        return false;
    }

    const std::vector<marrow::editor::SelectionItem> selection_before_project_reload =
        hot_reload_state.selection.items();
    const marrow::editor::SelectionItem active_before_project_reload =
        *hot_reload_state.selection.active();
    hot_reload_state.hierarchy_selection_anchor = selected_arm;
    hot_reload_state.viewport_box_selection = ViewportBoxSelectionGesture{
        ImVec2(30.0f, 40.0f), ImVec2(130.0f, 140.0f), true, true};
    hot_reload_state.viewport_ffd_selection = ViewportFfdSelection{
        ViewportFfdSelectionScope{
            0U, std::nullopt, "project-display", "project-deform", 2U},
        {0U, 1U}};
    hot_reload_state.viewport_ffd_box_selection = ViewportFfdBoxSelectionGesture{
        hot_reload_state.viewport_ffd_selection->scope,
        ImVec2(32.0f, 42.0f),
        ImVec2(132.0f, 142.0f),
        true,
        true};
    if (!reload_project(&hot_reload_state)) {
        std::cerr << "Project reload failed during derived-cache smoke.\n";
        return false;
    }
    (void)cached_timeline_tracks(&hot_reload_state);
    const auto reloaded_arm_slot =
        hot_reload_state.load_result.skeleton_data->find_slot_index("arm_l");
    if (reloaded_arm_slot.has_value()) {
        (void)cached_slot_attachments(&hot_reload_state, *reloaded_arm_slot);
    }
    if (hot_reload_state.selection.items() != selection_before_project_reload ||
        hot_reload_state.selection.active() == nullptr ||
        *hot_reload_state.selection.active() != active_before_project_reload ||
        hot_reload_state.viewport_ffd_selection.has_value() ||
        hot_reload_state.viewport_ffd_box_selection.has_value() ||
        hot_reload_state.viewport_box_selection.has_value() ||
        hot_reload_state.hierarchy_selection_anchor !=
            std::optional<marrow::editor::SelectionItem>(selected_arm) ||
        !reloaded_arm_slot.has_value() ||
        hot_reload_state.timeline_track_cache.generation !=
            initial_timeline_cache_generation + 3U ||
        hot_reload_state.slot_derived_cache.generation !=
            initial_slot_cache_generation + 3U) {
        std::cerr << "Project reload did not preserve surviving exact selection and anchor identities.\n";
        return false;
    }

    const auto stable_document = hot_reload_state.load_result.base_skeleton_document;
    const auto stable_runtime = hot_reload_state.load_result.skeleton_data;
    const auto stable_atlases = hot_reload_state.load_result.atlas_data;
    const std::vector<marrow::editor::SelectionItem> stable_selection_items =
        hot_reload_state.selection.items();
    const marrow::editor::SelectionItem stable_active_selection =
        *hot_reload_state.selection.active();
    const std::optional<marrow::editor::SelectionItem> stable_hierarchy_anchor =
        hot_reload_state.hierarchy_selection_anchor;
    const std::uint64_t stable_timeline_cache_generation =
        hot_reload_state.timeline_track_cache.generation;
    const std::uint64_t stable_slot_cache_generation =
        hot_reload_state.slot_derived_cache.generation;
    hot_reload_state.viewport_ffd_selection = ViewportFfdSelection{
        ViewportFfdSelectionScope{
            0U, std::nullopt, "failed-display", "failed-deform", 2U},
        {1U}};
    hot_reload_state.viewport_ffd_box_selection = ViewportFfdBoxSelectionGesture{
        hot_reload_state.viewport_ffd_selection->scope,
        ImVec2(34.0f, 44.0f),
        ImVec2(134.0f, 144.0f),
        true,
        true};
    if (!write_text_file(temp_skeleton, "{}\n", &rewrite_error)) {
        std::cerr << rewrite_error << '\n';
        return false;
    }
    const RuntimeAssetPollOutcome failed_poll =
        poll_runtime_asset_changes(&hot_reload_state);
    (void)cached_timeline_tracks(&hot_reload_state);
    (void)cached_slot_attachments(&hot_reload_state, *reloaded_arm_slot);
    if (failed_poll != RuntimeAssetPollOutcome::Failed ||
        hot_reload_state.load_result.base_skeleton_document.get() != stable_document.get() ||
        hot_reload_state.load_result.skeleton_data.get() != stable_runtime.get() ||
        hot_reload_state.load_result.atlas_data.size() != stable_atlases.size() ||
        hot_reload_state.selection.items() != stable_selection_items ||
        hot_reload_state.selection.active() == nullptr ||
        *hot_reload_state.selection.active() != stable_active_selection ||
        hot_reload_state.hierarchy_selection_anchor != stable_hierarchy_anchor ||
        !hot_reload_state.viewport_ffd_selection.has_value() ||
        hot_reload_state.viewport_ffd_selection->scope.display_attachment_name !=
            "failed-display" ||
        hot_reload_state.viewport_ffd_selection->scope.deform_attachment_name !=
            "failed-deform" ||
        hot_reload_state.viewport_ffd_selection->vertex_indices !=
            std::vector<std::size_t>{1U} ||
        !hot_reload_state.viewport_ffd_box_selection.has_value() ||
        hot_reload_state.viewport_ffd_box_selection->scope.display_attachment_name !=
            "failed-display" ||
        hot_reload_state.viewport_ffd_box_selection->start.x != 34.0f ||
        hot_reload_state.viewport_ffd_box_selection->current.y != 144.0f ||
        !hot_reload_state.viewport_ffd_box_selection->additive ||
        !hot_reload_state.viewport_ffd_box_selection->dragged ||
        hot_reload_state.error_message.empty() ||
        hot_reload_state.timeline_track_cache.generation !=
            stable_timeline_cache_generation ||
        hot_reload_state.slot_derived_cache.generation !=
            stable_slot_cache_generation ||
        hot_reload_state.slot_derived_cache.runtime.get() != stable_runtime.get()) {
        std::cerr << "Failed hot reload did not retain the previous source/runtime bundle.\n";
        return false;
    }
    for (std::size_t index = 0; index < stable_atlases.size(); ++index) {
        if (hot_reload_state.load_result.atlas_data[index].get() != stable_atlases[index].get()) {
            std::cerr << "Failed hot reload replaced a previously loaded atlas.\n";
            return false;
        }
    }

    return true;
}

namespace {

/**
 * @brief Scopes the process-global atomic-rename seam.
 *
 * HAZARD: the seam is shared by the settings writer and the project writer, and
 * `marrow_editor_shell` drives both. Never save preferences inside this scope.
 */
class ScopedRenameCallback {
public:
    explicit ScopedRenameCallback(marrow::editor::detail::RenameCallback callback) {
        marrow::editor::detail::set_preference_rename_callback_for_testing(
            std::move(callback));
    }

    ~ScopedRenameCallback() {
        marrow::editor::detail::set_preference_rename_callback_for_testing({});
    }

    ScopedRenameCallback(const ScopedRenameCallback&) = delete;
    ScopedRenameCallback& operator=(const ScopedRenameCallback&) = delete;
};

/** @brief Rewrites a `.mskl` so it parses as JSON but fails `load_skeleton_data`. */
bool break_skeleton_document_build(
    const std::filesystem::path& skeleton_path,
    std::string* error_out) {
    std::string text;
    if (!read_text_file(skeleton_path, &text, error_out)) {
        return false;
    }
    const std::string marker = "\"bones\": [";
    const std::size_t position = text.find(marker);
    if (position == std::string::npos) {
        if (error_out != nullptr) {
            *error_out = "Could not find a bones array in " + skeleton_path.string();
        }
        return false;
    }
    text.insert(
        position + marker.size(),
        "\n    {\"name\": \"mar180_orphan\", \"parent\": \"mar180_missing_parent\"},");
    return write_text_file(skeleton_path, text, error_out);
}

/** @brief Seeds a temp directory with the project and every asset it resolves. */
bool seed_shell_project_copy(
    const ShellState& source_state,
    const std::filesystem::path& directory,
    std::filesystem::path* project_out,
    std::filesystem::path* skeleton_out) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    error.clear();
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }

    const std::filesystem::path source_skeleton =
        source_state.load_result.project->resolved_skeleton_path();
    const std::vector<std::filesystem::path> source_atlases =
        source_state.load_result.project->resolved_atlas_paths();
    if (source_atlases.empty()) {
        return false;
    }
    const std::filesystem::path temp_skeleton = directory / source_skeleton.filename();
    std::filesystem::copy_file(
        source_skeleton,
        temp_skeleton,
        std::filesystem::copy_options::overwrite_existing,
        error);
    if (error) {
        return false;
    }
    std::vector<std::filesystem::path> temp_atlases;
    for (const std::filesystem::path& atlas : source_atlases) {
        const std::filesystem::path temp_atlas = directory / atlas.filename();
        error.clear();
        std::filesystem::copy_file(
            atlas,
            temp_atlas,
            std::filesystem::copy_options::overwrite_existing,
            error);
        if (error) {
            return false;
        }
        temp_atlases.push_back(temp_atlas);
        // The `.matl` names a texture beside it; copy it so the atlas resolves.
        const std::filesystem::path source_texture =
            atlas.parent_path() / "player_fixture.png";
        error.clear();
        std::filesystem::copy_file(
            source_texture,
            directory / source_texture.filename(),
            std::filesystem::copy_options::overwrite_existing,
            error);
    }

    const std::filesystem::path temp_project = directory / "mar180_shell.marrow";
    marrow::editor::MinimalProjectOptions project_options;
    project_options.project_path = temp_project;
    project_options.skeleton_path = temp_skeleton;
    project_options.atlas_paths = temp_atlases;
    project_options.name = "MAR-180 Shell";
    project_options.active_animation = "attack";
    project_options.preview_skins = {"default"};
    const marrow::editor::ProjectData temp_project_data =
        marrow::editor::create_minimal_project(project_options);
    if (!marrow::editor::save_project(temp_project_data, temp_project)) {
        return false;
    }

    if (project_out != nullptr) {
        *project_out = temp_project;
    }
    if (skeleton_out != nullptr) {
        *skeleton_out = temp_skeleton;
    }
    return true;
}

} // namespace

/**
 * @brief MAR-180 C2 -- a failed hot reload preserves the shell's session AND its
 *        cached preview pointers.
 *
 * The shell caches raw `preview_skeleton` / `animation_state` pointers into the
 * session's preview. Only the shell holds them, so only a shell-layer case can
 * prove a failed adoption left them usable rather than dangling. The model-layer
 * coherence case never dereferences them.
 */
bool validate_mar180_failed_hot_reload_shell_coherence(const ShellState& source_state) {
    if (!source_state.load_result || source_state.load_result.project == nullptr) {
        std::cerr << "MAR-180 C2 requires a loaded project.\n";
        return false;
    }
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "marrow_mar180_c2";
    std::filesystem::path temp_project;
    std::filesystem::path temp_skeleton;
    if (!seed_shell_project_copy(source_state, temp_root, &temp_project, &temp_skeleton)) {
        std::cerr << "MAR-180 C2 could not seed a project copy.\n";
        return false;
    }

    ShellState state;
    state.project_path = temp_project;
    if (!reload_project(&state)) {
        std::cerr << "MAR-180 C2 could not load the seeded project: "
                  << state.error_message << '\n';
        return false;
    }
    reset_runtime_asset_watch(&state);

    std::string skeleton_bytes;
    std::string file_error;
    if (!read_text_file(temp_skeleton, &skeleton_bytes, &file_error)) {
        std::cerr << "MAR-180 C2: " << file_error << '\n';
        return false;
    }

    const std::string serialized_before =
        marrow::editor::serialize_project(*state.load_result.project);
    const std::uint64_t project_revision_before = state.session.project_revision();
    const std::uint64_t runtime_revision_before = state.session.runtime_revision();
    const std::uint64_t preview_revision_before = state.session.preview_revision();
    const std::string selected_animation_before = state.selected_animation_name;
    const auto* document_before = state.load_result.base_skeleton_document.get();
    const auto* runtime_before = state.load_result.skeleton_data.get();

    if (!break_skeleton_document_build(temp_skeleton, &file_error)) {
        std::cerr << "MAR-180 C2: " << file_error << '\n';
        return false;
    }
    const RuntimeAssetPollOutcome outcome = poll_runtime_asset_changes(&state);
    if (outcome != RuntimeAssetPollOutcome::Failed) {
        std::cerr << "MAR-180 C2: a skeleton that parses but cannot build must fail "
                     "the hot reload.\n";
        return false;
    }
    if (state.error_message.empty() ||
        state.status_message != "Runtime asset hot-reload failed") {
        std::cerr << "MAR-180 C2: a failed hot reload must report an error and the "
                     "hot-reload failure status.\n";
        return false;
    }
    if (marrow::editor::serialize_project(*state.load_result.project) != serialized_before ||
        state.session.project_revision() != project_revision_before ||
        state.session.runtime_revision() != runtime_revision_before ||
        state.session.preview_revision() != preview_revision_before ||
        state.selected_animation_name != selected_animation_before ||
        state.load_result.base_skeleton_document.get() != document_before ||
        state.load_result.skeleton_data.get() != runtime_before) {
        std::cerr << "MAR-180 C2: a failed hot reload must leave the shell's session "
                     "exactly as it was.\n";
        return false;
    }
    if (state.preview_skeleton() == nullptr || state.animation_state() == nullptr ||
        state.animation_state()->get_current(0) == nullptr) {
        std::cerr << "MAR-180 C2: the shell's cached preview pointers must still be "
                     "USABLE after a failed hot reload. Committing the session's "
                     "runtime before the preview bind leaves them addressing freed "
                     "data, which only the shell layer can observe.\n";
        return false;
    }

    if (!write_text_file(temp_skeleton, skeleton_bytes, &file_error)) {
        std::cerr << "MAR-180 C2: " << file_error << '\n';
        return false;
    }
    if (!reload_project(&state)) {
        std::cerr << "MAR-180 C2: the shell must recover once the sources are valid "
                     "again: " << state.error_message << '\n';
        return false;
    }

    std::error_code ignored;
    std::filesystem::remove_all(temp_root, ignored);
    std::cout << "MAR-180 C2: a hot reload whose skeleton parses but cannot build "
                 "fails, reports the hot-reload status, leaves the shell's project, "
                 "three session revisions and source/runtime bundle untouched, keeps "
                 "the shell's cached preview pointers usable, and recovers on the next "
                 "reload.\n";
    return true;
}

/**
 * @brief MAR-180 C3 -- a failed save preserves the file, the dirty flag, and the
 *        user's only warning.
 *
 * `save_project_file` returns early on failure without clearing `project_dirty`.
 * Combined with the atomic write, "returned false" + "still dirty" + "bytes
 * unchanged" is exactly what a save that reported success over a truncated file
 * would fail. The rename seam is process-global, so it is RAII-scoped here and no
 * preference save happens inside that scope.
 */
bool validate_mar180_failed_shell_save_preserves_file(const ShellState& source_state) {
    if (!source_state.load_result || source_state.load_result.project == nullptr) {
        std::cerr << "MAR-180 C3 requires a loaded project.\n";
        return false;
    }
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "marrow_mar180_c3";
    std::filesystem::path temp_project;
    std::filesystem::path temp_skeleton;
    if (!seed_shell_project_copy(source_state, temp_root, &temp_project, &temp_skeleton)) {
        std::cerr << "MAR-180 C3 could not seed a project copy.\n";
        return false;
    }

    ShellState state;
    state.project_path = temp_project;
    if (!reload_project(&state)) {
        std::cerr << "MAR-180 C3 could not load the seeded project: "
                  << state.error_message << '\n';
        return false;
    }

    auto transaction = state.session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "MAR-180 C3 note",
        "mar180-c3",
        false,
        marrow::editor::EditImpact::Project});
    if (!transaction || transaction.project() == nullptr) {
        std::cerr << "MAR-180 C3 could not begin the seeding edit.\n";
        return false;
    }
    transaction.project()->editor_metadata.notes += " mar180-c3";
    if (!transaction.commit()) {
        std::cerr << "MAR-180 C3 could not commit the seeding edit.\n";
        return false;
    }
    update_project_dirty_state(&state);
    if (!state.project_dirty || !state.session.dirty()) {
        std::cerr << "MAR-180 C3 requires a dirty session before the save.\n";
        return false;
    }

    std::string previous_bytes;
    std::string file_error;
    if (!read_text_file(temp_project, &previous_bytes, &file_error)) {
        std::cerr << "MAR-180 C3: " << file_error << '\n';
        return false;
    }

    {
        const ScopedRenameCallback rename_failure(
            [](const std::filesystem::path&, const std::filesystem::path&) {
                return std::make_error_code(std::errc::permission_denied);
            });
        if (save_project_file(&state, true)) {
            std::cerr << "MAR-180 C3: an injected rename failure must fail the shell "
                         "save.\n";
            return false;
        }
    }

    if (state.error_message.empty() || state.status_message != "Project save failed") {
        std::cerr << "MAR-180 C3: a failed save must report an error and the save "
                     "failure status.\n";
        return false;
    }
    if (!state.project_dirty || !state.session.dirty()) {
        std::cerr << "MAR-180 C3: a failed save must leave the session dirty -- the "
                     "dirty flag is the user's last warning that the work on screen is "
                     "not on disk.\n";
        return false;
    }
    std::string current_bytes;
    if (!read_text_file(temp_project, &current_bytes, &file_error) ||
        current_bytes != previous_bytes) {
        std::cerr << "MAR-180 C3: a failed save must leave the project file "
                     "byte-for-byte unchanged.\n";
        return false;
    }
    if (!marrow::editor::load_project(temp_project)) {
        std::cerr << "MAR-180 C3: the preserved project must still OPEN.\n";
        return false;
    }

    if (!save_project_file(&state, true)) {
        std::cerr << "MAR-180 C3: the save must succeed once the seam is released: "
                  << state.error_message << '\n';
        return false;
    }
    if (state.project_dirty || state.session.dirty()) {
        std::cerr << "MAR-180 C3: a successful save must clear the dirty flag.\n";
        return false;
    }
    if (!marrow::editor::load_project(temp_project)) {
        std::cerr << "MAR-180 C3: the saved project must OPEN.\n";
        return false;
    }

    std::error_code ignored;
    std::filesystem::remove_all(temp_root, ignored);
    std::cout << "MAR-180 C3: an injected rename failure fails the shell save, reports "
                 "the save failure status, leaves the session dirty and the project "
                 "file byte-for-byte unchanged (and still openable), and the retry "
                 "after releasing the seam saves cleanly.\n";
    return true;
}

bool validate_animation_catalog_smoke(const std::filesystem::path& project_path) {
    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state)) {
        std::cerr << "Animation catalog smoke could not load the project: "
                  << state.error_message << '\n';
        return false;
    }

    const std::size_t initial_animation_count =
        state.load_result.skeleton_data->animations().size();
    if (initial_animation_count == 0U) {
        std::cerr << "Animation catalog smoke requires at least one base animation.\n";
        return false;
    }

    const auto unique_name = [&](std::string stem) {
        while (state.load_result.skeleton_data->find_animation(stem) != nullptr) {
            stem.push_back('_');
        }
        return stem;
    };
    const std::string created = unique_name("marrow_catalog_smoke");
    const std::string duplicated = unique_name(created + "_copy");
    const std::string renamed = unique_name(duplicated + "_renamed");
    std::size_t expected_undo_count = state.session.undo_count();

    if (!apply_animation_catalog_action(
            &state,
            AnimationCatalogAction::Create,
            {},
            created) ||
        state.load_result.skeleton_data->find_animation(created) == nullptr ||
        state.selected_animation_name != created ||
        state.load_result.skeleton_data->animations().size() != initial_animation_count + 1U ||
        state.session.undo_count() != ++expected_undo_count) {
        std::cerr << "Animation catalog smoke failed to create and select a clip.\n";
        return false;
    }

    if (!apply_animation_catalog_action(
            &state,
            AnimationCatalogAction::Duplicate,
            created,
            duplicated) ||
        state.load_result.skeleton_data->find_animation(duplicated) == nullptr ||
        state.selected_animation_name != duplicated ||
        state.load_result.skeleton_data->animations().size() != initial_animation_count + 2U ||
        state.session.undo_count() != ++expected_undo_count) {
        std::cerr << "Animation catalog smoke failed to duplicate and select a clip.\n";
        return false;
    }

    std::string queued_name;
    for (const auto& animation : state.load_result.skeleton_data->animations()) {
        if (animation.name != duplicated) {
            queued_name = animation.name;
            break;
        }
    }
    if (queued_name.empty() ||
        !state.session.set_queue(queued_name, 0.125, 0.05)) {
        std::cerr << "Animation catalog smoke could not stage a queued preview.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);

    if (!apply_animation_catalog_action(
            &state,
            AnimationCatalogAction::Rename,
            duplicated,
            renamed) ||
        state.load_result.skeleton_data->find_animation(duplicated) != nullptr ||
        state.load_result.skeleton_data->find_animation(renamed) == nullptr ||
        state.selected_animation_name != renamed ||
        !state.preview_queue_enabled ||
        state.preview_queued_animation_name != queued_name ||
        state.session.undo_count() != ++expected_undo_count) {
        std::cerr << "Animation catalog smoke failed to rename a clip or preserve its queue.\n";
        return false;
    }

    if (!undo_project_change(&state) ||
        state.load_result.skeleton_data->find_animation(duplicated) == nullptr ||
        state.load_result.skeleton_data->find_animation(renamed) != nullptr ||
        state.selected_animation_name != duplicated ||
        !state.preview_queue_enabled ||
        state.preview_queued_animation_name != queued_name ||
        state.session.undo_count() + 1U != expected_undo_count) {
        std::cerr << "Animation catalog smoke did not atomically undo its renamed preview.\n";
        return false;
    }
    if (!redo_project_change(&state) ||
        state.load_result.skeleton_data->find_animation(duplicated) != nullptr ||
        state.load_result.skeleton_data->find_animation(renamed) == nullptr ||
        state.selected_animation_name != renamed ||
        !state.preview_queue_enabled ||
        state.preview_queued_animation_name != queued_name ||
        state.session.undo_count() != expected_undo_count) {
        std::cerr << "Animation catalog smoke did not atomically redo its renamed preview.\n";
        return false;
    }

    if (!set_selected_animation(
            &state,
            queued_name,
            "Animation catalog smoke",
            false,
            true) ||
        !state.session.set_queue(renamed, 0.125, 0.05)) {
        std::cerr << "Animation catalog smoke could not queue the clip selected for deletion.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);

    if (!apply_animation_catalog_action(
            &state,
            AnimationCatalogAction::Delete,
            renamed) ||
        state.load_result.skeleton_data->find_animation(renamed) != nullptr ||
        state.selected_animation_name != queued_name ||
        state.preview_queue_enabled ||
        state.session.undo_count() != ++expected_undo_count) {
        std::cerr << "Animation catalog smoke failed to remove a deleted queued clip.\n";
        return false;
    }

    if (!undo_project_change(&state) ||
        state.load_result.skeleton_data->find_animation(renamed) == nullptr ||
        state.selected_animation_name != queued_name ||
        !state.preview_queue_enabled ||
        state.preview_queued_animation_name != renamed ||
        state.session.undo_count() + 1U != expected_undo_count) {
        std::cerr << "Animation catalog smoke did not restore a deleted preview queue on undo.\n";
        return false;
    }
    if (!redo_project_change(&state) ||
        state.load_result.skeleton_data->find_animation(renamed) != nullptr ||
        state.selected_animation_name != queued_name ||
        state.preview_queue_enabled ||
        state.session.undo_count() != expected_undo_count) {
        std::cerr << "Animation catalog smoke did not remove the preview queue again on redo.\n";
        return false;
    }

    while (state.load_result.skeleton_data->animations().size() > 1U) {
        const std::string animation_name = state.selected_animation_name.empty()
            ? state.load_result.skeleton_data->animations().front().name
            : state.selected_animation_name;
        if (!apply_animation_catalog_action(
                &state,
                AnimationCatalogAction::Delete,
                animation_name)) {
            std::cerr << "Animation catalog smoke could not delete '" << animation_name
                      << "' while reducing the catalog: " << state.error_message << '\n';
            return false;
        }
        ++expected_undo_count;
    }

    const std::string last_animation =
        state.load_result.skeleton_data->animations().front().name;
    const std::size_t edits_before_rejection =
        state.load_result.project->animation_edits.size();
    if (apply_animation_catalog_action(
            &state,
            AnimationCatalogAction::Delete,
            last_animation) ||
        state.error_message.find("last animation") == std::string::npos ||
        state.load_result.skeleton_data->animations().size() != 1U ||
        state.load_result.project->animation_edits.size() != edits_before_rejection ||
        state.session.undo_count() != expected_undo_count) {
        std::cerr << "Animation catalog smoke did not reject deletion of the last clip.\n";
        return false;
    }

    if (!undo_project_change(&state) ||
        state.load_result.skeleton_data->animations().size() != 2U ||
        state.session.undo_count() + 1U != expected_undo_count) {
        std::cerr << "Animation catalog smoke did not restore a deletion as one undo item.\n";
        return false;
    }
    return true;
}

bool validate_animation_duration_shell_smoke(
    const std::filesystem::path& project_path) {
    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) ||
        !set_selected_animation(
            &state, "aim", "Animation duration smoke", false, true)) {
        std::cerr << "Animation duration smoke could not load the aim clip.\n";
        return false;
    }

    const auto find_aim = [&]() {
        return state.session.runtime_data() != nullptr
            ? state.session.runtime_data()->find_animation("aim")
            : nullptr;
    };
    const auto duration_is = [&](double expected) {
        const auto* animation = find_aim();
        return animation != nullptr && animation->explicit_duration.has_value() &&
            std::abs(animation->duration() - expected) <= 1e-6;
    };
    const auto preview_states_equal = [](
        const marrow::editor::PreviewState& left,
        const marrow::editor::PreviewState& right) {
        if (left.animation_name != right.animation_name ||
            left.time_seconds != right.time_seconds || left.loop != right.loop ||
            left.playing != right.playing ||
            left.queue_enabled != right.queue_enabled ||
            left.queued_animation_name != right.queued_animation_name ||
            left.queue_delay != right.queue_delay ||
            left.mix_duration != right.mix_duration || left.reverse != right.reverse ||
            left.skin_names != right.skin_names ||
            left.slot_overrides.size() != right.slot_overrides.size() ||
            left.direct_parameter_values != right.direct_parameter_values ||
            left.active_expression != right.active_expression ||
            left.synthetic_amplitude != right.synthetic_amplitude ||
            left.synthetic_phoneme != right.synthetic_phoneme) {
            return false;
        }
        for (std::size_t index = 0U; index < left.slot_overrides.size(); ++index) {
            const auto& left_override = left.slot_overrides[index];
            const auto& right_override = right.slot_overrides[index];
            if (left_override.has_value() != right_override.has_value()) {
                return false;
            }
            if (left_override.has_value() &&
                (left_override->skin_index != right_override->skin_index ||
                 left_override->attachment_name != right_override->attachment_name)) {
                return false;
            }
        }
        return true;
    };

    const auto* initial_aim = find_aim();
    if (initial_aim == nullptr || !initial_aim->explicit_duration.has_value() ||
        std::abs(initial_aim->inferred_duration() - 0.5) > 1e-6 ||
        !duration_is(0.5)) {
        std::cerr << "Animation duration smoke requires the explicit aim fixture boundary.\n";
        return false;
    }

    state.session.clear_history();
    const std::string baseline_project =
        marrow::editor::serialize_project(*state.session.project());
    if (!begin_animation_duration_gesture(&state, "aim") ||
        !apply_animation_duration_gesture(&state, 0.8) ||
        !state.animation_duration_gesture.has_value() ||
        !state.session.transaction_active() || !duration_is(0.8) ||
        std::abs(timeline_preview_duration(state) - 0.8) > 1e-6 ||
        state.session.undo_count() != 0U || !state.project_dirty) {
        std::cerr << "Animation duration gesture did not update the live preview atomically.\n";
        return false;
    }
    if (!finish_animation_duration_gesture(&state, true) ||
        state.animation_duration_gesture.has_value() ||
        state.session.transaction_active() || state.session.undo_count() != 1U ||
        !state.session.dirty() || !duration_is(0.8)) {
        std::cerr << "Animation duration gesture did not commit one dirty history item.\n";
        return false;
    }
    const std::string extended_project =
        marrow::editor::serialize_project(*state.session.project());
    if (extended_project == baseline_project || !undo_project_change(&state) ||
        marrow::editor::serialize_project(*state.session.project()) != baseline_project ||
        !duration_is(0.5) || !redo_project_change(&state) ||
        marrow::editor::serialize_project(*state.session.project()) != extended_project ||
        !duration_is(0.8) || state.session.undo_count() != 1U) {
        std::cerr << "Animation duration undo/redo did not restore exact project boundaries.\n";
        return false;
    }

    if (!state.session.set_queue("attack", 0.0, std::nullopt)) {
        std::cerr << "Animation duration smoke could not stage a queued preview.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    if (!scrub_timeline_time(
            &state, 0.7, "Animation duration queue smoke", false)) {
        std::cerr << "Animation duration smoke could not seek the queued preview.\n";
        return false;
    }
    const auto current_before_queue_rebuild =
        state.animation_state() != nullptr
        ? state.animation_state()->get_current(0U)
        : nullptr;
    if (current_before_queue_rebuild == nullptr ||
        current_before_queue_rebuild->animation_name != "aim" ||
        !begin_animation_duration_gesture(&state, "aim") ||
        !apply_animation_duration_gesture(&state, 0.6)) {
        std::cerr << "Animation duration queue smoke could not start from the primary clip.\n";
        return false;
    }
    const auto current_after_queue_rebuild =
        state.animation_state() != nullptr
        ? state.animation_state()->get_current(0U)
        : nullptr;
    if (current_after_queue_rebuild == nullptr ||
        current_after_queue_rebuild->animation_name != "attack" ||
        std::abs(state.timeline_time_seconds - 0.7) > 1e-6) {
        std::cerr << "Animation duration live preview retained the old queue boundary.\n";
        return false;
    }
    (void)finish_animation_duration_gesture(&state, false);
    const auto current_after_queue_cancel =
        state.animation_state() != nullptr
        ? state.animation_state()->get_current(0U)
        : nullptr;
    if (!duration_is(0.8) || current_after_queue_cancel == nullptr ||
        current_after_queue_cancel->animation_name != "aim" ||
        state.session.undo_count() != 1U || !state.session.clear_queue()) {
        std::cerr << "Animation duration queue-boundary cancellation was not exact.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);

    if (!scrub_timeline_time(
            &state, 0.75, "Animation duration smoke", false) ||
        std::abs(state.timeline_time_seconds - 0.75) > 1e-6 ||
        !begin_animation_duration_gesture(&state, "aim") ||
        !apply_animation_duration_gesture(&state, 0.6) || !duration_is(0.6) ||
        std::abs(state.timeline_time_seconds - 0.6) > 1e-6 ||
        !finish_animation_duration_gesture(&state, true) ||
        state.session.undo_count() != 2U) {
        std::cerr << "Animation duration tail shrink did not clamp the live playhead.\n";
        return false;
    }
    const std::string shrunken_project =
        marrow::editor::serialize_project(*state.session.project());
    if (!undo_project_change(&state) || !duration_is(0.8) ||
        std::abs(state.timeline_time_seconds - 0.75) > 1e-6 ||
        !redo_project_change(&state) || !duration_is(0.6) ||
        std::abs(state.timeline_time_seconds - 0.6) > 1e-6 ||
        marrow::editor::serialize_project(*state.session.project()) != shrunken_project) {
        std::cerr << "Animation duration tail-shrink history lost its preview boundary.\n";
        return false;
    }

    const auto duration_tracks = build_timeline_tracks(
        *state.session.runtime_data(), *find_aim());
    if (duration_tracks.empty() || duration_tracks.front().key_times.empty()) {
        std::cerr << "Animation duration smoke could not stage selection invariants.\n";
        return false;
    }
    select_slot(&state, 0U, "Smoke", false);
    state.selected_timeline_track_id = duration_tracks.front().id;
    state.timeline_editor.selected_keys = {
        timeline_key_ref(duration_tracks.front(), 0U)};

    const std::string before_rejection =
        marrow::editor::serialize_project(*state.session.project());
    const marrow::editor::PreviewState preview_before_rejection =
        state.session.preview_state();
    const auto selected_bone_before = selected_bone_index(state);
    const auto selected_slot_before = selected_slot_index(state);
    const auto selected_track_before = state.selected_timeline_track_id;
    const auto selected_keys_before = state.timeline_editor.selected_keys;
    const std::size_t undo_before = state.session.undo_count();
    const std::size_t redo_before = state.session.redo_count();
    const bool dirty_before = state.session.dirty();
    const std::uint64_t project_revision_before = state.session.project_revision();
    const std::uint64_t runtime_revision_before = state.session.runtime_revision();
    const std::uint64_t preview_revision_before = state.session.preview_revision();

    if (!begin_animation_duration_gesture(&state, "aim") ||
        apply_animation_duration_gesture(&state, 0.49) ||
        state.animation_duration_gesture.has_value() ||
        state.session.transaction_active() ||
        marrow::editor::serialize_project(*state.session.project()) != before_rejection ||
        !preview_states_equal(
            state.session.preview_state(), preview_before_rejection) ||
        selected_bone_index(state) != selected_bone_before ||
        selected_slot_index(state) != selected_slot_before ||
        state.selected_timeline_track_id != selected_track_before ||
        state.timeline_editor.selected_keys != selected_keys_before ||
        state.session.undo_count() != undo_before ||
        state.session.redo_count() != redo_before ||
        state.session.dirty() != dirty_before ||
        state.session.project_revision() != project_revision_before ||
        state.session.runtime_revision() != runtime_revision_before ||
        state.session.preview_revision() != preview_revision_before ||
        !duration_is(0.6)) {
        std::cerr << "Rejected animation duration changed project, preview, selection, or history.\n";
        return false;
    }

    return true;
}

/**
 * @brief MAR-181 C8 -- the path modal's acceptance rule, UI-free.
 *
 * Table-driven against `resolve_choice` directly. Every row asserts BOTH the
 * returned path and whether the choice was acceptable, so deleting any single
 * branch of the rule flips exactly one row and leaves the other ten green.
 *
 * The last row is the one an over-eager "safety" edit would break: a SaveTarget
 * over an EXISTING file is a legitimate, deliberate overwrite that MAR-180 made
 * atomic, so it asserts ACCEPTANCE and merely carries an informational
 * diagnostic.
 */
bool validate_mar181_path_resolution_smoke() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar181_c8";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    const std::filesystem::path browse = root / "browse";
    const std::filesystem::path sibling = root / "sib";
    std::filesystem::create_directories(browse / "subdir", error);
    if (error) {
        std::cerr << "MAR-181 C8 could not create the browse tree.\n";
        return false;
    }
    error.clear();
    std::filesystem::create_directories(sibling, error);
    if (error) {
        std::cerr << "MAR-181 C8 could not create the sibling directory.\n";
        return false;
    }
    std::string file_error;
    if (!write_text_file(browse / "existing.marrow", "{}\n", &file_error)) {
        std::cerr << "MAR-181 C8: " << file_error << '\n';
        return false;
    }

    struct Row {
        const char* label;
        const char* typed;
        FilePathMode mode;
        bool expect_acceptable;
        std::filesystem::path expected_path;   // empty == not asserted
        const char* expect_diagnostic;         // nullptr == not asserted
    };

    const std::filesystem::path missing_parent = browse / "no_such_dir" / "x.marrow";
    const std::vector<Row> rows{
        {"empty name", "", FilePathMode::SaveTarget, false, {}, "Enter a file name."},
        {"whitespace-only name", "   \t ", FilePathMode::SaveTarget, false, {},
         "Enter a file name."},
        {"no extension appends the filter", "x", FilePathMode::SaveTarget, true,
         browse / "x.marrow", ""},
        {"wrong extension is rejected", "x.mskl", FilePathMode::SaveTarget, false, {},
         "Expected a .marrow file."},
        {"absolute typed path is used verbatim", nullptr, FilePathMode::SaveTarget, true,
         sibling / "y.marrow", ""},
        {"relative parent path is normalized", "../sib/y.marrow",
         FilePathMode::SaveTarget, true, sibling / "y.marrow", ""},
        {"OpenExisting on a missing file", "nope.marrow", FilePathMode::OpenExisting,
         false, {}, "That file does not exist."},
        {"OpenExisting on a directory", "subdir.marrow", FilePathMode::OpenExisting,
         false, {}, "That is a directory."},
        {"SaveTarget whose parent is missing", "no_such_dir/x.marrow",
         FilePathMode::SaveTarget, false, {}, "The destination folder does not exist."},
        {"SaveTarget that is an existing directory", "subdir.marrow",
         FilePathMode::SaveTarget, false, {}, "That is a directory."},
        {"SaveTarget over an existing file is ACCEPTED", "existing.marrow",
         FilePathMode::SaveTarget, true, browse / "existing.marrow",
         "Replaces the existing file."},
    };

    // Row 8 and row 10 need a directory whose name ends in the filter extension,
    // so the extension rule cannot reject them before the is_directory branch is
    // reached. That is the whole point of those two rows.
    error.clear();
    std::filesystem::create_directories(browse / "subdir.marrow", error);
    if (error) {
        std::cerr << "MAR-181 C8 could not create the extension-named directory.\n";
        return false;
    }

    const std::string absolute_typed = (sibling / "y.marrow").string();
    for (const Row& row : rows) {
        const std::string typed =
            row.typed != nullptr ? std::string(row.typed) : absolute_typed;
        const FilePathChoice choice =
            resolve_choice(browse, typed, row.mode, ".marrow");
        if (choice.acceptable != row.expect_acceptable) {
            std::cerr << "MAR-181 C8 row \"" << row.label << "\": expected "
                      << (row.expect_acceptable ? "acceptable" : "rejected")
                      << " but resolve_choice returned "
                      << (choice.acceptable ? "acceptable" : "rejected")
                      << " with diagnostic \"" << choice.diagnostic << "\".\n";
            return false;
        }
        if (!row.expected_path.empty() &&
            choice.path != row.expected_path.lexically_normal()) {
            std::cerr << "MAR-181 C8 row \"" << row.label << "\": expected path "
                      << row.expected_path.lexically_normal() << " but got "
                      << choice.path << ".\n";
            return false;
        }
        if (row.expect_diagnostic != nullptr &&
            choice.diagnostic != row.expect_diagnostic) {
            std::cerr << "MAR-181 C8 row \"" << row.label << "\": expected diagnostic \""
                      << row.expect_diagnostic << "\" but got \"" << choice.diagnostic
                      << "\".\n";
            return false;
        }
    }

    error.clear();
    std::filesystem::remove_all(root, error);
    std::cout << "MAR-181 C8: resolve_choice appends a missing extension, rejects a "
                 "wrong one without case folding, normalizes relative and honours "
                 "absolute typed paths, rejects a missing OpenExisting target, a "
                 "directory in either mode and a SaveTarget whose parent is absent, "
                 "and ACCEPTS a SaveTarget over an existing file with an "
                 "informational diagnostic.\n";
    return true;
}


/** @brief The six values a failed action must leave bit-identical. */
struct SessionSnapshot {
    std::string serialized;
    std::uint64_t project_revision{0U};
    std::uint64_t runtime_revision{0U};
    std::uint64_t preview_revision{0U};
    std::size_t undo_count{0U};
    bool dirty{false};

    bool operator==(const SessionSnapshot& other) const {
        return serialized == other.serialized &&
            project_revision == other.project_revision &&
            runtime_revision == other.runtime_revision &&
            preview_revision == other.preview_revision &&
            undo_count == other.undo_count && dirty == other.dirty;
    }
};

SessionSnapshot capture_session_snapshot(const ShellState& state) {
    SessionSnapshot snapshot;
    if (state.session.project() != nullptr) {
        snapshot.serialized = marrow::editor::serialize_project(*state.session.project());
    }
    snapshot.project_revision = state.session.project_revision();
    snapshot.runtime_revision = state.session.runtime_revision();
    snapshot.preview_revision = state.session.preview_revision();
    snapshot.undo_count = state.session.undo_count();
    snapshot.dirty = state.session.dirty();
    return snapshot;
}

/** @brief Copies the seeded assets only -- no `.marrow` -- for the New cases. */
bool seed_shell_asset_copy(
    const ShellState& source_state,
    const std::filesystem::path& directory,
    std::filesystem::path* skeleton_out,
    std::vector<std::filesystem::path>* atlases_out) {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    error.clear();
    std::filesystem::create_directories(directory, error);
    if (error) return false;

    const std::filesystem::path source_skeleton =
        source_state.load_result.project->resolved_skeleton_path();
    const std::vector<std::filesystem::path> source_atlases =
        source_state.load_result.project->resolved_atlas_paths();
    if (source_atlases.empty()) return false;

    const std::filesystem::path temp_skeleton = directory / source_skeleton.filename();
    error.clear();
    std::filesystem::copy_file(
        source_skeleton, temp_skeleton,
        std::filesystem::copy_options::overwrite_existing, error);
    if (error) return false;

    for (const std::filesystem::path& atlas : source_atlases) {
        const std::filesystem::path temp_atlas = directory / atlas.filename();
        error.clear();
        std::filesystem::copy_file(
            atlas, temp_atlas,
            std::filesystem::copy_options::overwrite_existing, error);
        if (error) return false;
        if (atlases_out != nullptr) atlases_out->push_back(temp_atlas);
        const std::filesystem::path source_texture =
            atlas.parent_path() / "player_fixture.png";
        error.clear();
        std::filesystem::copy_file(
            source_texture, directory / source_texture.filename(),
            std::filesystem::copy_options::overwrite_existing, error);
    }
    if (skeleton_out != nullptr) *skeleton_out = temp_skeleton;
    return true;
}

/**
 * @brief MAR-181 C7 -- a failed Open changes nothing, INCLUDING the shell's path.
 *
 * `save_project_file` writes `ShellState::project_path` while the agent save
 * writes `project()->source_path`; the two agree today only because nothing ever
 * moves the project. A shell that adopted the chosen path before knowing the
 * open succeeded would leave the toolbar's Save writing to a file the session
 * never loaded.
 *
 * `broken.marrow` is valid JSON naming a `.mskl` that does not exist -- exactly
 * the shape `json::load_document` ACCEPTS and `load_project` REJECTS. The case
 * therefore fails only if the shell adopts a project it could not materialize.
 */
bool validate_mar181_failed_open_preserves_shell(const ShellState& source_state) {
    if (!source_state.load_result || source_state.load_result.project == nullptr) {
        std::cerr << "MAR-181 C7 requires a loaded project.\n";
        return false;
    }
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "marrow_mar181_c7";
    std::filesystem::path temp_project;
    std::filesystem::path temp_skeleton;
    if (!seed_shell_project_copy(source_state, temp_root, &temp_project, &temp_skeleton)) {
        std::cerr << "MAR-181 C7 could not seed a project copy.\n";
        return false;
    }

    ShellState state;
    state.project_path = temp_project;
    if (!reload_project(&state)) {
        std::cerr << "MAR-181 C7 could not load the seeded project: "
                  << state.error_message << '\n';
        return false;
    }

    std::string project_text;
    std::string file_error;
    if (!read_text_file(temp_project, &project_text, &file_error)) {
        std::cerr << "MAR-181 C7: " << file_error << '\n';
        return false;
    }
    const std::string skeleton_name = temp_skeleton.filename().string();
    const std::size_t skeleton_position = project_text.find(skeleton_name);
    if (skeleton_position == std::string::npos) {
        std::cerr << "MAR-181 C7 could not find the skeleton reference to break.\n";
        return false;
    }
    project_text.replace(
        skeleton_position, skeleton_name.size(), "does_not_exist.mskl");
    const std::filesystem::path broken_project = temp_root / "broken.marrow";
    if (!write_text_file(broken_project, project_text, &file_error)) {
        std::cerr << "MAR-181 C7: " << file_error << '\n';
        return false;
    }
    // The premise of the case: the bytes PARSE. If they did not, C7 would be
    // testing a JSON syntax error rather than an unresolvable cross-reference,
    // and `load_project` would reject it for the wrong reason.
    if (!marrow::runtime::json::load_document(broken_project).document.has_value()) {
        std::cerr << "MAR-181 C7 requires broken.marrow to be VALID JSON -- the whole "
                     "case is that json::load_document accepts what load_project "
                     "rejects.\n";
        return false;
    }

    const SessionSnapshot before = capture_session_snapshot(state);
    const std::filesystem::path path_before = state.project_path;
    const marrow::runtime::Skeleton* preview_before = state.preview_skeleton();
    const marrow::runtime::AnimationState* animation_before = state.animation_state();

    PendingFileApplication pending;
    pending.action = FileAction::Open;
    pending.path = broken_project;
    state.pending_file_application = pending;
    if (apply_pending_file_action(&state)) {
        std::cerr << "MAR-181 C7: opening a project whose skeleton is missing must "
                     "FAIL.\n";
        return false;
    }
    if (state.error_message.empty() || state.status_message != "Project load failed") {
        std::cerr << "MAR-181 C7: a failed Open must report an error and the load "
                     "failure status; status was \"" << state.status_message << "\".\n";
        return false;
    }
    if (state.project_path != path_before) {
        std::cerr << "MAR-181 C7: a failed Open must NOT move the shell's project "
                     "path. It moved to " << state.project_path
                  << ", which would leave the toolbar's Save writing to a file the "
                     "session never loaded.\n";
        return false;
    }
    if (!(capture_session_snapshot(state) == before)) {
        std::cerr << "MAR-181 C7: a failed Open must leave the six-value session "
                     "snapshot bit-identical.\n";
        return false;
    }
    if (state.preview_skeleton() != preview_before ||
        state.animation_state() != animation_before ||
        state.preview_skeleton() == nullptr) {
        std::cerr << "MAR-181 C7: a failed Open must leave the shell's cached preview "
                     "pointers usable and unchanged.\n";
        return false;
    }

    pending.path = temp_project;
    state.pending_file_application = pending;
    if (!apply_pending_file_action(&state)) {
        std::cerr << "MAR-181 C7: opening the good project must succeed: "
                  << state.error_message << '\n';
        return false;
    }
    if (state.project_path != temp_project || state.session.dirty() ||
        state.load_result.skeleton_data == nullptr) {
        std::cerr << "MAR-181 C7: a successful Open must adopt the chosen path, land "
                     "clean, and materialize a skeleton.\n";
        return false;
    }
    if (state.status_message != "Opened " + temp_project.string()) {
        std::cerr << "MAR-181 C7: a successful Open must report the opened path; "
                     "status was \"" << state.status_message << "\".\n";
        return false;
    }

    std::error_code ignored;
    std::filesystem::remove_all(temp_root, ignored);
    std::cout << "MAR-181 C7: an Open of valid JSON naming a missing skeleton fails, "
                 "reports the load failure status, and leaves the shell's project "
                 "path, the six-value session snapshot and the cached preview "
                 "pointers untouched; the following Open of the good project adopts "
                 "the chosen path and lands clean.\n";
    return true;
}

/**
 * @brief MAR-181 C5 -- a cross-directory Save As moves the shell's path and
 *        keeps every asset resolving.
 *
 * The `project_path` equality is design §1.3's divergence and nothing else in
 * the suite checks it. The watch-list equality is MAR-180's identity-preserving
 * rebase observed at the SHELL layer: `current_runtime_asset_paths` is rebuilt
 * from `resolved_skeleton_path()` and each `resolved_atlas_paths()` entry, so
 * recomputing it after the move is free and comparing it is a cheap check that
 * the rebase did what it claims.
 *
 * Every "the project is good" clause reloads from disk via `load_project`.
 * `validate_project_for_save` takes no base document and cannot resolve a
 * cross-reference, so a ProjectSaveResult that is `ok` proves nothing, and
 * `json::load_document` proves only that the bytes parse.
 */
bool validate_mar181_save_as_moves_the_shell_path(const ShellState& source_state) {
    if (!source_state.load_result || source_state.load_result.project == nullptr) {
        std::cerr << "MAR-181 C5 requires a loaded project.\n";
        return false;
    }
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "marrow_mar181_c5";
    std::error_code error;
    std::filesystem::remove_all(temp_root, error);
    const std::filesystem::path directory_a = temp_root / "a";
    const std::filesystem::path directory_b = temp_root / "b";
    std::filesystem::path temp_project;
    if (!seed_shell_project_copy(source_state, directory_a, &temp_project, nullptr)) {
        std::cerr << "MAR-181 C5 could not seed a project copy.\n";
        return false;
    }
    error.clear();
    std::filesystem::create_directories(directory_b, error);
    if (error) {
        std::cerr << "MAR-181 C5 could not create the destination directory.\n";
        return false;
    }

    ShellState state;
    state.project_path = temp_project;
    if (!reload_project(&state)) {
        std::cerr << "MAR-181 C5 could not load the seeded project: "
                  << state.error_message << '\n';
        return false;
    }

    const std::vector<std::filesystem::path> watch_before =
        current_runtime_asset_paths(state);
    const std::filesystem::path original_skeleton =
        state.load_result.project->resolved_skeleton_path().lexically_normal();
    std::vector<std::filesystem::path> original_atlases;
    for (const std::filesystem::path& atlas :
         state.load_result.project->resolved_atlas_paths()) {
        original_atlases.push_back(atlas.lexically_normal());
    }

    const std::filesystem::path moved = directory_b / "moved.marrow";
    if (!apply_save_as(&state, moved)) {
        std::cerr << "MAR-181 C5: the cross-directory Save As must succeed: "
                  << state.error_message << '\n';
        return false;
    }
    if (state.project_path != moved) {
        std::cerr << "MAR-181 C5: Save As must move the SHELL's project path to "
                  << moved << "; it is " << state.project_path
                  << ". The toolbar's Save writes ShellState::project_path, so a "
                     "Save As that moved only the project's source_path would keep "
                     "saving to the old file -- and, after MAR-180, rebase the "
                     "references back to the old directory on the way.\n";
        return false;
    }
    if (state.session.project() == nullptr ||
        state.session.project()->source_path != moved) {
        std::cerr << "MAR-181 C5: the session's source_path and the shell's "
                     "project_path must AGREE after a Save As.\n";
        return false;
    }
    if (state.project_dirty || state.session.dirty()) {
        std::cerr << "MAR-181 C5: a successful Save As must land clean.\n";
        return false;
    }

    const marrow::editor::ProjectLoadResult reloaded = marrow::editor::load_project(moved);
    if (!reloaded || reloaded.project == nullptr || reloaded.skeleton_data == nullptr) {
        std::cerr << "MAR-181 C5: the moved project must OPEN -- a passing save() "
                     "proves nothing, because validate_project_for_save cannot "
                     "resolve a cross-reference.\n";
        return false;
    }
    if (reloaded.project->resolved_skeleton_path().lexically_normal() !=
        original_skeleton) {
        std::cerr << "MAR-181 C5: the moved project's skeleton must resolve to the "
                     "ORIGINAL file " << original_skeleton << "; it resolves to "
                  << reloaded.project->resolved_skeleton_path().lexically_normal()
                  << ".\n";
        return false;
    }
    const std::vector<std::filesystem::path> reloaded_atlases =
        reloaded.project->resolved_atlas_paths();
    if (reloaded_atlases.size() != original_atlases.size()) {
        std::cerr << "MAR-181 C5: the moved project lost an atlas reference.\n";
        return false;
    }
    for (std::size_t index = 0; index < original_atlases.size(); ++index) {
        if (reloaded_atlases[index].lexically_normal() != original_atlases[index]) {
            std::cerr << "MAR-181 C5: atlas " << index << " resolves to "
                      << reloaded_atlases[index].lexically_normal() << " rather than "
                      << original_atlases[index] << ".\n";
            return false;
        }
    }

    const std::vector<std::filesystem::path> watch_after =
        current_runtime_asset_paths(state);
    if (watch_after.size() != watch_before.size()) {
        std::cerr << "MAR-181 C5: the runtime asset watch list changed SIZE across a "
                     "Save As: " << join_paths(watch_before) << " -> "
                  << join_paths(watch_after) << ".\n";
        return false;
    }
    for (std::size_t index = 0; index < watch_before.size(); ++index) {
        if (watch_before[index] != watch_after[index]) {
            std::cerr << "MAR-181 C5: the runtime asset watch list is not "
                         "element-wise equal across a Save As. MAR-180's rebase is "
                         "identity-preserving, so entry " << index << " must still be "
                      << watch_before[index] << "; it is " << watch_after[index]
                      << ".\n";
            return false;
        }
    }

    if (!std::filesystem::exists(temp_project) ||
        !marrow::editor::load_project(temp_project)) {
        std::cerr << "MAR-181 C5: a Save As must leave the ORIGINAL project in place "
                     "and still openable.\n";
        return false;
    }

    error.clear();
    std::filesystem::remove_all(temp_root, error);
    std::cout << "MAR-181 C5: a cross-directory Save As moves ShellState::project_path "
                 "and the session's source_path together, lands clean, produces a "
                 "project that RELOADS from disk with every asset resolving to the "
                 "original file, leaves the runtime asset watch list element-wise "
                 "equal, and leaves the original project openable.\n";
    return true;
}

/**
 * @brief MAR-181 C6 -- a failed Save As leaves the shell pointing where it was.
 *
 * The failure is INJECTED at the process-global atomic-rename seam -- the only
 * thing between a fully written temporary and the destination -- so it is real,
 * not simulated. The seam is shared with the settings writer, so it is scoped by
 * RAII and no preference save happens inside it.
 */
bool validate_mar181_failed_save_as_preserves_shell_path(const ShellState& source_state) {
    if (!source_state.load_result || source_state.load_result.project == nullptr) {
        std::cerr << "MAR-181 C6 requires a loaded project.\n";
        return false;
    }
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "marrow_mar181_c6";
    std::error_code error;
    std::filesystem::remove_all(temp_root, error);
    const std::filesystem::path directory_a = temp_root / "a";
    const std::filesystem::path directory_b = temp_root / "b";
    std::filesystem::path project_a;
    std::filesystem::path project_b;
    if (!seed_shell_project_copy(source_state, directory_a, &project_a, nullptr) ||
        !seed_shell_project_copy(source_state, directory_b, &project_b, nullptr)) {
        std::cerr << "MAR-181 C6 could not seed the two project copies.\n";
        return false;
    }

    ShellState state;
    state.project_path = project_a;
    if (!reload_project(&state)) {
        std::cerr << "MAR-181 C6 could not load the seeded project: "
                  << state.error_message << '\n';
        return false;
    }

    auto transaction = state.session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "MAR-181 C6 note",
        "mar181-c6",
        false,
        marrow::editor::EditImpact::Project});
    if (!transaction || transaction.project() == nullptr) {
        std::cerr << "MAR-181 C6 could not begin the seeding edit.\n";
        return false;
    }
    transaction.project()->editor_metadata.notes += " mar181-c6";
    if (!transaction.commit()) {
        std::cerr << "MAR-181 C6 could not commit the seeding edit.\n";
        return false;
    }
    update_project_dirty_state(&state);
    if (!state.project_dirty || !state.session.dirty()) {
        std::cerr << "MAR-181 C6 requires a dirty session before the Save As.\n";
        return false;
    }

    std::string previous_bytes;
    std::string file_error;
    if (!read_text_file(project_b, &previous_bytes, &file_error)) {
        std::cerr << "MAR-181 C6: " << file_error << '\n';
        return false;
    }
    const std::filesystem::path path_before = state.project_path;

    {
        const ScopedRenameCallback rename_failure(
            [](const std::filesystem::path&, const std::filesystem::path&) {
                return std::make_error_code(std::errc::permission_denied);
            });
        if (apply_save_as(&state, project_b)) {
            std::cerr << "MAR-181 C6: an injected rename failure must fail the "
                         "Save As.\n";
            return false;
        }
    }

    if (state.project_path != path_before) {
        std::cerr << "MAR-181 C6: a FAILED Save As must not move the shell's project "
                     "path. It moved to " << state.project_path << " rather than "
                     "staying at " << path_before << ".\n";
        return false;
    }
    if (state.session.project() == nullptr ||
        state.session.project()->source_path != path_before) {
        std::cerr << "MAR-181 C6: a failed Save As must leave the session's "
                     "source_path where it was.\n";
        return false;
    }
    if (!state.project_dirty || !state.session.dirty()) {
        std::cerr << "MAR-181 C6: a failed Save As must leave the session dirty.\n";
        return false;
    }
    if (state.status_message != "Project save failed" || state.error_message.empty()) {
        std::cerr << "MAR-181 C6: a failed Save As must report the save failure "
                     "status; status was \"" << state.status_message << "\".\n";
        return false;
    }
    std::string current_bytes;
    if (!read_text_file(project_b, &current_bytes, &file_error) ||
        current_bytes != previous_bytes) {
        std::cerr << "MAR-181 C6: a failed Save As must leave the DESTINATION file "
                     "byte-for-byte unchanged.\n";
        return false;
    }
    if (!marrow::editor::load_project(project_b)) {
        std::cerr << "MAR-181 C6: the preserved destination must still OPEN.\n";
        return false;
    }
    if (!marrow::editor::load_project(project_a)) {
        std::cerr << "MAR-181 C6: the source project must still OPEN.\n";
        return false;
    }

    if (!apply_save_as(&state, project_b)) {
        std::cerr << "MAR-181 C6: the Save As must succeed once the seam is "
                     "released: " << state.error_message << '\n';
        return false;
    }
    if (state.project_path != project_b || state.project_dirty ||
        state.session.dirty()) {
        std::cerr << "MAR-181 C6: the retried Save As must move the shell's path and "
                     "land clean.\n";
        return false;
    }
    if (!marrow::editor::load_project(project_b)) {
        std::cerr << "MAR-181 C6: the newly saved project must OPEN.\n";
        return false;
    }

    error.clear();
    std::filesystem::remove_all(temp_root, error);
    std::cout << "MAR-181 C6: an injected rename failure fails the Save As, leaves the "
                 "shell's project path and the session's source_path where they were, "
                 "keeps the session dirty, leaves the destination byte-for-byte "
                 "unchanged and still openable, and the retry after releasing the "
                 "seam moves the path and reloads from disk.\n";
    return true;
}

/**
 * @brief MAR-181 C4 -- New writes nothing, starts dirty, and is only real after
 *        a Save.
 *
 * `exists(target) == false` is checked against the FILESYSTEM, not a flag, so it
 * can only hold if no code path wrote it. MAR-180 made `create` dirty-from-birth
 * on purpose -- `saved_serialized_project` empty, `project_dirty` true -- and
 * `reload_project` hardcoded `project_dirty = false`, which is correct for open
 * and reload and WRONG for New. Reusing the reset block verbatim would paint the
 * session clean over a file that does not exist, destroying exactly the warning
 * MAR-180 built.
 */
bool validate_mar181_new_project_writes_nothing(const ShellState& source_state) {
    if (!source_state.load_result || source_state.load_result.project == nullptr) {
        std::cerr << "MAR-181 C4 requires a loaded project.\n";
        return false;
    }
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "marrow_mar181_c4";
    std::error_code error;
    std::filesystem::remove_all(temp_root, error);
    const std::filesystem::path base_directory = temp_root / "base";
    const std::filesystem::path fresh_directory = temp_root / "fresh";
    std::filesystem::path base_project;
    if (!seed_shell_project_copy(source_state, base_directory, &base_project, nullptr)) {
        std::cerr << "MAR-181 C4 could not seed the base project.\n";
        return false;
    }
    std::filesystem::path fresh_skeleton;
    std::vector<std::filesystem::path> fresh_atlases;
    if (!seed_shell_asset_copy(
            source_state, fresh_directory, &fresh_skeleton, &fresh_atlases) ||
        fresh_atlases.empty()) {
        std::cerr << "MAR-181 C4 could not seed the rig assets.\n";
        return false;
    }

    ShellState state;
    state.project_path = base_project;
    if (!reload_project(&state)) {
        std::cerr << "MAR-181 C4 could not load the base project: "
                  << state.error_message << '\n';
        return false;
    }

    const std::filesystem::path target = fresh_directory / "new_project.marrow";
    if (std::filesystem::exists(target)) {
        std::cerr << "MAR-181 C4: the target must not exist before New runs.\n";
        return false;
    }
    if (!validate_new_project_sources(fresh_skeleton, fresh_atlases).empty()) {
        std::cerr << "MAR-181 C4: the seeded rig must pass New's validation pass.\n";
        return false;
    }

    PendingFileApplication pending;
    pending.action = FileAction::New;
    pending.path = target;
    pending.skeleton_path = fresh_skeleton;
    pending.atlas_paths = fresh_atlases;
    state.pending_file_application = pending;
    if (!apply_pending_file_action(&state)) {
        std::cerr << "MAR-181 C4: New must succeed on a rig that loads: "
                  << state.error_message << '\n';
        return false;
    }

    if (std::filesystem::exists(target)) {
        std::cerr << "MAR-181 C4: New must write NOTHING. " << target
                  << " exists on disk, so some code path saved a project the user "
                     "has not asked to save.\n";
        return false;
    }
    if (!state.session.dirty() || !state.project_dirty) {
        std::cerr << "MAR-181 C4: a New project must be dirty from birth -- both the "
                     "session's dirty() and the shell's project_dirty. session.dirty()="
                  << (state.session.dirty() ? "true" : "false")
                  << " project_dirty=" << (state.project_dirty ? "true" : "false")
                  << ". A clean flag over a file that does not exist is the exact "
                     "warning MAR-180 built and reload_project's hardcoded "
                     "project_dirty=false would destroy.\n";
        return false;
    }
    if (state.session.undo_count() != 0U || state.session.redo_count() != 0U) {
        std::cerr << "MAR-181 C4: a New project must start with empty history; "
                     "undo=" << state.session.undo_count()
                  << " redo=" << state.session.redo_count() << ".\n";
        return false;
    }
    if (state.project_path != target) {
        std::cerr << "MAR-181 C4: New must adopt the chosen target as the shell's "
                     "project path.\n";
        return false;
    }
    if (state.load_result.skeleton_data == nullptr || state.preview_skeleton() == nullptr) {
        std::cerr << "MAR-181 C4: New must materialize a rig and bind the preview.\n";
        return false;
    }
    if (state.status_message != "New project (unsaved): " + target.string()) {
        std::cerr << "MAR-181 C4: New must report that the project is unsaved; status "
                     "was \"" << state.status_message << "\".\n";
        return false;
    }

    if (!save_project_file(&state, true)) {
        std::cerr << "MAR-181 C4: the explicit Save must succeed: "
                  << state.error_message << '\n';
        return false;
    }
    if (!std::filesystem::exists(target)) {
        std::cerr << "MAR-181 C4: the explicit Save must create the target.\n";
        return false;
    }
    if (state.session.dirty() || state.project_dirty) {
        std::cerr << "MAR-181 C4: the explicit Save must clear the dirty flag.\n";
        return false;
    }
    const marrow::editor::ProjectLoadResult reloaded =
        marrow::editor::load_project(target);
    if (!reloaded || reloaded.skeleton_data == nullptr) {
        std::cerr << "MAR-181 C4: the saved New project must RELOAD from disk with a "
                     "materialized skeleton -- a passing save() proves nothing.\n";
        return false;
    }

    // --- The failure half: a New whose skeleton does not exist. -------------
    const SessionSnapshot before = capture_session_snapshot(state);
    const std::filesystem::path path_before = state.project_path;
    const std::filesystem::path missing_skeleton =
        fresh_directory / "does_not_exist.mskl";
    if (validate_new_project_sources(missing_skeleton, fresh_atlases).empty()) {
        std::cerr << "MAR-181 C4: New's validation pass must reject a skeleton that "
                     "does not exist, BEFORE a session is constructed.\n";
        return false;
    }
    PendingFileApplication broken = pending;
    broken.skeleton_path = missing_skeleton;
    broken.path = fresh_directory / "second_project.marrow";
    state.pending_file_application = broken;
    if (apply_pending_file_action(&state)) {
        std::cerr << "MAR-181 C4: a New against a missing rig must FAIL.\n";
        return false;
    }
    if (state.error_message.empty() || state.status_message != "New project failed") {
        std::cerr << "MAR-181 C4: a failed New must report an error and the New "
                     "failure status; status was \"" << state.status_message << "\".\n";
        return false;
    }
    if (state.project_path != path_before) {
        std::cerr << "MAR-181 C4: a failed New must not move the shell's project "
                     "path.\n";
        return false;
    }
    if (!(capture_session_snapshot(state) == before)) {
        std::cerr << "MAR-181 C4: a failed New must leave the six-value session "
                     "snapshot bit-identical.\n";
        return false;
    }
    if (!std::filesystem::exists(target) ||
        std::filesystem::exists(broken.path)) {
        std::cerr << "MAR-181 C4: a failed New must touch no file on disk.\n";
        return false;
    }

    error.clear();
    std::filesystem::remove_all(temp_root, error);
    std::cout << "MAR-181 C4: New writes NOTHING to disk, lands dirty-from-birth in "
                 "both the session and the shell with empty history and the chosen "
                 "target as the shell's path, becomes a real file only after an "
                 "explicit Save that RELOADS from disk, and a New against a missing "
                 "rig fails without moving the path, the six-value snapshot, or any "
                 "file.\n";
    return true;
}

namespace {

/**
 * @brief How ImGui derives the id of the widget a label names.
 *
 * MEASURED against imgui_widgets.cpp, not assumed. `BeginMenuBar` pushes
 * `"##MenuBar"` onto the id stack (imgui.cpp), so a menu-bar menu's id is
 * seeded through it. `MenuItemEx` does `PushID(label)` and then submits
 * `Selectable("")`, so a menu item's id is the hash of the EMPTY string seeded
 * by the label -- `window->GetID(label)` alone never matches one. An ordinary
 * button or input in a modal body has no extra push.
 */
enum class ProbeIdKind {
    Direct,
    MenuBarMenu,
    MenuItem,
};

/** @brief One label the sweep must find, and where it found it. */
struct MenuProbe {
    const char* label;
    ProbeIdKind kind{ProbeIdKind::Direct};
    ImGuiID id{0};
    ImVec2 position{0.0f, 0.0f};
    bool found{false};
};

ImGuiID probe_id(const ImGuiWindow& window, const MenuProbe& probe) {
    switch (probe.kind) {
        case ProbeIdKind::MenuBarMenu:
            return ImHashStr(
                probe.label, 0, ImHashStr("##MenuBar", 0, window.ID));
        case ProbeIdKind::MenuItem:
            return ImHashStr("", 0, ImHashStr(probe.label, 0, window.ID));
        case ProbeIdKind::Direct:
            break;
    }
    return ImHashStr(probe.label, 0, window.ID);
}

} // namespace

/**
 * @brief MAR-181 C9 -- the File menu items and the chooser exist, and a real
 *        mouse reaches them.
 *
 * This is the one observation C4-C8 cannot make: they call the shell seams
 * directly and would ALL pass with every menu item deleted. A widget that is not
 * emitted cannot own an id equal to `window->GetID(label)`, so a label that is
 * never hovered at any scanned position is a FAILURE, never a skip.
 */
bool validate_mar181_file_menu_mouse_smoke(const std::filesystem::path& project_path) {
    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) || state.load_result.skeleton_data == nullptr) {
        std::cerr << "MAR-181 C9 could not load " << project_path << ".\n";
        return false;
    }
    state.session.clear_history();

    ImGuiIO& io = ImGui::GetIO();
    const bool macos_behaviors_before = io.ConfigMacOSXBehaviors;
    io.ConfigMacOSXBehaviors = false;

    const auto render_frame = [&]() {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        (void)draw_menu_bar(&state);
        ImGui::Render();
    };

    const auto fail = [&](const std::string& message) {
        std::cerr << message;
        io.ConfigMacOSXBehaviors = macos_behaviors_before;
        return false;
    };

    /** @brief Sweeps a real mouse across one window and records what it hovered. */
    const auto sweep = [&](std::vector<MenuProbe>& probes,
                           const char* window_title,
                           const char* case_label) -> bool {
        ImGuiWindow* window = ImGui::FindWindowByName(window_title);
        if (window == nullptr) {
            std::cerr << "MAR-181 C9 " << case_label << ": \"" << window_title
                      << "\" was never submitted.\n";
            return false;
        }
        for (MenuProbe& probe : probes) {
            probe.id = probe_id(*window, probe);
            probe.found = false;
        }
        // An ImGuiWindowFlags_AlwaysAutoResize window is submitted at a stub size
        // on its first frame and only reaches its content size on the next one.
        // Measured: the chooser's Rect() one frame after it opens is
        // (452,259)-(468,296), a 16x37 stub, so a sweep that captured bounds
        // immediately would scan a sliver and find nothing.
        for (int settle = 0; settle < 3; ++settle) {
            render_frame();
        }
        window = ImGui::FindWindowByName(window_title);
        if (window == nullptr) {
            std::cerr << "MAR-181 C9 " << case_label << ": lost \"" << window_title
                      << "\" while it settled.\n";
            return false;
        }
        // The window's FULL rect, not InnerClipRect: a menu bar's client area is
        // empty (measured: ##MainMenuBar's InnerClipRect is (0,21)-(1440,21), a
        // zero-height band), because the bar itself lives in MenuBarRect.
        const ImRect bounds = window->Rect();
        bool remaining = true;
        for (float y = bounds.Min.y + 2.0f; y <= bounds.Max.y - 2.0f && remaining;
             y += 4.0f) {
            for (float x = bounds.Min.x + 4.0f; x <= bounds.Max.x - 2.0f && remaining;
                 x += 12.0f) {
                io.AddMousePosEvent(x, y);
                render_frame();
                const ImGuiContext* context = ImGui::GetCurrentContext();
                const ImGuiID hovered = context != nullptr ? context->HoveredId : 0U;
                if (hovered == 0U) continue;
                remaining = false;
                for (MenuProbe& probe : probes) {
                    if (!probe.found && probe.id == hovered) {
                        probe.found = true;
                        probe.position = ImVec2(x, y);
                    }
                    if (!probe.found) remaining = true;
                }
            }
        }
        bool complete = true;
        for (const MenuProbe& probe : probes) {
            if (!probe.found) {
                std::cerr << "MAR-181 C9 " << case_label << ": \"" << window_title
                          << "\" never emitted a widget with the id of \""
                          << probe.label
                          << "\". A real mouse swept every position in the window and "
                             "HoveredId never equalled window->GetID(\"" << probe.label
                          << "\"), so the widget is absent.\n";
                complete = false;
            }
        }
        return complete;
    };

    const auto click_position = [&](ImVec2 position) {
        io.AddMousePosEvent(position.x, position.y);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_frame();
    };

    render_frame();
    render_frame();

    // --- Step 1: reach the File menu itself. --------------------------------
    std::vector<MenuProbe> bar_probes{{"File", ProbeIdKind::MenuBarMenu}};
    if (!sweep(bar_probes, "##MainMenuBar", "step 1 (menu bar)")) {
        return fail("");
    }
    click_position(bar_probes[0].position);

    // --- Step 2: MEASURE the opened menu popup's ImGui window name. ---------
    const ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr || context->OpenPopupStack.Size == 0) {
        return fail(
            "MAR-181 C9 step 2: clicking \"File\" opened no popup, so the menu is "
            "undrivable under this harness.\n");
    }
    const ImGuiWindow* menu_window =
        context->OpenPopupStack[context->OpenPopupStack.Size - 1].Window;
    if (menu_window == nullptr) {
        return fail(
            "MAR-181 C9 step 2: the File menu popup has no window yet.\n");
    }
    const std::string menu_window_name = menu_window->Name;
    std::cout << "MAR-181 C9 measured: the open File menu popup's ImGui window name is "
                 "\"" << menu_window_name << "\".\n";

    // --- Step 3: every File item is present and hoverable. ------------------
    std::vector<MenuProbe> menu_probes{
        {"New Project...", ProbeIdKind::MenuItem},
        {"Open Project...", ProbeIdKind::MenuItem},
        {"Save", ProbeIdKind::MenuItem},
        {"Save As...", ProbeIdKind::MenuItem},
        {"Reload Project", ProbeIdKind::MenuItem},
        {"Quit", ProbeIdKind::MenuItem}};
    if (!sweep(menu_probes, menu_window_name.c_str(), "step 3 (File menu)")) {
        return fail("");
    }

    // --- Step 4: a real click on Open Project... raises the chooser. --------
    click_position(menu_probes[1].position);
    const ImGuiWindow* chooser = ImGui::FindWindowByName(kFilePathModal);
    if (chooser == nullptr || !chooser->Active) {
        return fail(
            "MAR-181 C9 step 4: a real click on \"Open Project...\" did not open \"" +
            std::string(kFilePathModal) +
            "\". The click never reached begin_file_action, or the modal is not "
            "drawn at root scope -- ImGui::OpenPopup inside BeginMenu hashes "
            "against the MENU window's id stack and cannot open a root-level "
            "modal.\n");
    }
    if (!state.file_path_request.has_value()) {
        return fail(
            "MAR-181 C9 step 4: the chooser is open but ShellState carries no "
            "request, so MAR-182 could not observe its lifetime.\n");
    }

    // --- Step 5: Cancel closes it and clears the request. -------------------
    const SessionSnapshot before = capture_session_snapshot(state);
    std::vector<MenuProbe> modal_probes{{"Cancel", ProbeIdKind::Direct}};
    if (!sweep(modal_probes, kFilePathModal, "step 5 (chooser)")) {
        return fail("");
    }
    click_position(modal_probes[0].position);
    // ImGui::CloseCurrentPopup() runs inside the frame the window was already
    // submitted in, so `Active` only falls on the following NewFrame.
    render_frame();
    render_frame();
    const ImGuiWindow* chooser_after = ImGui::FindWindowByName(kFilePathModal);
    if (chooser_after != nullptr && chooser_after->Active) {
        return fail("MAR-181 C9 step 5: Cancel did not close the chooser.\n");
    }
    if (state.file_path_request.has_value()) {
        return fail(
            "MAR-181 C9 step 5: Cancel must clear ShellState::file_path_request -- "
            "that clearing is exactly how MAR-182 sees a cancelled save path.\n");
    }
    if (!(capture_session_snapshot(state) == before)) {
        return fail(
            "MAR-181 C9 step 5: cancelling the chooser must not touch the "
            "session.\n");
    }

    render_frame();
    io.ConfigMacOSXBehaviors = macos_behaviors_before;
    std::cout << "MAR-181 C9: a real mouse reaches File in the menu bar, every one of "
                 "New Project.../Open Project.../Save/Save As.../Reload Project/Quit "
                 "in the opened menu, and a click on Open Project... raises \""
              << kFilePathModal
              << "\" at root scope whose Cancel closes it and clears the request "
                 "without touching the session.\n";
    return true;
}

/**
 * @brief MAR-181 C10 -- Ctrl+S saves, and does not fire while typing.
 *
 * `handle_project_history_shortcuts` returns early on `io.WantTextInput`, so
 * placing the Ctrl+S handler ABOVE that guard would let a project save while the
 * user is typing a filename into the chooser. The second half is what catches
 * that; the first half still passes under the inversion.
 */
bool validate_mar181_save_shortcut_smoke(const std::filesystem::path& project_path) {
    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "marrow_mar181_c10";
    std::filesystem::path temp_project;
    {
        ShellState source;
        source.project_path = project_path;
        if (!reload_project(&source)) {
            std::cerr << "MAR-181 C10 could not load " << project_path << ".\n";
            return false;
        }
        if (!seed_shell_project_copy(source, temp_root, &temp_project, nullptr)) {
            std::cerr << "MAR-181 C10 could not seed a project copy.\n";
            return false;
        }
    }

    ShellState state;
    state.project_path = temp_project;
    if (!reload_project(&state) || state.load_result.skeleton_data == nullptr) {
        std::cerr << "MAR-181 C10 could not load the seeded project.\n";
        return false;
    }
    state.session.clear_history();

    ImGuiIO& io = ImGui::GetIO();
    // ImGui swaps Cmd and Ctrl at the EVENT layer when io.ConfigMacOSXBehaviors
    // is set, which is the default on Apple (measured by MAR-179 C5). The widget
    // under test is the guard ORDERING, not the platform's modifier mapping.
    const bool macos_behaviors_before = io.ConfigMacOSXBehaviors;
    io.ConfigMacOSXBehaviors = false;

    const auto render_frame = [&]() {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        handle_project_history_shortcuts(&state);
        (void)draw_menu_bar(&state);
        ImGui::Render();
    };

    const auto fail = [&](const std::string& message) {
        std::cerr << message;
        io.ConfigMacOSXBehaviors = macos_behaviors_before;
        std::error_code ignored;
        std::filesystem::remove_all(temp_root, ignored);
        return false;
    };

    const auto send_ctrl_s = [&]() {
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        render_frame();
        io.AddKeyEvent(ImGuiKey_S, true);
        render_frame();
        io.AddKeyEvent(ImGuiKey_S, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        render_frame();
    };

    const auto dirty_the_project = [&](const char* note) {
        auto transaction = state.session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "MAR-181 C10 note",
            "mar181-c10",
            false,
            marrow::editor::EditImpact::Project});
        if (!transaction || transaction.project() == nullptr) return false;
        transaction.project()->editor_metadata.notes += note;
        if (!transaction.commit()) return false;
        update_project_dirty_state(&state);
        return state.session.dirty();
    };

    render_frame();
    render_frame();

    // --- Half 1: Ctrl+S saves. ---------------------------------------------
    if (!dirty_the_project(" c10-a")) {
        return fail("MAR-181 C10 could not dirty the project.\n");
    }
    send_ctrl_s();
    if (state.session.dirty()) {
        return fail(
            "MAR-181 C10 half 1: Ctrl+S did not save -- the session is still "
            "dirty.\n");
    }
    if (!marrow::editor::load_project(temp_project)) {
        return fail(
            "MAR-181 C10 half 1: the Ctrl+S-saved project must RELOAD from disk.\n");
    }

    // --- Half 2: Ctrl+S does not fire while a text field has focus. --------
    begin_file_action(&state, FileAction::SaveAs);
    render_frame();
    render_frame();
    ImGuiWindow* chooser = ImGui::FindWindowByName(kFilePathModal);
    if (chooser == nullptr || !chooser->Active) {
        return fail(
            "MAR-181 C10 half 2 needs the chooser open to own a focused text "
            "field.\n");
    }
    const ImGuiID name_field = chooser->GetID("Name");
    bool focused = false;
    for (float y = chooser->InnerClipRect.Min.y + 2.0f;
         y <= chooser->InnerClipRect.Max.y - 2.0f && !focused;
         y += 4.0f) {
        for (float x = chooser->InnerClipRect.Min.x + 4.0f;
             x <= chooser->InnerClipRect.Max.x - 2.0f && !focused;
             x += 12.0f) {
            io.AddMousePosEvent(x, y);
            render_frame();
            const ImGuiContext* context = ImGui::GetCurrentContext();
            if (context == nullptr || context->HoveredId != name_field) continue;
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            render_frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            render_frame();
            focused = io.WantTextInput;
        }
    }
    if (!focused) {
        return fail(
            "MAR-181 C10 half 2: a real click never focused the chooser's Name "
            "field, so io.WantTextInput never became true and the guard under test "
            "was never reached.\n");
    }
    if (!dirty_the_project(" c10-b")) {
        return fail("MAR-181 C10 half 2 could not dirty the project.\n");
    }
    send_ctrl_s();
    if (!state.session.dirty()) {
        return fail(
            "MAR-181 C10 half 2: Ctrl+S fired while the chooser's Name field had "
            "keyboard focus. The handler must sit BELOW "
            "handle_project_history_shortcuts' io.WantTextInput guard, or typing a "
            "filename saves the project.\n");
    }

    // Leave the context clean for the cases that follow.
    state.file_path_request.reset();
    render_frame();
    render_frame();
    io.ConfigMacOSXBehaviors = macos_behaviors_before;
    std::error_code ignored;
    std::filesystem::remove_all(temp_root, ignored);
    std::cout << "MAR-181 C10: Ctrl+S saves a dirty project to a file that RELOADS "
                 "from disk, and the same chord with the chooser's Name field focused "
                 "leaves the session dirty because the handler sits below the "
                 "io.WantTextInput guard.\n";
    return true;
}


/**
 * @brief MAR-181 C11 -- a deferred file action actually reaches the SMOKE's
 *        frame body.
 *
 * `shell_main.cpp` and `shell_smoke_frames.cpp` carry hand-maintained duplicate
 * frame bodies. Deleting `apply_pending_file_action` from the smoke's copy
 * leaves a New/Open that works interactively and is invisible to every test --
 * MEASURED: with that one line removed, C4-C10 all still pass, because they
 * drive the UI-free seam directly. This case is the only one that observes the
 * wiring itself.
 *
 * The armed action deliberately FAILS (it names a project that does not exist)
 * so that consuming it perturbs nothing: `EditorSession::open` is atomic, so the
 * session, the runtime and every cached pointer are identical afterwards. What
 * changes is only the bookkeeping this case reads.
 */
bool validate_mar181_arm_deferred_action_for_frame_body(ShellState* state) {
    PendingFileApplication pending;
    pending.action = FileAction::Open;
    pending.path = state->project_path.parent_path() /
        "mar181_absent_project.marrow";
    state->pending_file_application = pending;
    return true;
}

bool validate_mar181_frame_body_applied_pending(const ShellState& state) {
    if (state.pending_file_application.has_value()) {
        std::cerr << "MAR-181 C11: the headless smoke's frame body never called "
                     "apply_pending_file_action -- a deferred action armed before "
                     "the frames was still pending after them. src/editor/"
                     "shell_smoke_frames.cpp and src/editor/shell_main.cpp are "
                     "hand-maintained duplicate frame bodies; the smoke's copy is "
                     "missing the call, so every New and Open in this build is "
                     "untested.\n";
        return false;
    }
    // The frame body keeps running after the loop and overwrites status_message
    // (measured: it ends at "Edited key easing"), so the surviving assertion is
    // the one that matters anyway -- the failed Open must not have moved the
    // shell's path, end to end through the real frame body.
    if (state.project_path.filename() == "mar181_absent_project.marrow") {
        std::cerr << "MAR-181 C11: the frame body adopted a project that does not "
                     "exist.\n";
        return false;
    }
    std::cout << "MAR-181 C11: a file action armed before the headless frames is "
                 "consumed by the smoke's OWN frame body without moving the shell's "
                 "project path, so the duplicate-frame-body wiring is observed "
                 "rather than assumed.\n";
    return true;
}


namespace {

/**
 * @brief Dirties the session through a real transaction, exactly as C10 does.
 *
 * It deliberately does NOT call `update_project_dirty_state`. MAR-182's gate
 * reads `session.dirty()`, and inversion I2 -- gating on the shell-side
 * `project_dirty` cache instead -- is only observable because the cache is left
 * stale right here.
 */
bool dirty_the_session(ShellState* state, const char* note) {
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "MAR-182 note",
        "mar182-note",
        false,
        marrow::editor::EditImpact::Project});
    if (!transaction || transaction.project() == nullptr) return false;
    transaction.project()->editor_metadata.notes += note;
    if (!transaction.commit()) return false;
    return state->session.dirty();
}

/** @brief Seeds a temp project copy and loads it into a fresh, clean ShellState. */
bool load_seeded_project(
    const ShellState& source_state,
    const std::filesystem::path& directory,
    std::filesystem::path* project_out,
    ShellState* state_out) {
    if (!seed_shell_project_copy(source_state, directory, project_out, nullptr)) {
        return false;
    }
    state_out->project_path = *project_out;
    if (!reload_project(state_out) || state_out->load_result.skeleton_data == nullptr) {
        return false;
    }
    state_out->session.clear_history();
    return true;
}

const char* intent_name(SessionIntent intent) {
    switch (intent) {
        case SessionIntent::New: return "New";
        case SessionIntent::Open: return "Open";
        case SessionIntent::Reload: return "Reload";
        case SessionIntent::Quit: return "Quit";
    }
    return "?";
}

} // namespace

/**
 * @brief MAR-183 C23 -- missing entries, and the guarantee that LOADING NEVER WRITES.
 *
 * This is the case that pins design 2.5's central decision: nothing prunes a
 * recent entry automatically. Not on load, not on display, not on click. A
 * project on an unmounted external volume or a sleeping network share must
 * survive a launch on which the user did nothing -- and because pruning on load
 * would imply WRITING on load, an auto-prune would delete the entry from disk
 * too, irreversibly, over a transient unmount.
 *
 * The settings file is hand-written here because nothing else can produce a file
 * naming a project that does not exist: every recording site runs only after a
 * successful open or save.
 */
bool validate_mar183_missing_entries_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar183_c23";
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    };
    const auto canonical = [](const std::filesystem::path& path) {
        return marrow::editor::canonical_recent_path(path);
    };

    std::error_code directory_error;
    std::filesystem::remove_all(root, directory_error);
    directory_error.clear();
    std::filesystem::create_directories(root, directory_error);
    if (directory_error) {
        std::cerr << "MAR-183 C23 could not create its scratch directory.\n";
        return false;
    }

    // --- The main body: a hand-written file naming one present, one missing. -
    {
        const ScopedPreferenceIsolation isolation("mar183-c23");
        const std::filesystem::path settings = isolation.settings_path();

        std::filesystem::path present;
        if (!seed_shell_project_copy(source_state, root / "present", &present, nullptr)) {
            std::cerr << "MAR-183 C23 could not seed a present project.\n";
            cleanup();
            return false;
        }
        const std::filesystem::path gone = root / "gone.marrow";
        if (std::filesystem::exists(gone)) {
            std::cerr << "MAR-183 C23 requires gone.marrow to NOT exist.\n";
            cleanup();
            return false;
        }
        // A relative spelling of `present`, so normalization has something to
        // collapse, plus an empty entry the STORE already skips.
        const std::filesystem::path relative_present =
            std::filesystem::relative(present, std::filesystem::current_path());

        const auto escape = [](const std::filesystem::path& path) {
            std::string out;
            for (const char character : path.string()) {
                if (character == '\\' || character == '"') out.push_back('\\');
                out.push_back(character);
            }
            return out;
        };
        const std::string seeded =
            std::string("{\n  \"version\": 1,\n  \"default_curve\": \"ease\",\n") +
            "  \"payload\": \"keep\",\n  \"recent_projects\": [\n    \"" +
            escape(present) + "\",\n    \"" + escape(gone) + "\",\n    \"" +
            escape(relative_present) + "\",\n    \"\"\n  ]\n}\n";
        std::string file_error;
        if (!write_text_file(settings, seeded, &file_error)) {
            std::cerr << "MAR-183 C23: " << file_error << '\n';
            cleanup();
            return false;
        }

        std::string bytes_before;
        if (!read_text_file(settings, &bytes_before, &file_error)) {
            std::cerr << "MAR-183 C23: " << file_error << '\n';
            cleanup();
            return false;
        }

        ShellState state;
        load_shell_preferences(&state);

        // (1) BYTE-IDENTITY ACROSS LOAD. (I11)
        std::string bytes_after;
        if (!read_text_file(settings, &bytes_after, &file_error) ||
            bytes_after != bytes_before) {
            std::cerr << "MAR-183 C23 assertion 1: LOADING MUST NEVER WRITE. The "
                         "settings file changed across load_shell_preferences, so a "
                         "hand-edited file is being rewritten under the user and an "
                         "entry on an unmounted volume would be destroyed.\n";
            cleanup();
            return false;
        }

        // (2) The MISSING entry survives. (I5)
        const std::vector<std::filesystem::path>& list =
            state.preferences.recent_projects;
        if (std::find(list.begin(), list.end(), canonical(gone)) == list.end()) {
            std::cerr << "MAR-183 C23 assertion 2: a recent entry whose file does "
                         "NOT exist must survive the load. Pruning it would delete "
                         "the user's bookmark over a transient unmount.\n";
            cleanup();
            return false;
        }

        // (3) Normalization ran IN MEMORY.
        if (list.size() != 2U) {
            std::cerr << "MAR-183 C23 assertion 3: the two spellings of the present "
                         "project must collapse and the empty entry must be gone, "
                         "leaving 2 entries; measured " << list.size() << ".\n";
            cleanup();
            return false;
        }
        if (list[0] != canonical(present) || list[1] != canonical(gone)) {
            std::cerr << "MAR-183 C23 assertion 3: normalization must keep the FIRST "
                         "occurrence at its position, leaving [present, gone].\n";
            cleanup();
            return false;
        }
        if (!list[0].is_absolute() || !list[1].is_absolute()) {
            std::cerr << "MAR-183 C23 assertion 3: every loaded entry must be "
                         "absolute -- ShellState::project_path defaults to a "
                         "RELATIVE path, so this is load-bearing.\n";
            cleanup();
            return false;
        }

        // (4) default_curve and the status.
        if (state.preferences.default_curve != marrow::editor::CurvePreset::Ease) {
            std::cerr << "MAR-183 C23 assertion 4: default_curve must survive.\n";
            cleanup();
            return false;
        }
        std::cout << "  MAR-183 C23 measured: a settings file carrying one empty "
                     "recent entry loads with status "
                  << (state.preference_status ==
                              marrow::editor::PreferenceLoadStatus::LoadedWithDefaults
                          ? "LoadedWithDefaults"
                          : "Loaded")
                  << " (the store counts a skipped entry as a defaulted field).\n";
        if (state.preference_status !=
            marrow::editor::PreferenceLoadStatus::LoadedWithDefaults) {
            std::cerr << "MAR-183 C23 assertion 4: an empty entry is skipped by the "
                         "store and marks the load as LoadedWithDefaults.\n";
            cleanup();
            return false;
        }

        // (5) Existence per entry.
        if (!marrow::editor::recent_project_exists(list[0]) ||
            marrow::editor::recent_project_exists(list[1])) {
            std::cerr << "MAR-183 C23 assertion 5: recent_project_exists must be "
                         "true for the present entry and false for the missing one.\n";
            cleanup();
            return false;
        }

        // (6) forget_recent_project removes exactly it, and persists.
        forget_recent_project(&state, list[1]);
        if (state.preferences.recent_projects.size() != 1U ||
            state.preferences.recent_projects.front() != canonical(present)) {
            std::cerr << "MAR-183 C23 assertion 6: Remove must drop exactly the "
                         "named entry.\n";
            cleanup();
            return false;
        }
        {
            const marrow::editor::PreferenceStore store;
            const auto reloaded = store.load();
            if (reloaded.preferences.recent_projects !=
                state.preferences.recent_projects) {
                std::cerr << "MAR-183 C23 assertion 6: the rewritten file must "
                             "reload element-wise equal.\n";
                cleanup();
                return false;
            }
            if (reloaded.preferences.default_curve !=
                marrow::editor::CurvePreset::Ease) {
                std::cerr << "MAR-183 C23 assertion 6: a recent-list write must "
                             "preserve default_curve.\n";
                cleanup();
                return false;
            }
        }

        // (7) Clear Missing removes exactly the missing, and is a no-op twice. (I12)
        {
            std::filesystem::path present_two;
            if (!seed_shell_project_copy(
                    source_state, root / "present2", &present_two, nullptr)) {
                std::cerr << "MAR-183 C23 could not seed a second present project.\n";
                cleanup();
                return false;
            }
            const std::filesystem::path gone_two = root / "gone2.marrow";
            state.preferences.recent_projects = {
                canonical(present), canonical(gone), canonical(present_two),
                canonical(gone_two)};
            (void)persist_recent_projects(&state, true);

            forget_missing_recent_projects(&state);
            const std::vector<std::filesystem::path> expected = {
                canonical(present), canonical(present_two)};
            if (state.preferences.recent_projects != expected) {
                std::cerr << "MAR-183 C23 assertion 7: Clear Missing must remove "
                             "exactly the missing entries and preserve the present "
                             "pair's relative order.\n";
                cleanup();
                return false;
            }

            std::string bytes_one;
            if (!read_text_file(settings, &bytes_one, &file_error)) {
                std::cerr << "MAR-183 C23: " << file_error << '\n';
                cleanup();
                return false;
            }
            std::error_code time_error;
            const auto mtime_one =
                std::filesystem::last_write_time(settings, time_error);

            forget_missing_recent_projects(&state);

            std::string bytes_two;
            if (!read_text_file(settings, &bytes_two, &file_error) ||
                bytes_two != bytes_one) {
                std::cerr << "MAR-183 C23 assertion 7: a SECOND Clear Missing over "
                             "an all-present list changes nothing and must not "
                             "rewrite the settings file.\n";
                cleanup();
                return false;
            }
            const auto mtime_two =
                std::filesystem::last_write_time(settings, time_error);
            if (mtime_two != mtime_one) {
                std::cerr << "MAR-183 C23 assertion 7: the no-op skip must leave the "
                             "settings file's mtime untouched -- rewriting identical "
                             "bytes is still a write.\n";
                cleanup();
                return false;
            }
        }
    }

    // (8) An ABSENT settings file stays absent across a load. (I11, first run) -
    {
        const ScopedPreferenceIsolation isolation("mar183-c23-firstrun");
        const std::filesystem::path settings = isolation.settings_path();
        if (std::filesystem::exists(settings)) {
            std::cerr << "MAR-183 C23 assertion 8: a fresh isolation must start "
                         "with no settings file.\n";
            cleanup();
            return false;
        }
        ShellState state;
        load_shell_preferences(&state);
        if (!state.preferences.recent_projects.empty()) {
            std::cerr << "MAR-183 C23 assertion 8: a first run must load an empty "
                         "recent list.\n";
            cleanup();
            return false;
        }
        if (state.preference_status !=
            marrow::editor::PreferenceLoadStatus::FirstRun) {
            std::cerr << "MAR-183 C23 assertion 8: a first run must report "
                         "FirstRun.\n";
            cleanup();
            return false;
        }
        if (std::filesystem::exists(settings)) {
            std::cerr << "MAR-183 C23 assertion 8: LOADING MUST NEVER WRITE -- a "
                         "first run created " << settings.string()
                      << ". Normalizing on load must stay in memory.\n";
            cleanup();
            return false;
        }
    }

    cleanup();
    return true;
}

/**
 * @brief MAR-183 C21 -- the recording policy, one phase per row of design 2.4.
 *
 * AC2 names three recording events and four non-events, and the hard part is the
 * pair that a content-keyed rule cannot separate: the first save of a New
 * session records, and an ordinary Save does not. No property of the DOCUMENT
 * distinguishes them, so the discriminator is an explicit arm --
 * `ShellState::pending_recent_on_first_save` -- and both of its polarities are
 * asserted here.
 *
 * Phase 6 is the sharp one. The naive shape of that inversion ("open A, then
 * Ctrl+S") CANNOT bite: A is already at the head, `promote` returns false and
 * the no-op skip suppresses the write, so dropping the arm entirely would still
 * leave the list correct. It must use the STARTUP path -- `reload_project`,
 * which records nothing -- against an ABSENT settings file.
 */
bool validate_mar183_recording_policy_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar183_c21";
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    };
    const auto canonical = [](const std::filesystem::path& path) {
        return marrow::editor::canonical_recent_path(path);
    };

    // --- Phases 1-5 and 7 share one isolation and one accumulating list. -----
    {
        const ScopedPreferenceIsolation isolation("mar183-c21");
        const std::filesystem::path settings = isolation.settings_path();

        std::filesystem::path project;
        ShellState state;
        if (!load_seeded_project(source_state, root, &project, &state)) {
            std::cerr << "MAR-183 C21 could not seed a project copy.\n";
            cleanup();
            return false;
        }
        load_shell_preferences(&state);
        if (!state.preferences.recent_projects.empty()) {
            std::cerr << "MAR-183 C21: a fresh isolation must start with an EMPTY "
                         "recent list.\n";
            cleanup();
            return false;
        }

        // --- Phase 1: a successful Open records. ----------------------------
        {
            PendingFileApplication pending;
            pending.action = FileAction::Open;
            pending.path = project;
            state.pending_file_application = pending;
            if (!apply_pending_file_action(&state)) {
                std::cerr << "MAR-183 C21 phase 1: the Open must succeed: "
                          << state.error_message << '\n';
                cleanup();
                return false;
            }
            if (state.preferences.recent_projects.size() != 1U ||
                state.preferences.recent_projects.front() != canonical(project)) {
                std::cerr << "MAR-183 C21 phase 1: a successful Open must record the "
                             "canonical path at the head. size="
                          << state.preferences.recent_projects.size() << ".\n";
                cleanup();
                return false;
            }
            if (!std::filesystem::exists(settings)) {
                std::cerr << "MAR-183 C21 phase 1: recording must PERSIST -- the "
                             "settings file does not exist at "
                          << settings.string() << ".\n";
                cleanup();
                return false;
            }
            const marrow::editor::PreferenceStore store;
            if (store.load().preferences.recent_projects !=
                state.preferences.recent_projects) {
                std::cerr << "MAR-183 C21 phase 1: the persisted list must reload "
                             "element-wise equal to the in-memory one.\n";
                cleanup();
                return false;
            }
        }

        // --- Phase 2: a FAILED Open records nothing and preserves order. (I4)
        {
            std::string project_text;
            std::string file_error;
            if (!read_text_file(project, &project_text, &file_error)) {
                std::cerr << "MAR-183 C21 phase 2: " << file_error << '\n';
                cleanup();
                return false;
            }
            const std::size_t position = project_text.find(".mskl");
            if (position == std::string::npos) {
                std::cerr << "MAR-183 C21 phase 2 could not find a skeleton "
                             "reference to break.\n";
                cleanup();
                return false;
            }
            // Break the STEM, keeping valid JSON: the case is an unresolvable
            // cross-reference, exactly as MAR-181 C7 builds it, not a syntax error.
            const std::size_t stem_start = project_text.rfind('"', position) + 1U;
            project_text.replace(
                stem_start, position + 5U - stem_start, "does_not_exist.mskl");
            const std::filesystem::path broken = root / "mar183-broken.marrow";
            if (!write_text_file(broken, project_text, &file_error)) {
                std::cerr << "MAR-183 C21 phase 2: " << file_error << '\n';
                cleanup();
                return false;
            }
            if (!marrow::runtime::json::load_document(broken).document.has_value()) {
                std::cerr << "MAR-183 C21 phase 2 requires the broken project to be "
                             "VALID JSON -- otherwise the Open fails for the wrong "
                             "reason and the case proves nothing.\n";
                cleanup();
                return false;
            }

            const std::vector<std::filesystem::path> before =
                state.preferences.recent_projects;
            PendingFileApplication pending;
            pending.action = FileAction::Open;
            pending.path = broken;
            state.pending_file_application = pending;
            if (apply_pending_file_action(&state)) {
                std::cerr << "MAR-183 C21 phase 2: opening a project whose skeleton "
                             "is missing must FAIL.\n";
                cleanup();
                return false;
            }
            if (state.preferences.recent_projects != before) {
                std::cerr << "MAR-183 C21 phase 2: a FAILED Open must record nothing "
                             "and leave the order unchanged. The head is now '"
                          << (state.preferences.recent_projects.empty()
                                  ? std::string("<empty>")
                                  : state.preferences.recent_projects.front().string())
                          << "'.\n";
                cleanup();
                return false;
            }
        }

        // --- Phase 3: a successful Save As records the NEW path. ------------
        const std::filesystem::path save_as_target = root / "mar183-saved-as.marrow";
        {
            const std::filesystem::path previous_head =
                state.preferences.recent_projects.front();
            if (!apply_save_as(&state, save_as_target)) {
                std::cerr << "MAR-183 C21 phase 3: the Save As must succeed: "
                          << state.error_message << '\n';
                cleanup();
                return false;
            }
            if (state.preferences.recent_projects.size() != 2U ||
                state.preferences.recent_projects.front() !=
                    canonical(save_as_target)) {
                std::cerr << "MAR-183 C21 phase 3: Save As must record the NEW path "
                             "at the head, not the old one. Head is '"
                          << state.preferences.recent_projects.front().string()
                          << "'.\n";
                cleanup();
                return false;
            }
            if (state.preferences.recent_projects[1] != previous_head) {
                std::cerr << "MAR-183 C21 phase 3: the previous head must slide to "
                             "index 1.\n";
                cleanup();
                return false;
            }
        }

        // --- Phase 4: a FAILED Save As records nothing, and writes nothing
        //     INSIDE the rename seam. The seam is process-global and shared
        //     with the project writer, so a settings write landing inside it
        //     would be injected with a failure it never asked for.
        {
            const std::vector<std::filesystem::path> before =
                state.preferences.recent_projects;
            std::string settings_before;
            std::string file_error;
            if (!read_text_file(settings, &settings_before, &file_error)) {
                std::cerr << "MAR-183 C21 phase 4: " << file_error << '\n';
                cleanup();
                return false;
            }
            const std::filesystem::path failed_target =
                root / "mar183-failed-save-as.marrow";
            {
                const ScopedRenameCallback rename_failure(
                    [](const std::filesystem::path&, const std::filesystem::path&) {
                        return std::make_error_code(std::errc::permission_denied);
                    });
                if (apply_save_as(&state, failed_target)) {
                    std::cerr << "MAR-183 C21 phase 4: an injected rename failure "
                                 "must fail the Save As.\n";
                    cleanup();
                    return false;
                }
            }
            if (state.preferences.recent_projects != before) {
                std::cerr << "MAR-183 C21 phase 4: a FAILED Save As must record "
                             "nothing.\n";
                cleanup();
                return false;
            }
            std::string settings_after;
            if (!read_text_file(settings, &settings_after, &file_error) ||
                settings_after != settings_before) {
                std::cerr << "MAR-183 C21 phase 4: a failed Save As must leave the "
                             "settings file BYTE-IDENTICAL -- no settings write may "
                             "occur inside the rename seam's scope.\n";
                cleanup();
                return false;
            }
        }

        // --- Phase 4b: a SUCCESSFUL Save As whose SETTINGS write FAILS must
        //     still report that failure. The rename seam is process-global and
        //     shared with the project writer, so a blanket failure (phase 4)
        //     fails the project save first and never reaches the settings
        //     write. This callback therefore fails ONLY the settings
        //     destination and performs the real rename for everything else.
        {
            const std::filesystem::path reported_target =
                root / "mar183-settings-write-reported.marrow";
            state.error_message.clear();
            {
                const ScopedRenameCallback settings_only_failure(
                    [](const std::filesystem::path& source,
                       const std::filesystem::path& destination) -> std::error_code {
                        if (destination.filename() == "editor-settings.json") {
                            return std::make_error_code(std::errc::permission_denied);
                        }
                        std::error_code rename_error;
                        std::filesystem::rename(source, destination, rename_error);
                        return rename_error;
                    });
                if (!apply_save_as(&state, reported_target)) {
                    std::cerr << "MAR-183 C21 phase 4b: the PROJECT save must still "
                                 "succeed -- only the settings destination is failed: "
                              << state.error_message << '\n';
                    cleanup();
                    return false;
                }
            }
            if (state.error_message.empty()) {
                std::cerr << "MAR-183 C21 phase 4b: a failed settings write on the "
                             "Save As path must be REPORTED. Design 10.7 grants this "
                             "failure exactly ONE report -- 'reported once and then "
                             "forgotten' -- and clearing error_message AFTER "
                             "record_recent_project rather than before it swallows "
                             "that one report, so the failure is reported zero "
                             "times.\n";
                cleanup();
                return false;
            }
            if (state.preferences.recent_projects.empty() ||
                state.preferences.recent_projects.front() !=
                    canonical(reported_target)) {
                std::cerr << "MAR-183 C21 phase 4b: the failed settings write must "
                             "KEEP the in-memory record at the head.\n";
                cleanup();
                return false;
            }
        }

        // --- Phase 5: New records NOTHING; its FIRST save records. ----------
        {
            const std::filesystem::path fresh = root / "mar183-new";
            std::filesystem::path fresh_skeleton;
            std::vector<std::filesystem::path> fresh_atlases;
            if (!seed_shell_asset_copy(
                    source_state, fresh, &fresh_skeleton, &fresh_atlases)) {
                std::cerr << "MAR-183 C21 phase 5 could not seed a fresh rig.\n";
                cleanup();
                return false;
            }
            const std::filesystem::path target = fresh / "mar183-new.marrow";
            const std::vector<std::filesystem::path> before =
                state.preferences.recent_projects;

            PendingFileApplication pending;
            pending.action = FileAction::New;
            pending.path = target;
            pending.skeleton_path = fresh_skeleton;
            pending.atlas_paths = fresh_atlases;
            state.pending_file_application = pending;
            if (!apply_pending_file_action(&state)) {
                std::cerr << "MAR-183 C21 phase 5: New must succeed: "
                          << state.error_message << '\n';
                cleanup();
                return false;
            }
            if (std::filesystem::exists(target)) {
                std::cerr << "MAR-183 C21 phase 5: New must still write nothing.\n";
                cleanup();
                return false;
            }
            if (state.preferences.recent_projects != before) {
                std::cerr << "MAR-183 C21 phase 5: New must record NOTHING -- there "
                             "is no file on disk yet to record.\n";
                cleanup();
                return false;
            }
            if (!state.pending_recent_on_first_save.has_value() ||
                *state.pending_recent_on_first_save != target) {
                std::cerr << "MAR-183 C21 phase 5: New must ARM the first-save "
                             "recorder with its own target.\n";
                cleanup();
                return false;
            }

            if (!save_project_file(&state, true)) {
                std::cerr << "MAR-183 C21 phase 5: the first save of a New session "
                             "must succeed: " << state.error_message << '\n';
                cleanup();
                return false;
            }
            if (state.preferences.recent_projects.front() != canonical(target)) {
                std::cerr << "MAR-183 C21 phase 5: the FIRST save of a New session "
                             "must record its path at the head. Head is '"
                          << state.preferences.recent_projects.front().string()
                          << "'.\n";
                cleanup();
                return false;
            }
            if (state.pending_recent_on_first_save.has_value()) {
                std::cerr << "MAR-183 C21 phase 5: the arm must be CONSUMED by the "
                             "save that used it, so a second save records nothing.\n";
                cleanup();
                return false;
            }
        }

        // --- Phase 7: Reload records nothing. -------------------------------
        {
            const std::vector<std::filesystem::path> before =
                state.preferences.recent_projects;
            if (!reload_project(&state)) {
                std::cerr << "MAR-183 C21 phase 7: the reload must succeed: "
                          << state.error_message << '\n';
                cleanup();
                return false;
            }
            if (state.preferences.recent_projects != before) {
                std::cerr << "MAR-183 C21 phase 7: Reload must record nothing.\n";
                cleanup();
                return false;
            }
        }
    }

    // --- Phase 6: a STARTUP project's ordinary Save records nothing. (I1) ----
    // Its own isolation, its own ShellState, and an ABSENT settings file. This
    // is the ONLY shape in which dropping the arm is observable: the startup
    // path is `reload_project`, which records nothing, so the list is empty
    // before the save and the head is NOT the saved path.
    {
        const ScopedPreferenceIsolation isolation("mar183-c21-startup");
        const std::filesystem::path settings = isolation.settings_path();

        std::filesystem::path project;
        ShellState state;
        const std::filesystem::path startup_root = root / "startup";
        if (!seed_shell_project_copy(
                source_state, startup_root, &project, nullptr)) {
            std::cerr << "MAR-183 C21 phase 6 could not seed a startup project.\n";
            cleanup();
            return false;
        }
        // Exactly what `--project X` does: assign the path and reload_project.
        state.project_path = project;
        if (!reload_project(&state)) {
            std::cerr << "MAR-183 C21 phase 6 could not load the startup project: "
                      << state.error_message << '\n';
            cleanup();
            return false;
        }
        state.session.clear_history();
        load_shell_preferences(&state);

        if (!state.preferences.recent_projects.empty()) {
            std::cerr << "MAR-183 C21 phase 6: the startup path must record NOTHING, "
                         "but the list already holds "
                      << state.preferences.recent_projects.size() << " entr(y/ies).\n";
            cleanup();
            return false;
        }
        if (std::filesystem::exists(settings)) {
            std::cerr << "MAR-183 C21 phase 6: loading must not create the settings "
                         "file.\n";
            cleanup();
            return false;
        }
        if (!dirty_the_session(&state, " c21-startup")) {
            std::cerr << "MAR-183 C21 phase 6 could not dirty the session.\n";
            cleanup();
            return false;
        }

        if (!save_project_file(&state, true)) {
            std::cerr << "MAR-183 C21 phase 6: the ordinary save must succeed: "
                      << state.error_message << '\n';
            cleanup();
            return false;
        }

        if (!state.preferences.recent_projects.empty()) {
            std::cerr << "MAR-183 C21 phase 6: an ORDINARY Save must record nothing. "
                         "The list now holds '"
                      << state.preferences.recent_projects.front().string()
                      << "'. Only Open, Save As, and the FIRST save of a New session "
                         "record (AC2), and this session was created by neither.\n";
            cleanup();
            return false;
        }
        if (std::filesystem::exists(settings)) {
            std::cerr << "MAR-183 C21 phase 6: an ordinary Save must not write the "
                         "settings file at all -- it still must not exist at "
                      << settings.string() << ".\n";
            cleanup();
            return false;
        }
    }

    cleanup();
    return true;
}

/**
 * @brief MAR-183 C25 -- a real mouse through the real `Open Recent` submenu.
 *
 * The other five MAR-183 cases drive UI-free seams, and every one of them would
 * still pass with the entire menu deleted. This case is the one that cannot:
 * it sweeps a real ImGui window for the id of each label and fails when a label
 * is never hovered. That is MAR-181 C9's mechanism and MAR-182 C19's, verbatim.
 *
 * Phase 0 MEASURES the two harness facts the rest of the case depends on, and
 * PRINTS both every run. MAR-182's first Escape-closes-modal reading was an
 * artifact of `FindWindowByName` returning null for a modal that did not exist
 * yet; guessing M3 here is how inversion I10 becomes one that cannot bite.
 */
bool validate_mar183_recent_menu_mouse_smoke(const std::filesystem::path& project_path) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar183_c25";
    std::error_code directory_error;
    std::filesystem::remove_all(root, directory_error);
    directory_error.clear();
    std::filesystem::create_directories(root, directory_error);
    if (directory_error) {
        std::cerr << "MAR-183 C25 could not create its scratch directory.\n";
        return false;
    }
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    };

    const ScopedPreferenceIsolation isolation("mar183-c25");

    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) || state.load_result.skeleton_data == nullptr) {
        std::cerr << "MAR-183 C25 could not load " << project_path << ".\n";
        cleanup();
        return false;
    }
    state.session.clear_history();
    load_shell_preferences(&state);

    ImGuiIO& io = ImGui::GetIO();
    const bool macos_behaviors_before = io.ConfigMacOSXBehaviors;
    io.ConfigMacOSXBehaviors = false;

    const auto render_frame = [&]() {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        handle_project_history_shortcuts(&state);
        draw_menu_bar(&state);
        ImGui::Render();
        (void)apply_pending_file_action(&state);
    };

    const auto fail = [&](const std::string& message) {
        std::cerr << message;
        io.ConfigMacOSXBehaviors = macos_behaviors_before;
        cleanup();
        return false;
    };

    // Sweeps `window_title` for every probe. Returns whether ALL were found.
    // `report_missing` is false only for the deliberate absence measurements.
    const auto sweep = [&](std::vector<MenuProbe>& probes,
                           const char* window_title,
                           const char* case_label,
                           bool report_missing) -> bool {
        ImGuiWindow* window = ImGui::FindWindowByName(window_title);
        if (window == nullptr) {
            if (report_missing) {
                std::cerr << "MAR-183 C25 " << case_label << ": \"" << window_title
                          << "\" was never submitted.\n";
            }
            return false;
        }
        for (MenuProbe& probe : probes) {
            probe.id = probe_id(*window, probe);
            probe.found = false;
        }
        for (int settle = 0; settle < 3; ++settle) {
            render_frame();
        }
        window = ImGui::FindWindowByName(window_title);
        if (window == nullptr) {
            if (report_missing) {
                std::cerr << "MAR-183 C25 " << case_label << ": lost \""
                          << window_title << "\" while it settled.\n";
            }
            return false;
        }
        const ImRect bounds = window->Rect();
        for (float y = bounds.Min.y + 2.0f; y <= bounds.Max.y - 2.0f; y += 4.0f) {
            for (float x = bounds.Min.x + 4.0f; x <= bounds.Max.x - 2.0f; x += 12.0f) {
                io.AddMousePosEvent(x, y);
                render_frame();
                const ImGuiContext* context = ImGui::GetCurrentContext();
                const ImGuiID hovered = context != nullptr ? context->HoveredId : 0U;
                if (hovered == 0U) continue;
                for (MenuProbe& probe : probes) {
                    if (!probe.found && probe.id == hovered) {
                        probe.found = true;
                        probe.position = ImVec2(x, y);
                    }
                }
            }
        }
        bool complete = true;
        for (const MenuProbe& probe : probes) {
            if (!probe.found) {
                if (report_missing) {
                    std::cerr << "MAR-183 C25 " << case_label << ": \"" << window_title
                              << "\" never emitted a widget with the id of \""
                              << probe.label
                              << "\". A real mouse swept every position in the window "
                                 "and HoveredId never equalled that id, so the widget "
                                 "is absent or unreachable.\n";
                }
                complete = false;
            }
        }
        return complete;
    };

    const auto click_position = [&](ImVec2 position) {
        io.AddMousePosEvent(position.x, position.y);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_frame();
    };

    const auto close_all_menus = [&]() {
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::ClosePopupsExceptModals();
        }
        io.AddMousePosEvent(-100.0f, -100.0f);
        for (int settle = 0; settle < 3; ++settle) render_frame();
    };

    // Opens File, then Open Recent, leaving the submenu popup up.
    // @return the submenu popup's ImGui window name, or empty on failure.
    const auto open_recent_submenu = [&](const char* case_label) -> std::string {
        close_all_menus();
        std::vector<MenuProbe> bar_probes{{"File", ProbeIdKind::MenuBarMenu}};
        if (!sweep(bar_probes, "##MainMenuBar", case_label, true)) return {};
        click_position(bar_probes[0].position);

        std::vector<MenuProbe> file_probes{{kRecentMenu, ProbeIdKind::Direct}};
        if (!sweep(file_probes, "File###Menu_00", case_label, true)) return {};
        // A BeginMenu inside a popup opens on HOVER; the click is harmless and
        // makes the open deterministic under this harness.
        click_position(file_probes[0].position);
        for (int settle = 0; settle < 3; ++settle) render_frame();

        const ImGuiContext* context = ImGui::GetCurrentContext();
        if (context == nullptr || context->OpenPopupStack.Size == 0) return {};
        const ImGuiWindow* popup =
            context->OpenPopupStack[context->OpenPopupStack.Size - 1].Window;
        if (popup == nullptr) return {};
        return popup->Name;
    };

    render_frame();
    render_frame();

    // --- Seed three entries: two present, one missing. ----------------------
    const std::filesystem::path present_one = root / "alpha.marrow";
    const std::filesystem::path present_two = root / "beta.marrow";
    const std::filesystem::path missing_one = root / "gone.marrow";
    {
        std::string file_error;
        if (!write_text_file(present_one, "{}\n", &file_error) ||
            !write_text_file(present_two, "{}\n", &file_error)) {
            return fail("MAR-183 C25 could not seed the present entries.\n");
        }
    }
    // Oldest first, so the rendered order is [missing, beta, alpha].
    record_recent_project(&state, present_one);
    record_recent_project(&state, present_two);
    record_recent_project(&state, missing_one);
    if (state.preferences.recent_projects.size() != 3U) {
        return fail("MAR-183 C25 requires exactly three seeded entries.\n");
    }
    const std::vector<std::filesystem::path> seeded =
        state.preferences.recent_projects;
    // MenuProbe holds a `const char*`, so the label strings must outlive every
    // sweep that uses them.
    std::vector<std::string> labels;
    labels.reserve(seeded.size());
    for (const std::filesystem::path& entry : seeded) {
        labels.push_back(recent_menu_label(entry));
    }
    const std::string missing_label =
        recent_menu_label(marrow::editor::canonical_recent_path(missing_one));

    // --- Phase 0 / M2: the submenu opens, and its window name. --------------
    const std::string submenu_name = open_recent_submenu("phase 0");
    if (submenu_name.empty()) {
        return fail(
            "MAR-183 C25 M2: File > Open Recent did not open a child popup under "
            "this harness. The submenu is undrivable by a real mouse here, and "
            "design 2.6's shape must be revisited before any more of this case is "
            "written.\n");
    }
    std::cout << "MAR-183 C25 measured: the open \"Open Recent\" submenu popup's "
                 "ImGui window name is \"" << submenu_name << "\".\n";

    // --- Phase 0 / M3: is a DISABLED MenuItem reachable by HoveredId? -------
    bool disabled_hoverable = false;
    {
        std::vector<MenuProbe> probe{{missing_label.c_str(), ProbeIdKind::MenuItem}};
        disabled_hoverable = sweep(probe, submenu_name.c_str(), "M3", false);
    }
    std::cout << "MAR-183 C25 measured: a DISABLED MenuItem "
              << (disabled_hoverable ? "IS" : "is NOT")
              << " reachable by HoveredId under this harness.\n";

    // --- 1 + 2: the submenu is wired, and every seeded entry is emitted. ----
    // (I9) Deleting the draw_recent_projects_menu call makes phase 0 fail
    // outright, because "Open Recent" is then never submitted in the File menu.
    {
        std::vector<MenuProbe> entry_probes;
        for (const std::string& label : labels) {
            if (label == missing_label && !disabled_hoverable) continue;
            entry_probes.push_back({label.c_str(), ProbeIdKind::MenuItem});
        }
        if (!sweep(entry_probes, submenu_name.c_str(), "entries", true)) {
            return fail(
                "MAR-183 C25 assertion 2: every ENABLED seeded entry must be "
                "emitted in the submenu.\n");
        }
    }

    // --- 4: the missing entry is not actionable. ---------------------------
    {
        std::vector<MenuProbe> probe{{missing_label.c_str(), ProbeIdKind::MenuItem}};
        const bool found = sweep(probe, submenu_name.c_str(), "missing", false);
        if (found != disabled_hoverable) {
            return fail(
                "MAR-183 C25 assertion 4: the missing entry's reachability "
                "disagreed with the M3 measurement taken moments earlier.\n");
        }
        const std::filesystem::path path_before = state.project_path;
        // The three checks below are all VACUOUS on this clean session, and they
        // are kept only as cheap corroboration. A clickable missing entry runs
        // `open_recent_project` -> `begin_session_intent`, which on a CLEAN
        // session skips the prompt entirely (no `dirty_intent`), arms via
        // `arm_open`, and has that arm consumed by the very
        // `apply_pending_file_action` at the tail of the same `render_frame`,
        // which RESETS `pending_file_application` at its head and returns from
        // the failed `session.open` BEFORE it assigns `project_path`.
        // What actually survives that route is the failure REPORT: a status of
        // "Project load failed" and a non-empty `error_message`. Those are the
        // load-bearing assertions here, and they are snapshot rather than
        // assumed empty.
        const std::string error_before = state.error_message;
        const std::string status_before = state.status_message;
        if (found) {
            std::cout << "MAR-183 C25 assertion 4 branch: disabled items ARE "
                         "hoverable, so a click at the missing entry's own position "
                         "is asserted to do nothing.\n";
            click_position(probe[0].position);
            for (int settle = 0; settle < 3; ++settle) render_frame();
        } else {
            std::cout << "MAR-183 C25 assertion 4 branch: disabled items are NOT "
                         "hoverable, so the missing entry's ABSENCE from the sweep "
                         "is the assertion, and the two present entries were "
                         "found.\n";
        }
        if (state.error_message != error_before ||
            state.status_message != status_before) {
            return fail(
                "MAR-183 C25 assertion 4: a missing recent entry must NOT be "
                "actionable, but the click CHANGED the shell's messages -- status "
                "'" + status_before + "' -> '" + state.status_message +
                "', error '" + error_before + "' -> '" + state.error_message +
                "'. Dropping the `present` argument from the entry's MenuItem "
                "makes a dead path clickable: the open is attempted and fails, "
                "which is the only trace that survives apply_pending_file_action.\n");
        }
        if (state.dirty_intent.has_value() ||
            state.pending_file_application.has_value() ||
            state.project_path != path_before) {
            return fail(
                "MAR-183 C25 assertion 4: a missing recent entry must NOT be "
                "actionable -- it armed an intent, a pending action, or moved the "
                "shell's project path.\n");
        }
    }

    // --- 3: a click over a DIRTY session raises the prompt, carrying the path.
    {
        auto transaction = state.session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "MAR-183 C25 note",
            "mar183-c25",
            false,
            marrow::editor::EditImpact::Project});
        if (!transaction || transaction.project() == nullptr) {
            return fail("MAR-183 C25 could not begin the dirtying edit.\n");
        }
        transaction.project()->editor_metadata.notes += " c25-dirty";
        if (!transaction.commit() || !state.session.dirty()) {
            return fail("MAR-183 C25 could not dirty the session.\n");
        }

        const std::string target_label = recent_menu_label(seeded[1]);
        const std::string submenu = open_recent_submenu("dirty click");
        if (submenu.empty()) {
            return fail("MAR-183 C25 assertion 3 could not reopen the submenu.\n");
        }
        std::vector<MenuProbe> probe{{target_label.c_str(), ProbeIdKind::MenuItem}};
        if (!sweep(probe, submenu.c_str(), "dirty click", true)) {
            return fail(
                "MAR-183 C25 assertion 3: the entry to click was not reachable.\n");
        }
        click_position(probe[0].position);
        for (int settle = 0; settle < 3; ++settle) render_frame();

        if (!state.dirty_intent.has_value()) {
            return fail(
                "MAR-183 C25 assertion 3: clicking a recent entry over UNSAVED work "
                "must raise the Save/Discard/Cancel prompt. No intent was armed, so "
                "the Recent surface bypassed MAR-182's gate.\n");
        }
        if (state.dirty_intent->intent != SessionIntent::Open ||
            state.dirty_intent->path != seeded[1]) {
            return fail(
                "MAR-183 C25 assertion 3: the armed intent must be an Open carrying "
                "the clicked entry's path.\n");
        }
        if (state.pending_file_application.has_value()) {
            return fail(
                "MAR-183 C25 assertion 3: the open was PERFORMED behind the prompt.\n");
        }
        const ImGuiWindow* modal = ImGui::FindWindowByName(kDirtyIntentModal);
        if (modal == nullptr || !modal->Active) {
            return fail(
                "MAR-183 C25 assertion 3: the dirty-intent modal must be Active "
                "after a recent click over unsaved work.\n");
        }
        resolve_dirty_intent(&state, DirtyIntentResponse::Cancel);
        state.pending_file_application.reset();
        for (int settle = 0; settle < 3; ++settle) render_frame();
    }

    // --- 5: Remove reaches the MISSING entry. (I10) ------------------------
    {
        const std::string submenu = open_recent_submenu("remove");
        if (submenu.empty()) {
            return fail("MAR-183 C25 assertion 5 could not reopen the submenu.\n");
        }
        std::vector<MenuProbe> remove_probe{{kRecentRemoveMenu, ProbeIdKind::Direct}};
        if (!sweep(remove_probe, submenu.c_str(), "remove menu", true)) {
            return fail(
                "MAR-183 C25 assertion 5: the Remove submenu must be emitted.\n");
        }
        click_position(remove_probe[0].position);
        for (int settle = 0; settle < 3; ++settle) render_frame();

        const ImGuiContext* context = ImGui::GetCurrentContext();
        if (context == nullptr || context->OpenPopupStack.Size == 0) {
            return fail(
                "MAR-183 C25 assertion 5: the Remove submenu did not open.\n");
        }
        const ImGuiWindow* remove_popup =
            context->OpenPopupStack[context->OpenPopupStack.Size - 1].Window;
        if (remove_popup == nullptr) {
            return fail("MAR-183 C25 assertion 5: the Remove popup has no window.\n");
        }
        const std::string remove_name = remove_popup->Name;
        std::cout << "MAR-183 C25 measured: the open \"Remove\" submenu popup's "
                     "ImGui window name is \"" << remove_name << "\".\n";

        std::vector<MenuProbe> probe{{missing_label.c_str(), ProbeIdKind::MenuItem}};
        if (!sweep(probe, remove_name.c_str(), "remove missing", true)) {
            return fail(
                "MAR-183 C25 assertion 5: the MISSING entry must be reachable "
                "inside Remove. Its own row is disabled and cannot be clicked, so a "
                "Remove that is also disabled for it would leave the user no way to "
                "delete a dead bookmark at all.\n");
        }
        click_position(probe[0].position);
        for (int settle = 0; settle < 3; ++settle) render_frame();

        const std::vector<std::filesystem::path>& list =
            state.preferences.recent_projects;
        if (std::find(list.begin(), list.end(),
                      marrow::editor::canonical_recent_path(missing_one)) !=
            list.end()) {
            return fail(
                "MAR-183 C25 assertion 5: clicking Remove on the missing entry must "
                "delete exactly it.\n");
        }
        if (list.size() != 2U) {
            return fail(
                "MAR-183 C25 assertion 5: Remove must delete exactly one entry.\n");
        }
        const marrow::editor::PreferenceStore store;
        if (store.load().preferences.recent_projects != list) {
            return fail(
                "MAR-183 C25 assertion 5: the removal must reach the settings "
                "file.\n");
        }
    }

    // --- 6: Clear Missing removes only the missing. ------------------------
    {
        record_recent_project(&state, root / "gone-again.marrow");
        if (state.preferences.recent_projects.size() != 3U) {
            return fail("MAR-183 C25 assertion 6 needs three entries again.\n");
        }
        const std::string submenu = open_recent_submenu("clear missing");
        if (submenu.empty()) {
            return fail("MAR-183 C25 assertion 6 could not reopen the submenu.\n");
        }
        std::vector<MenuProbe> probe{{kRecentClearMissing, ProbeIdKind::MenuItem}};
        if (!sweep(probe, submenu.c_str(), "clear missing", true)) {
            return fail(
                "MAR-183 C25 assertion 6: Clear Missing must be emitted and enabled "
                "while at least one entry is missing.\n");
        }
        click_position(probe[0].position);
        for (int settle = 0; settle < 3; ++settle) render_frame();

        const std::vector<std::filesystem::path> expected = {
            marrow::editor::canonical_recent_path(present_two),
            marrow::editor::canonical_recent_path(present_one)};
        if (state.preferences.recent_projects != expected) {
            return fail(
                "MAR-183 C25 assertion 6: Clear Missing must remove every missing "
                "entry and keep every present one, in order.\n");
        }
    }

    // --- 7: the EMPTY list case. -------------------------------------------
    {
        state.preferences.recent_projects.clear();
        (void)persist_recent_projects(&state, true);
        close_all_menus();
        std::vector<MenuProbe> bar_probes{{"File", ProbeIdKind::MenuBarMenu}};
        if (!sweep(bar_probes, "##MainMenuBar", "empty", true)) {
            return fail("MAR-183 C25 assertion 7 could not reach the File menu.\n");
        }
        click_position(bar_probes[0].position);
        std::vector<MenuProbe> file_probes{{kRecentMenu, ProbeIdKind::Direct}};
        const bool emitted = sweep(file_probes, "File###Menu_00", "empty", false);
        std::cout << "MAR-183 C25 measured: with an EMPTY list, \"Open Recent\" "
                  << (emitted ? "IS still hoverable" : "is NOT hoverable")
                  << " -- BeginMenu(label, enabled=false).\n";
        if (emitted) {
            click_position(file_probes[0].position);
            for (int settle = 0; settle < 3; ++settle) render_frame();
            const ImGuiContext* context = ImGui::GetCurrentContext();
            const ImGuiWindow* popup =
                (context != nullptr && context->OpenPopupStack.Size > 0)
                    ? context->OpenPopupStack[context->OpenPopupStack.Size - 1].Window
                    : nullptr;
            if (popup != nullptr &&
                std::string(popup->Name).rfind(kRecentMenu, 0) == 0) {
                return fail(
                    "MAR-183 C25 assertion 7: a DISABLED Open Recent must not open a "
                    "child popup.\n");
            }
        }
        close_all_menus();
    }

    io.ConfigMacOSXBehaviors = macos_behaviors_before;
    cleanup();
    return true;
}

/**
 * @brief MAR-183 C24 -- non-interference, the no-op skip, and write failure.
 *
 * AC5's other half: recording a recent project is a PREFERENCE write and must
 * never touch the document. It opens no transaction, enters no undo history and
 * changes no revision, and the proof is a four-value comparison across the call
 * rather than an argument about which functions it happens to call.
 *
 * The rename seam it installs in assertion 3 is PROCESS-GLOBAL and shared with
 * the project writer (`atomic_file_write.hpp`), so the scope is RAII and no
 * project save happens inside it.
 */
bool validate_mar183_non_interference_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar183_c24";
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    };
    const ScopedPreferenceIsolation isolation("mar183-c24");
    const std::filesystem::path settings = isolation.settings_path();

    std::filesystem::path project;
    ShellState state;
    if (!load_seeded_project(source_state, root, &project, &state)) {
        std::cerr << "MAR-183 C24 could not seed a project copy.\n";
        cleanup();
        return false;
    }

    // Seed a settings file carrying BOTH a non-default curve and an unknown
    // additive field, so assertion 4 can prove the write preserved each.
    {
        std::string file_error;
        const std::string seeded =
            "{\n  \"version\": 1,\n  \"default_curve\": \"ease_in\",\n"
            "  \"payload\": \"keep\",\n  \"recent_projects\": []\n}\n";
        if (!write_text_file(settings, seeded, &file_error)) {
            std::cerr << "MAR-183 C24: " << file_error << '\n';
            cleanup();
            return false;
        }
    }
    load_shell_preferences(&state);
    if (state.preferences.default_curve != marrow::editor::CurvePreset::EaseIn) {
        std::cerr << "MAR-183 C24 requires the seeded curve to load.\n";
        cleanup();
        return false;
    }

    // --- 1: a record does not dirty the project or touch history. (I8) ------
    {
        if (state.session.dirty()) {
            std::cerr << "MAR-183 C24 assertion 1 requires a CLEAN session.\n";
            cleanup();
            return false;
        }
        const bool dirty_before = state.session.dirty();
        const bool can_undo_before = state.session.can_undo();
        const bool can_redo_before = state.session.can_redo();
        const std::string serialized_before =
            marrow::editor::serialize_project(*state.load_result.project);

        record_recent_project(&state, root / "c24-unrelated.marrow");

        if (state.session.dirty() != dirty_before ||
            state.session.can_undo() != can_undo_before ||
            state.session.can_redo() != can_redo_before) {
            std::cerr << "MAR-183 C24 assertion 1: recording a recent project must "
                         "not touch the session. dirty "
                      << dirty_before << "->" << state.session.dirty()
                      << ", can_undo " << can_undo_before << "->"
                      << state.session.can_undo() << ", can_redo "
                      << can_redo_before << "->" << state.session.can_redo()
                      << ". A preference write that opens a transaction would put "
                         "a settings change into the project's undo history.\n";
            cleanup();
            return false;
        }
        if (marrow::editor::serialize_project(*state.load_result.project) !=
            serialized_before) {
            std::cerr << "MAR-183 C24 assertion 1: recording must leave the project "
                         "document byte-identical.\n";
            cleanup();
            return false;
        }
    }

    // --- 2: re-recording the head writes NOTHING. (I12) ---------------------
    const std::filesystem::path head = root / "c24-head.marrow";
    {
        record_recent_project(&state, head);
        std::string bytes_before;
        std::string file_error;
        if (!read_text_file(settings, &bytes_before, &file_error)) {
            std::cerr << "MAR-183 C24: " << file_error << '\n';
            cleanup();
            return false;
        }
        std::error_code time_error;
        const auto mtime_before =
            std::filesystem::last_write_time(settings, time_error);

        record_recent_project(&state, head);

        std::string bytes_after;
        if (!read_text_file(settings, &bytes_after, &file_error) ||
            bytes_after != bytes_before) {
            std::cerr << "MAR-183 C24 assertion 2: re-recording the CURRENT HEAD "
                         "changes nothing and must not rewrite the settings file.\n";
            cleanup();
            return false;
        }
        const auto mtime_after =
            std::filesystem::last_write_time(settings, time_error);
        if (mtime_after != mtime_before) {
            std::cerr << "MAR-183 C24 assertion 2: the no-op skip must leave the "
                         "settings file's mtime untouched.\n";
            cleanup();
            return false;
        }
    }

    // --- 3: a write failure preserves. --------------------------------------
    {
        std::string bytes_before;
        std::string file_error;
        if (!read_text_file(settings, &bytes_before, &file_error)) {
            std::cerr << "MAR-183 C24: " << file_error << '\n';
            cleanup();
            return false;
        }
        const std::filesystem::path blocked = root / "c24-blocked.marrow";
        state.error_message.clear();
        {
            // RAII, and NO project save inside: the seam is process-global and
            // shared with the project writer.
            const ScopedRenameCallback rename_failure(
                [](const std::filesystem::path&, const std::filesystem::path&) {
                    return std::make_error_code(std::errc::permission_denied);
                });
            record_recent_project(&state, blocked);
        }
        if (state.error_message.empty()) {
            std::cerr << "MAR-183 C24 assertion 3: a failed settings write must "
                         "report an error.\n";
            cleanup();
            return false;
        }
        if (state.preferences.recent_projects.empty() ||
            state.preferences.recent_projects.front() !=
                marrow::editor::canonical_recent_path(blocked)) {
            std::cerr << "MAR-183 C24 assertion 3: a failed write KEEPS the "
                         "in-memory change, matching set_shell_default_curve's "
                         "shipped behaviour.\n";
            cleanup();
            return false;
        }
        std::string bytes_after;
        if (!read_text_file(settings, &bytes_after, &file_error) ||
            bytes_after != bytes_before) {
            std::cerr << "MAR-183 C24 assertion 3: a failed write must leave the "
                         "settings file BYTE-IDENTICAL.\n";
            cleanup();
            return false;
        }

        // After the scope closes, a re-record succeeds and reaches the file.
        const std::filesystem::path retry = root / "c24-retry.marrow";
        state.error_message.clear();
        record_recent_project(&state, retry);
        if (!state.error_message.empty()) {
            std::cerr << "MAR-183 C24 assertion 3: the retry after releasing the "
                         "seam must succeed: " << state.error_message << '\n';
            cleanup();
            return false;
        }
        const marrow::editor::PreferenceStore store;
        const auto reloaded = store.load();
        if (reloaded.preferences.recent_projects !=
            state.preferences.recent_projects) {
            std::cerr << "MAR-183 C24 assertion 3: the retry must reach the settings "
                         "file.\n";
            cleanup();
            return false;
        }

        // --- 4: everything else in the file survived. -----------------------
        if (reloaded.preferences.default_curve !=
            marrow::editor::CurvePreset::EaseIn) {
            std::cerr << "MAR-183 C24 assertion 4: a recent-list write must preserve "
                         "default_curve. Constructing a fresh EditorPreferences "
                         "instead of mutating the loaded one resets it to Linear.\n";
            cleanup();
            return false;
        }
        std::string final_bytes;
        if (!read_text_file(settings, &final_bytes, &file_error) ||
            final_bytes.find("\"payload\"") == std::string::npos ||
            final_bytes.find("keep") == std::string::npos) {
            std::cerr << "MAR-183 C24 assertion 4: the unknown additive field "
                         "\"payload\" must survive a recent-list write -- that is "
                         "the preserved_root guarantee.\n";
            cleanup();
            return false;
        }
    }

    cleanup();
    return true;
}

/**
 * @brief MAR-183 C20 -- the list algebra as the SHELL drives it.
 *
 * `marrow_preference_tests` covers the algebra pure, with no `ShellState` and no
 * store. This case covers the binding: that a relative `ShellState::project_path`
 * -- which is what the shipped default IS (`shell_state.hpp`'s
 * `assets/fixtures/player_idle.marrow`) -- reaches the settings file as an
 * absolute canonical path, and that what the shell wrote reloads equal through a
 * real `PreferenceStore`.
 */
bool validate_mar183_shell_list_algebra_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar183_c20";
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    };
    const ScopedPreferenceIsolation isolation("mar183-c20");

    std::filesystem::path project;
    ShellState state;
    if (!load_seeded_project(source_state, root, &project, &state)) {
        std::cerr << "MAR-183 C20 could not seed a project copy.\n";
        cleanup();
        return false;
    }
    load_shell_preferences(&state);

    // (1) A RELATIVE path records as an ABSOLUTE canonical one.
    const std::filesystem::path relative = "assets/fixtures/player_idle.marrow";
    if (relative.is_absolute()) {
        std::cerr << "MAR-183 C20: the fixture path must be relative for this case "
                     "to mean anything.\n";
        cleanup();
        return false;
    }
    record_recent_project(&state, relative);
    if (state.preferences.recent_projects.size() != 1U ||
        !state.preferences.recent_projects.front().is_absolute()) {
        std::cerr << "MAR-183 C20: a relative path must be stored ABSOLUTE -- "
                     "otherwise the list is relative to whichever directory the "
                     "editor happened to be launched from.\n";
        cleanup();
        return false;
    }
    if (state.preferences.recent_projects.front() !=
        marrow::editor::canonical_recent_path(relative)) {
        std::cerr << "MAR-183 C20: the stored form must be the canonical one.\n";
        cleanup();
        return false;
    }

    // (2) Re-recording the same file under its ABSOLUTE spelling promotes
    //     without duplicating.
    record_recent_project(&state, std::filesystem::absolute(relative));
    if (state.preferences.recent_projects.size() != 1U) {
        std::cerr << "MAR-183 C20: two spellings of one file must not produce two "
                     "entries; size is "
                  << state.preferences.recent_projects.size() << ".\n";
        cleanup();
        return false;
    }

    // (3) Twelve records leave exactly the bound, newest first.
    std::vector<std::filesystem::path> recorded;
    for (int index = 1; index <= 12; ++index) {
        const std::filesystem::path entry =
            root / ("c20-" + std::to_string(index) + ".marrow");
        recorded.push_back(marrow::editor::canonical_recent_path(entry));
        record_recent_project(&state, entry);
    }
    if (state.preferences.recent_projects.size() !=
        marrow::editor::kRecentProjectLimit) {
        std::cerr << "MAR-183 C20: twelve records must leave exactly "
                  << marrow::editor::kRecentProjectLimit << " entries; measured "
                  << state.preferences.recent_projects.size() << ".\n";
        cleanup();
        return false;
    }
    if (state.preferences.recent_projects.front() != recorded.back()) {
        std::cerr << "MAR-183 C20: the newest record must be at the head.\n";
        cleanup();
        return false;
    }

    // (4) What the shell wrote reloads equal through a real store.
    {
        const marrow::editor::PreferenceStore store;
        const auto reloaded = store.load();
        if (reloaded.preferences.recent_projects !=
            state.preferences.recent_projects) {
            std::cerr << "MAR-183 C20: the settings file must reload element-wise "
                         "equal to what the shell holds.\n";
            cleanup();
            return false;
        }
        std::vector<std::filesystem::path> copy =
            reloaded.preferences.recent_projects;
        if (marrow::editor::normalize_recent_paths(&copy)) {
            std::cerr << "MAR-183 C20: a list the shell wrote must already be "
                         "normalized -- normalizing it again changed it.\n";
            cleanup();
            return false;
        }
    }

    cleanup();
    return true;
}

/**
 * @brief MAR-183 C22 -- a Recent entry is a targeted Open, and the gate holds.
 *
 * MAR-182 built one gate in front of every session replacement, and MAR-183 adds
 * exactly one new origin to it. The whole risk of that addition is that the new
 * origin either bypasses the gate or loses its destination on the way through,
 * so this case asserts the DESTINATION at every step, never merely that
 * "something was performed".
 *
 * Phase 2 is the regression guard rather than a new-feature check: every
 * existing caller passes no path, and the empty-path branch is what keeps
 * `File > Open Project...` raising a chooser instead of arming an Open of "".
 */
bool validate_mar183_recent_gate_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar183_c22";
    const ScopedPreferenceIsolation isolation("mar183-c22");
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    };

    // --- Phase 1: a CLEAN session performs the targeted Open immediately. ----
    {
        std::filesystem::path project;
        ShellState state;
        if (!load_seeded_project(source_state, root, &project, &state)) {
            std::cerr << "MAR-183 C22 could not seed a project copy.\n";
            cleanup();
            return false;
        }
        const std::filesystem::path target = project;

        begin_session_intent(&state, SessionIntent::Open, target);

        if (!state.pending_file_application.has_value()) {
            std::cerr << "MAR-183 C22 phase 1: a clean session must arm the targeted "
                         "Open immediately. pending_file_application is empty.\n";
            cleanup();
            return false;
        }
        if (state.pending_file_application->action != FileAction::Open ||
            state.pending_file_application->path != target) {
            std::cerr << "MAR-183 C22 phase 1: the armed action must be Open of '"
                      << target.string() << "', measured path '"
                      << state.pending_file_application->path.string() << "'.\n";
            cleanup();
            return false;
        }
        if (state.dirty_intent.has_value() || state.file_path_request.has_value()) {
            std::cerr << "MAR-183 C22 phase 1: a clean targeted Open must raise "
                         "neither the prompt nor the chooser.\n";
            cleanup();
            return false;
        }
    }

    // --- Phase 2: a PATHLESS Open still raises the chooser. ------------------
    // The regression guard. Dropping the empty-path branch would arm an Open of
    // an empty path here and silently break every File > Open Project... click.
    {
        std::filesystem::path project;
        ShellState state;
        if (!load_seeded_project(source_state, root, &project, &state)) {
            std::cerr << "MAR-183 C22 could not seed a project copy for phase 2.\n";
            cleanup();
            return false;
        }

        begin_session_intent(&state, SessionIntent::Open);

        if (!state.file_path_request.has_value() ||
            state.file_path_request->action != FileAction::Open) {
            std::cerr << "MAR-183 C22 phase 2: an UNTARGETED Open must raise the "
                         "chooser -- file_path_request is empty or not an Open.\n";
            cleanup();
            return false;
        }
        if (state.pending_file_application.has_value()) {
            std::cerr << "MAR-183 C22 phase 2: an untargeted Open must arm nothing, "
                         "but pending_file_application holds '"
                      << state.pending_file_application->path.string() << "'.\n";
            cleanup();
            return false;
        }
    }

    // --- Phase 3: a DIRTY session arms the prompt, carrying the path. --------
    {
        std::filesystem::path project;
        ShellState state;
        if (!load_seeded_project(source_state, root, &project, &state)) {
            std::cerr << "MAR-183 C22 could not seed a project copy for phase 3.\n";
            cleanup();
            return false;
        }
        if (!dirty_the_session(&state, " c22-dirty")) {
            std::cerr << "MAR-183 C22 could not dirty the session.\n";
            cleanup();
            return false;
        }
        const std::filesystem::path target = project;
        const std::filesystem::path path_before = state.project_path;
        const SessionSnapshot before = capture_session_snapshot(state);

        begin_session_intent(&state, SessionIntent::Open, target);

        if (!state.dirty_intent.has_value() ||
            state.dirty_intent->intent != SessionIntent::Open ||
            state.dirty_intent->phase != DirtyIntentPhase::Prompting) {
            std::cerr << "MAR-183 C22 phase 3: a dirty session must ARM an Open "
                         "intent in the Prompting phase.\n";
            cleanup();
            return false;
        }
        if (state.dirty_intent->path != target) {
            std::cerr << "MAR-183 C22 phase 3: the armed intent must carry the "
                         "destination '" << target.string() << "', measured '"
                      << state.dirty_intent->path.string() << "'.\n";
            cleanup();
            return false;
        }
        if (state.pending_file_application.has_value() ||
            state.file_path_request.has_value()) {
            std::cerr << "MAR-183 C22 phase 3: the Open was PERFORMED behind the "
                         "prompt -- a Recent entry bypassed the gate.\n";
            cleanup();
            return false;
        }
        if (state.project_path != path_before ||
            !(capture_session_snapshot(state) == before)) {
            std::cerr << "MAR-183 C22 phase 3: arming must leave the session and "
                         "project_path bit-identical.\n";
            cleanup();
            return false;
        }

        // --- Phase 4: Discard performs THAT destination. ---------------------
        resolve_dirty_intent(&state, DirtyIntentResponse::Discard);
        if (!state.pending_file_application.has_value() ||
            state.pending_file_application->action != FileAction::Open ||
            state.pending_file_application->path != target) {
            std::cerr << "MAR-183 C22 phase 4: Discard must perform the Open of '"
                      << target.string() << "'.\n";
            cleanup();
            return false;
        }
    }

    // --- Phase 5: "last wish wins" retargets the DESTINATION too. (I2) ------
    {
        std::filesystem::path project;
        ShellState state;
        if (!load_seeded_project(source_state, root, &project, &state)) {
            std::cerr << "MAR-183 C22 could not seed a project copy for phase 5.\n";
            cleanup();
            return false;
        }
        if (!dirty_the_session(&state, " c22-retarget")) {
            std::cerr << "MAR-183 C22 could not dirty the session for phase 5.\n";
            cleanup();
            return false;
        }
        const std::filesystem::path target_a = project;
        const std::filesystem::path target_b =
            project.parent_path() / "mar183-c22-other.marrow";

        begin_session_intent(&state, SessionIntent::Open, target_a);
        begin_session_intent(&state, SessionIntent::Open, target_b);

        if (!state.dirty_intent.has_value() || state.dirty_intent->path != target_b) {
            std::cerr << "MAR-183 C22 phase 5: the LAST wish must win the "
                         "destination as well as the intent. Expected '"
                      << target_b.string() << "', measured '"
                      << (state.dirty_intent.has_value()
                              ? state.dirty_intent->path.string()
                              : std::string("<no intent>"))
                      << "'.\n";
            cleanup();
            return false;
        }
        resolve_dirty_intent(&state, DirtyIntentResponse::Discard);
        if (!state.pending_file_application.has_value() ||
            state.pending_file_application->path != target_b) {
            std::cerr << "MAR-183 C22 phase 5: Discard after a retarget must open "
                         "the SECOND entry, measured '"
                      << (state.pending_file_application.has_value()
                              ? state.pending_file_application->path.string()
                              : std::string("<nothing armed>"))
                      << "'.\n";
            cleanup();
            return false;
        }
    }

    // --- Phase 6: Cancel leaves everything. ---------------------------------
    {
        std::filesystem::path project;
        ShellState state;
        if (!load_seeded_project(source_state, root, &project, &state)) {
            std::cerr << "MAR-183 C22 could not seed a project copy for phase 6.\n";
            cleanup();
            return false;
        }
        if (!dirty_the_session(&state, " c22-cancel")) {
            std::cerr << "MAR-183 C22 could not dirty the session for phase 6.\n";
            cleanup();
            return false;
        }
        const SessionSnapshot before = capture_session_snapshot(state);

        begin_session_intent(&state, SessionIntent::Open, project);
        resolve_dirty_intent(&state, DirtyIntentResponse::Cancel);

        if (state.dirty_intent.has_value() ||
            state.pending_file_application.has_value() ||
            !(capture_session_snapshot(state) == before)) {
            std::cerr << "MAR-183 C22 phase 6: Cancel must leave the intent, the "
                         "arm and the session untouched.\n";
            cleanup();
            return false;
        }
    }

    // --- Phase 7: retargeting to a PATHLESS intent CLEARS the path. ---------
    // The other half of I2. Assigning the path only when it is non-empty leaves
    // a stale Open destination on a Reload, and `begin_file_action`'s Reload
    // case deliberately arms an EMPTY path because `reload_project` reads
    // `state->project_path` itself.
    {
        std::filesystem::path project;
        ShellState state;
        if (!load_seeded_project(source_state, root, &project, &state)) {
            std::cerr << "MAR-183 C22 could not seed a project copy for phase 7.\n";
            cleanup();
            return false;
        }
        if (!dirty_the_session(&state, " c22-clear")) {
            std::cerr << "MAR-183 C22 could not dirty the session for phase 7.\n";
            cleanup();
            return false;
        }

        begin_session_intent(&state, SessionIntent::Open, project);
        begin_session_intent(&state, SessionIntent::Reload);

        if (!state.dirty_intent.has_value() ||
            state.dirty_intent->intent != SessionIntent::Reload) {
            std::cerr << "MAR-183 C22 phase 7: the retargeted intent must be "
                         "Reload.\n";
            cleanup();
            return false;
        }
        if (!state.dirty_intent->path.empty()) {
            std::cerr << "MAR-183 C22 phase 7: retargeting to a PATHLESS intent "
                         "must CLEAR the destination, but it still holds '"
                      << state.dirty_intent->path.string()
                      << "'. A conditional assignment leaves a stale Open target "
                         "on a Reload.\n";
            cleanup();
            return false;
        }
        resolve_dirty_intent(&state, DirtyIntentResponse::Discard);
        if (!state.pending_file_application.has_value() ||
            state.pending_file_application->action != FileAction::Reload) {
            std::cerr << "MAR-183 C22 phase 7: Discard must perform a Reload.\n";
            cleanup();
            return false;
        }
        if (!state.pending_file_application->path.empty()) {
            std::cerr << "MAR-183 C22 phase 7: a Reload arms an EMPTY path -- "
                         "reload_project reads project_path itself -- but the arm "
                         "carries '"
                      << state.pending_file_application->path.string() << "'.\n";
            cleanup();
            return false;
        }
    }

    cleanup();
    return true;
}

/**
 * @brief MAR-182 C12 -- the gate itself, over all four intents.
 *
 * The four intents land in four DIFFERENT observable fields, and the clean half
 * asserts each intent's own field rather than a shared one. A gate that fires
 * for only some of the four fails on that intent's specific row; a single-field
 * assertion could not tell "unified" from "New happens to be gated".
 */
bool validate_mar182_intent_gate_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar182_c12";
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    };

    const SessionIntent intents[] = {
        SessionIntent::New,
        SessionIntent::Open,
        SessionIntent::Reload,
        SessionIntent::Quit};

    // --- Half 1: a DIRTY session arms the prompt and performs nothing. -------
    for (const SessionIntent intent : intents) {
        std::filesystem::path project;
        ShellState state;
        if (!load_seeded_project(source_state, root, &project, &state)) {
            std::cerr << "MAR-182 C12 could not seed a project copy.\n";
            cleanup();
            return false;
        }
        if (!dirty_the_session(&state, " c12-dirty")) {
            std::cerr << "MAR-182 C12 could not dirty the session.\n";
            cleanup();
            return false;
        }
        const SessionSnapshot before = capture_session_snapshot(state);

        begin_session_intent(&state, intent);

        if (!state.dirty_intent.has_value()) {
            std::cerr << "MAR-182 C12 dirty/" << intent_name(intent)
                      << ": a dirty session must ARM the prompt, not perform the "
                         "intent. dirty_intent is empty.\n";
            cleanup();
            return false;
        }
        if (state.dirty_intent->intent != intent ||
            state.dirty_intent->phase != DirtyIntentPhase::Prompting) {
            std::cerr << "MAR-182 C12 dirty/" << intent_name(intent)
                      << ": the armed intent must be this intent, Prompting.\n";
            cleanup();
            return false;
        }
        if (state.pending_file_application.has_value() ||
            state.new_project_form.has_value() ||
            state.file_path_request.has_value() || state.should_exit) {
            std::cerr << "MAR-182 C12 dirty/" << intent_name(intent)
                      << ": the intent was PERFORMED behind the prompt -- one of "
                         "pending_file_application / new_project_form / "
                         "file_path_request / should_exit moved.\n";
            cleanup();
            return false;
        }
        if (!(capture_session_snapshot(state) == before)) {
            std::cerr << "MAR-182 C12 dirty/" << intent_name(intent)
                      << ": arming the prompt must not touch the session.\n";
            cleanup();
            return false;
        }
        resolve_dirty_intent(&state, DirtyIntentResponse::Cancel);
    }

    // --- Half 2: a CLEAN session performs immediately, each in its OWN field.
    for (const SessionIntent intent : intents) {
        std::filesystem::path project;
        ShellState state;
        if (!load_seeded_project(source_state, root, &project, &state)) {
            std::cerr << "MAR-182 C12 could not seed a clean project copy.\n";
            cleanup();
            return false;
        }
        if (state.session.dirty()) {
            std::cerr << "MAR-182 C12 clean/" << intent_name(intent)
                      << ": the seeded session must start clean.\n";
            cleanup();
            return false;
        }

        begin_session_intent(&state, intent);

        if (state.dirty_intent.has_value()) {
            std::cerr << "MAR-182 C12 clean/" << intent_name(intent)
                      << ": a clean session must never raise the prompt.\n";
            cleanup();
            return false;
        }
        bool performed = false;
        switch (intent) {
            case SessionIntent::New:
                performed = state.new_project_form.has_value();
                break;
            case SessionIntent::Open:
                performed = state.file_path_request.has_value() &&
                    state.file_path_request->action == FileAction::Open;
                break;
            case SessionIntent::Reload:
                performed = state.pending_file_application.has_value() &&
                    state.pending_file_application->action == FileAction::Reload;
                break;
            case SessionIntent::Quit:
                performed = state.should_exit;
                break;
        }
        if (!performed) {
            std::cerr << "MAR-182 C12 clean/" << intent_name(intent)
                      << ": a clean session must perform the intent immediately, "
                         "and this intent's OWN field never moved.\n";
            cleanup();
            return false;
        }
    }

    cleanup();
    std::cout << "MAR-182 C12: on a dirty session each of New/Open/Reload/Quit arms "
                 "the prompt and performs nothing; on a clean session each performs "
                 "immediately into its own field.\n";
    return true;
}

/**
 * @brief MAR-182 C13 -- Save completes the intent only after a real save.
 *
 * The reload is the assertion that matters: `validate_project_for_save` takes no
 * base document, so a passing `save()` proves nothing about whether the bytes it
 * wrote can be opened. Only `load_project` materializes them.
 */
bool validate_mar182_save_completes_intent_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar182_c13";
    std::filesystem::path project;
    ShellState state;
    const auto fail = [&](const std::string& message) {
        std::cerr << message;
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return false;
    };
    if (!load_seeded_project(source_state, root, &project, &state)) {
        return fail("MAR-182 C13 could not seed a project copy.\n");
    }
    if (!dirty_the_session(&state, " c13-dirty")) {
        return fail("MAR-182 C13 could not dirty the session.\n");
    }

    begin_session_intent(&state, SessionIntent::Open);
    if (!state.dirty_intent.has_value()) {
        return fail("MAR-182 C13: a dirty Open must arm the prompt.\n");
    }
    resolve_dirty_intent(&state, DirtyIntentResponse::Save);

    if (state.session.dirty()) {
        return fail(
            "MAR-182 C13: Save must leave the session clean before the intent "
            "proceeds.\n");
    }
    if (state.dirty_intent.has_value()) {
        return fail("MAR-182 C13: a completed Save must clear the intent.\n");
    }
    if (!state.file_path_request.has_value() ||
        state.file_path_request->action != FileAction::Open) {
        return fail(
            "MAR-182 C13: after the save landed the Open intent must have been "
            "PERFORMED -- the chooser is not up with action == Open.\n");
    }
    const marrow::editor::ProjectLoadResult reloaded =
        marrow::editor::load_project(project);
    if (!reloaded || reloaded.skeleton_data == nullptr) {
        return fail(
            "MAR-182 C13: the saved project must RELOAD from disk. A passing "
            "save() proves nothing -- validate_project_for_save takes no base "
            "document.\n");
    }

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::cout << "MAR-182 C13: Save writes atomically, the session goes clean, the "
                 "Open intent is then performed, and the written project reloads "
                 "through load_project.\n";
    return true;
}

/**
 * @brief MAR-182 C14 -- a failed Save never falls through to the intent.
 *
 * `should_exit` is a single bool that the fall-through defect sets; there is no
 * way to write that bug and leave it false. The destination is compared byte for
 * byte AND reloaded, so an atomicity regression is caught too.
 */
bool validate_mar182_failed_save_holds_intent_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar182_c14";
    std::filesystem::path project;
    ShellState state;
    const auto fail = [&](const std::string& message) {
        std::cerr << message;
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return false;
    };
    if (!load_seeded_project(source_state, root, &project, &state)) {
        return fail("MAR-182 C14 could not seed a project copy.\n");
    }
    if (!dirty_the_session(&state, " c14-dirty")) {
        return fail("MAR-182 C14 could not dirty the session.\n");
    }

    std::string before_bytes;
    std::string file_error;
    if (!read_text_file(project, &before_bytes, &file_error)) {
        return fail("MAR-182 C14: " + file_error + "\n");
    }

    begin_session_intent(&state, SessionIntent::Quit);
    {
        // HAZARD: this seam is process-global and shared with the preference
        // writer. Nothing inside this scope may save preferences.
        const ScopedRenameCallback rename_failure(
            [](const std::filesystem::path&, const std::filesystem::path&) {
                return std::make_error_code(std::errc::permission_denied);
            });
        resolve_dirty_intent(&state, DirtyIntentResponse::Save);

        if (state.should_exit) {
            return fail(
                "MAR-182 C14: a FAILED save fell through to the Quit intent -- "
                "should_exit is true with the project unwritten.\n");
        }
        if (!state.dirty_intent.has_value() ||
            state.dirty_intent->intent != SessionIntent::Quit ||
            state.dirty_intent->phase != DirtyIntentPhase::Prompting) {
            return fail(
                "MAR-182 C14: a failed save must KEEP the intent and re-raise the "
                "prompt as Quit/Prompting.\n");
        }
        if (!state.session.dirty()) {
            return fail(
                "MAR-182 C14: a failed save must leave the session dirty.\n");
        }
        if (state.error_message.empty()) {
            return fail("MAR-182 C14: a failed save must report an error.\n");
        }
    }

    std::string after_bytes;
    if (!read_text_file(project, &after_bytes, &file_error)) {
        return fail("MAR-182 C14: " + file_error + "\n");
    }
    if (after_bytes != before_bytes) {
        return fail(
            "MAR-182 C14: a failed save must leave the destination byte-identical.\n");
    }
    if (!marrow::editor::load_project(project)) {
        return fail(
            "MAR-182 C14: the untouched destination must still reload.\n");
    }

    // --- The seam is released; the same answer now completes the intent. -----
    resolve_dirty_intent(&state, DirtyIntentResponse::Save);
    if (!state.should_exit) {
        return fail(
            "MAR-182 C14: once the save succeeds the held Quit intent must "
            "proceed.\n");
    }
    if (state.dirty_intent.has_value() || state.session.dirty()) {
        return fail(
            "MAR-182 C14: the completed save must clear the intent and the dirty "
            "flag.\n");
    }
    if (!marrow::editor::load_project(project)) {
        return fail("MAR-182 C14: the successfully saved project must reload.\n");
    }

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::cout << "MAR-182 C14: an injected rename failure keeps the Quit intent "
                 "Prompting with the file byte-identical and should_exit false; "
                 "releasing the seam and answering Save again exits.\n";
    return true;
}

/**
 * @brief MAR-182 C15 -- the save path can be cancelled, and can also COMPLETE
 *        over a destination that already exists.
 *
 * `AwaitingSave` is a distinct enum value that no "Save always resolves"
 * implementation ever produces. Asserting the PHASE, not just the outcome, is
 * what makes the middle state observable.
 *
 * Half B guards a real deadlock. `resolve_dirty_intent(Save)` with no
 * destination recurses into Save As, so the prompt's Save button reaches the
 * shared chooser -- and there the overwhelmingly common case is writing over a
 * file that already exists. MAR-181's design §3.2 said `Choose` is disabled
 * whenever the diagnostic is non-empty, which would disable it EXACTLY then and
 * leave `AwaitingSave` with no exit. Its §3.4 rule 5 is what shipped:
 * `FilePathChoice::acceptable` gates `Choose`, and an existing Save target is
 * accepted WITH the diagnostic. Asserting the pair simultaneously is the only
 * thing that catches a "fix" toward §3.2.
 */
bool validate_mar182_save_path_cancel_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar182_c15";
    std::filesystem::path project;
    ShellState state;
    const auto fail = [&](const std::string& message) {
        std::cerr << message;
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return false;
    };
    if (!load_seeded_project(source_state, root, &project, &state)) {
        return fail("MAR-182 C15 could not seed a project copy.\n");
    }
    if (!dirty_the_session(&state, " c15-dirty")) {
        return fail("MAR-182 C15 could not dirty the session.\n");
    }

    std::string before_bytes;
    std::string file_error;
    if (!read_text_file(project, &before_bytes, &file_error)) {
        return fail("MAR-182 C15: " + file_error + "\n");
    }

    // The documented `begin_file_action` guard branch: an empty project_path
    // makes Save recurse into Save As. This is that branch, not a hack.
    state.project_path.clear();

    begin_session_intent(&state, SessionIntent::Reload);
    resolve_dirty_intent(&state, DirtyIntentResponse::Save);

    if (!state.dirty_intent.has_value() ||
        state.dirty_intent->phase != DirtyIntentPhase::AwaitingSave) {
        return fail(
            "MAR-182 C15: a Save with no destination must park the intent in "
            "AwaitingSave while the chooser is up.\n");
    }
    if (!state.file_path_request.has_value() ||
        state.file_path_request->action != FileAction::SaveAs) {
        return fail(
            "MAR-182 C15: the Save As chooser must be raised for the missing "
            "destination.\n");
    }

    // The chooser's Cancel clears exactly this, and nothing else.
    state.file_path_request.reset();
    tick_dirty_intent(&state);

    if (!state.dirty_intent.has_value() ||
        state.dirty_intent->phase != DirtyIntentPhase::Prompting ||
        state.dirty_intent->intent != SessionIntent::Reload) {
        return fail(
            "MAR-182 C15: a cancelled save destination must return the SAME intent "
            "to Prompting.\n");
    }
    if (!state.session.dirty() || state.pending_file_application.has_value()) {
        return fail(
            "MAR-182 C15: a cancelled save destination must perform nothing and "
            "leave the session dirty.\n");
    }
    std::string after_bytes;
    if (!read_text_file(project, &after_bytes, &file_error)) {
        return fail("MAR-182 C15: " + file_error + "\n");
    }
    if (after_bytes != before_bytes) {
        return fail("MAR-182 C15: a cancelled save must write nothing.\n");
    }

    const SessionSnapshot before = capture_session_snapshot(state);
    resolve_dirty_intent(&state, DirtyIntentResponse::Cancel);
    if (state.dirty_intent.has_value()) {
        return fail("MAR-182 C15: Cancel must clear the intent.\n");
    }
    if (!(capture_session_snapshot(state) == before)) {
        return fail("MAR-182 C15: Cancel must not touch the session.\n");
    }

    // --- Half B: the same path COMPLETES over an existing destination. ------
    // The session is still dirty and project_path is still empty, so this
    // re-enters AwaitingSave exactly as half A did.
    begin_session_intent(&state, SessionIntent::Reload);
    resolve_dirty_intent(&state, DirtyIntentResponse::Save);
    if (!state.dirty_intent.has_value() ||
        state.dirty_intent->phase != DirtyIntentPhase::AwaitingSave ||
        !state.file_path_request.has_value()) {
        return fail(
            "MAR-182 C15 half B could not re-enter AwaitingSave.\n");
    }

    // The destination ALREADY EXISTS -- the common case for the prompt's Save.
    const FilePathChoice choice = resolve_choice(
        project.parent_path(),
        project.filename().string(),
        FilePathMode::SaveTarget,
        ".marrow");
    if (!choice.acceptable) {
        return fail(
            "MAR-182 C15 half B: a Save target that already exists must be "
            "ACCEPTED. It is rejected, so `Choose` is disabled exactly in the "
            "common case, and an AwaitingSave intent raised from the prompt has "
            "NO EXIT -- the prompt becomes unresolvable. `Choose` is gated on "
            "FilePathChoice::acceptable and must never be gated on the "
            "diagnostic being empty.\n");
    }
    if (choice.diagnostic != "Replaces the existing file.") {
        return fail(
            "MAR-182 C15 half B: an accepted overwrite must still CARRY its "
            "diagnostic. Acceptance and the diagnostic are two separate outputs; "
            "collapsing them is what breaks the save path.\n");
    }

    // `commit_path_choice`'s Save As branch, UI-free: apply, then clear the
    // request exactly as a successful commit does.
    if (!apply_save_as(&state, choice.path)) {
        return fail(
            "MAR-182 C15 half B: the overwrite must succeed.\n");
    }
    state.file_path_request.reset();
    tick_dirty_intent(&state);

    if (state.dirty_intent.has_value()) {
        return fail(
            "MAR-182 C15 half B: a committed save must complete the intent and "
            "clear it.\n");
    }
    if (state.session.dirty()) {
        return fail(
            "MAR-182 C15 half B: the committed save must leave the session "
            "clean.\n");
    }
    if (!state.pending_file_application.has_value() ||
        state.pending_file_application->action != FileAction::Reload) {
        return fail(
            "MAR-182 C15 half B: the held Reload intent must be PERFORMED once "
            "the save lands.\n");
    }
    if (!marrow::editor::load_project(project)) {
        return fail(
            "MAR-182 C15 half B: the OVERWRITTEN project must reload from disk. "
            "A passing save() proves nothing -- validate_project_for_save takes "
            "no base document.\n");
    }

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::cout << "MAR-182 C15: a Save with no destination parks the intent in "
                 "AwaitingSave, a cancelled chooser returns it to Prompting with "
                 "nothing written, Cancel clears it cleanly, and a commit over an "
                 "EXISTING destination -- accepted with \"Replaces the existing "
                 "file.\" -- completes the intent and reloads.\n";
    return true;
}

/**
 * @brief MAR-182 C16 -- Discard performs the intent without persisting anything.
 *
 * Two independent comparisons. The byte compare catches a Discard that secretly
 * saves; the post-reload document compare catches a Discard that secretly keeps
 * the edit. One assertion alone would catch only one of the two defects.
 */
bool validate_mar182_discard_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar182_c16";
    std::filesystem::path project;
    ShellState state;
    const auto fail = [&](const std::string& message) {
        std::cerr << message;
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return false;
    };
    if (!load_seeded_project(source_state, root, &project, &state)) {
        return fail("MAR-182 C16 could not seed a project copy.\n");
    }
    const char* kNote = " c16-unsaved-note";
    if (!dirty_the_session(&state, kNote)) {
        return fail("MAR-182 C16 could not dirty the session.\n");
    }

    std::string before_bytes;
    std::string file_error;
    if (!read_text_file(project, &before_bytes, &file_error)) {
        return fail("MAR-182 C16: " + file_error + "\n");
    }

    begin_session_intent(&state, SessionIntent::Reload);
    resolve_dirty_intent(&state, DirtyIntentResponse::Discard);

    if (!state.pending_file_application.has_value() ||
        state.pending_file_application->action != FileAction::Reload) {
        return fail(
            "MAR-182 C16: Discard must PERFORM the Reload intent -- the deferred "
            "action is not armed.\n");
    }
    if (!state.session.dirty()) {
        return fail(
            "MAR-182 C16: Discard must not save; the session stays dirty until the "
            "replacement lands.\n");
    }
    std::string after_bytes;
    if (!read_text_file(project, &after_bytes, &file_error)) {
        return fail("MAR-182 C16: " + file_error + "\n");
    }
    if (after_bytes != before_bytes) {
        return fail(
            "MAR-182 C16: Discard secretly SAVED -- the destination bytes moved.\n");
    }

    if (!apply_pending_file_action(&state)) {
        return fail("MAR-182 C16: the deferred Reload must run to success.\n");
    }
    if (state.session.dirty()) {
        return fail("MAR-182 C16: the reloaded session must be clean.\n");
    }
    if (state.session.project() == nullptr) {
        return fail("MAR-182 C16: the reloaded session must hold a project.\n");
    }
    if (state.session.project()->editor_metadata.notes.find(kNote) !=
        std::string::npos) {
        return fail(
            "MAR-182 C16: Discard secretly KEPT the edit -- the unsaved note "
            "survived the reload.\n");
    }

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::cout << "MAR-182 C16: Discard arms the Reload without writing a byte and "
                 "without clearing the session, and the reload then drops the "
                 "unsaved edit.\n";
    return true;
}

/**
 * @brief MAR-182 C17 -- Cancel is a no-op, and repeats do not stack.
 *
 * Each clause reads a DIFFERENT field -- `intent`, `phase`, `should_exit`, the
 * snapshot -- so replace, ignore and stack are three distinguishable outcomes
 * rather than one.
 */
bool validate_mar182_cancel_and_repeat_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar182_c17";
    std::filesystem::path project;
    ShellState state;
    const auto fail = [&](const std::string& message) {
        std::cerr << message;
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        return false;
    };
    if (!load_seeded_project(source_state, root, &project, &state)) {
        return fail("MAR-182 C17 could not seed a project copy.\n");
    }
    if (!dirty_the_session(&state, " c17-dirty")) {
        return fail("MAR-182 C17 could not dirty the session.\n");
    }
    const SessionSnapshot before = capture_session_snapshot(state);

    begin_session_intent(&state, SessionIntent::New);
    begin_session_intent(&state, SessionIntent::New);
    if (!state.dirty_intent.has_value() ||
        state.dirty_intent->intent != SessionIntent::New ||
        state.dirty_intent->phase != DirtyIntentPhase::Prompting) {
        return fail(
            "MAR-182 C17: repeating an intent must leave ONE intent, still New, "
            "still Prompting.\n");
    }

    begin_session_intent(&state, SessionIntent::Quit);
    if (state.dirty_intent->intent != SessionIntent::Quit) {
        return fail(
            "MAR-182 C17: a second intent must REPLACE the first (last wish wins), "
            "not stack behind it.\n");
    }

    resolve_dirty_intent(&state, DirtyIntentResponse::Cancel);
    if (state.dirty_intent.has_value()) {
        return fail("MAR-182 C17: Cancel must clear the intent.\n");
    }
    if (state.should_exit || state.pending_file_application.has_value() ||
        state.new_project_form.has_value() || state.file_path_request.has_value()) {
        return fail("MAR-182 C17: Cancel must perform nothing.\n");
    }
    if (!(capture_session_snapshot(state) == before)) {
        return fail(
            "MAR-182 C17: Cancel must leave the session bit-identical.\n");
    }

    // AC3: the intent is re-expressible, not retained.
    begin_session_intent(&state, SessionIntent::New);
    if (!state.dirty_intent.has_value() ||
        state.dirty_intent->intent != SessionIntent::New) {
        return fail(
            "MAR-182 C17: after a Cancel the same intent must arm cleanly again.\n");
    }

    // While a save is in flight, a new intent is IGNORED.
    state.project_path.clear();
    resolve_dirty_intent(&state, DirtyIntentResponse::Save);
    if (!state.dirty_intent.has_value() ||
        state.dirty_intent->phase != DirtyIntentPhase::AwaitingSave) {
        return fail("MAR-182 C17 needs an AwaitingSave intent to test the drop.\n");
    }
    begin_session_intent(&state, SessionIntent::Quit);
    if (state.dirty_intent->phase != DirtyIntentPhase::AwaitingSave ||
        state.dirty_intent->intent != SessionIntent::New) {
        return fail(
            "MAR-182 C17: a new intent raised while a save is in flight must be "
            "IGNORED -- the AwaitingSave intent must survive unchanged.\n");
    }

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::cout << "MAR-182 C17: repeats collapse to one intent, a later intent "
                 "replaces an earlier one, Cancel is bit-identical and "
                 "re-expressible, and an intent raised during AwaitingSave is "
                 "dropped.\n";
    return true;
}


/**
 * @brief MAR-182 C18 -- a native OS close request folds into the machine.
 *
 * The RETURN VALUE is the veto: a design that forgot to veto returns false on a
 * dirty project, which is one `if` here. The post-confirmation pass-through
 * clause catches the opposite deadlock, where a confirmed exit is vetoed
 * forever and the editor can never close.
 *
 * `shell_main.cpp`'s loop is unreachable from any headless test --
 * `run_headless_smoke` returns before a window host exists -- so the DECISION
 * lives in this pure function and only two lines of glue remain uncovered.
 */
bool validate_mar182_close_request_smoke(const ShellState& source_state) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "marrow_mar182_c18";
    const auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    };
    const auto fail = [&](const std::string& message) {
        std::cerr << message;
        cleanup();
        return false;
    };

    // --- A CLEAN session exits straight through, with no veto. --------------
    {
        std::filesystem::path project;
        ShellState state;
        if (!load_seeded_project(source_state, root, &project, &state)) {
            return fail("MAR-182 C18 could not seed a clean project copy.\n");
        }
        if (absorb_close_request(&state, true)) {
            return fail(
                "MAR-182 C18: a CLEAN session must not veto the close -- there is "
                "nothing to lose, so the latch must stand.\n");
        }
        if (!state.should_exit) {
            return fail(
                "MAR-182 C18: a clean close request must set should_exit.\n");
        }
    }

    // --- A DIRTY session vetoes, and says so through the return value. ------
    std::filesystem::path project;
    ShellState state;
    if (!load_seeded_project(source_state, root, &project, &state)) {
        return fail("MAR-182 C18 could not seed a project copy.\n");
    }
    if (!dirty_the_session(&state, " c18-dirty")) {
        return fail("MAR-182 C18 could not dirty the session.\n");
    }

    if (!absorb_close_request(&state, true)) {
        return fail(
            "MAR-182 C18: a DIRTY session must VETO the close -- the return value "
            "is what tells the loop to clear the host's latch, and without it the "
            "prompt cannot block the exit.\n");
    }
    if (state.should_exit) {
        return fail(
            "MAR-182 C18: a vetoed close must not set should_exit.\n");
    }
    if (!state.dirty_intent.has_value() ||
        state.dirty_intent->intent != SessionIntent::Quit) {
        return fail(
            "MAR-182 C18: a vetoed close must arm a Quit intent.\n");
    }

    // A second request while the prompt is up still vetoes, and does not stack.
    if (!absorb_close_request(&state, true)) {
        return fail(
            "MAR-182 C18: a repeated close request must keep vetoing while the "
            "prompt is up.\n");
    }
    if (!state.dirty_intent.has_value() ||
        state.dirty_intent->intent != SessionIntent::Quit) {
        return fail(
            "MAR-182 C18: a repeated close request must leave ONE Quit intent.\n");
    }

    // Confirming the exit, and then passing through it.
    resolve_dirty_intent(&state, DirtyIntentResponse::Discard);
    if (!state.should_exit) {
        return fail(
            "MAR-182 C18: Discard on a Quit intent must set should_exit.\n");
    }
    if (absorb_close_request(&state, true)) {
        return fail(
            "MAR-182 C18: a close request AFTER a confirmed exit must pass "
            "through, not veto -- vetoing here deadlocks the main loop and the "
            "editor can never close.\n");
    }
    // Discard does not save, so the session is STILL DIRTY while the shutdown
    // runs. An absorber that omits the confirmed-exit short-circuit therefore
    // re-enters the gate and arms a fresh prompt on the way out. The return
    // value alone cannot see this -- it is false either way, because
    // `!should_exit` is already false -- so the arming is what must be asserted.
    if (state.dirty_intent.has_value()) {
        return fail(
            "MAR-182 C18: a close request after a confirmed exit must arm "
            "NOTHING. The absorber re-entered the gate during shutdown and "
            "raised a prompt behind a window that is already closing.\n");
    }
    if (!state.should_exit) {
        return fail(
            "MAR-182 C18: a confirmed exit must stay confirmed.\n");
    }

    // --- No request is not a request. ---------------------------------------
    {
        std::filesystem::path idle_project;
        ShellState idle;
        if (!load_seeded_project(source_state, root, &idle_project, &idle)) {
            return fail("MAR-182 C18 could not seed the idle project copy.\n");
        }
        if (!dirty_the_session(&idle, " c18-idle")) {
            return fail("MAR-182 C18 could not dirty the idle session.\n");
        }
        if (absorb_close_request(&idle, false)) {
            return fail(
                "MAR-182 C18: absorbing a NON-request must never veto.\n");
        }
        if (idle.dirty_intent.has_value() || idle.should_exit) {
            return fail(
                "MAR-182 C18: absorbing a non-request must arm nothing.\n");
        }
    }

    cleanup();
    std::cout << "MAR-182 C18: a clean close passes through and exits, a dirty one "
                 "vetoes and arms a Quit intent, repeats do not stack, and a close "
                 "after a confirmed exit passes through.\n";
    return true;
}


/**
 * @brief MAR-182 C19 -- the prompt exists, a real mouse reaches it, and the
 *        menu is actually wired to the gate.
 *
 * C12-C18 all drive the seams directly and would pass with every File menu item
 * unwired -- MAR-181's own I8 measured exactly that. This is the ONLY case that
 * observes the wiring, and the only one that observes the modal exists at root
 * scope at all. A widget that is not emitted cannot own an id equal to
 * `window->GetID(label)`, so a label never hovered at any scanned position is a
 * FAILURE, never a skip.
 */
bool validate_mar182_dirty_prompt_mouse_smoke(const std::filesystem::path& project_path) {
    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) || state.load_result.skeleton_data == nullptr) {
        std::cerr << "MAR-182 C19 could not load " << project_path << ".\n";
        return false;
    }
    state.session.clear_history();

    ImGuiIO& io = ImGui::GetIO();
    const bool macos_behaviors_before = io.ConfigMacOSXBehaviors;
    io.ConfigMacOSXBehaviors = false;

    // The frame body in miniature: the menu bar reaches draw_file_path_modals,
    // and the deferred rail runs at end of frame.
    const auto render_frame = [&]() {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        handle_project_history_shortcuts(&state);
        draw_menu_bar(&state);
        ImGui::Render();
        (void)apply_pending_file_action(&state);
    };

    const auto fail = [&](const std::string& message) {
        std::cerr << message;
        io.ConfigMacOSXBehaviors = macos_behaviors_before;
        return false;
    };

    const auto sweep = [&](std::vector<MenuProbe>& probes,
                           const char* window_title,
                           const char* case_label) -> bool {
        ImGuiWindow* window = ImGui::FindWindowByName(window_title);
        if (window == nullptr) {
            std::cerr << "MAR-182 C19 " << case_label << ": \"" << window_title
                      << "\" was never submitted.\n";
            return false;
        }
        for (MenuProbe& probe : probes) {
            probe.id = probe_id(*window, probe);
            probe.found = false;
        }
        // An AlwaysAutoResize window is submitted at a stub size on its first
        // frame and only reaches its content size on the next one.
        for (int settle = 0; settle < 3; ++settle) {
            render_frame();
        }
        window = ImGui::FindWindowByName(window_title);
        if (window == nullptr) {
            std::cerr << "MAR-182 C19 " << case_label << ": lost \"" << window_title
                      << "\" while it settled.\n";
            return false;
        }
        const ImRect bounds = window->Rect();
        bool remaining = true;
        for (float y = bounds.Min.y + 2.0f; y <= bounds.Max.y - 2.0f && remaining;
             y += 4.0f) {
            for (float x = bounds.Min.x + 4.0f; x <= bounds.Max.x - 2.0f && remaining;
                 x += 12.0f) {
                io.AddMousePosEvent(x, y);
                render_frame();
                const ImGuiContext* context = ImGui::GetCurrentContext();
                const ImGuiID hovered = context != nullptr ? context->HoveredId : 0U;
                if (hovered == 0U) continue;
                remaining = false;
                for (MenuProbe& probe : probes) {
                    if (!probe.found && probe.id == hovered) {
                        probe.found = true;
                        probe.position = ImVec2(x, y);
                    }
                    if (!probe.found) remaining = true;
                }
            }
        }
        bool complete = true;
        for (const MenuProbe& probe : probes) {
            if (!probe.found) {
                std::cerr << "MAR-182 C19 " << case_label << ": \"" << window_title
                          << "\" never emitted a widget with the id of \""
                          << probe.label
                          << "\". A real mouse swept every position in the window "
                             "and HoveredId never equalled window->GetID(\""
                          << probe.label << "\"), so the widget is absent.\n";
                complete = false;
            }
        }
        return complete;
    };

    const auto click_position = [&](ImVec2 position) {
        io.AddMousePosEvent(position.x, position.y);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_frame();
    };

    const auto dirty_it = [&](const char* note) {
        auto transaction = state.session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "MAR-182 C19 note",
            "mar182-c19",
            false,
            marrow::editor::EditImpact::Project});
        if (!transaction || transaction.project() == nullptr) return false;
        transaction.project()->editor_metadata.notes += note;
        if (!transaction.commit()) return false;
        return state.session.dirty();
    };

    render_frame();
    render_frame();

    if (!dirty_it(" c19-a")) {
        return fail("MAR-182 C19 could not dirty the project.\n");
    }

    // --- Phase 0: MEASURE the two harness facts this case depends on. -------
    begin_session_intent(&state, SessionIntent::Reload);
    render_frame();
    const ImGuiWindow* stub = ImGui::FindWindowByName(kDirtyIntentModal);
    if (stub != nullptr) {
        const ImRect rect = stub->Rect();
        std::cout << "MAR-182 C19 measured: kDirtyIntentModal's Rect() one frame "
                     "after opening is (" << rect.Min.x << "," << rect.Min.y
                  << ")-(" << rect.Max.x << "," << rect.Max.y << "), "
                  << rect.GetWidth() << "x" << rect.GetHeight() << " px.\n";
    }
    io.AddKeyEvent(ImGuiKey_Escape, true);
    render_frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    render_frame();
    render_frame();
    const ImGuiWindow* after_escape = ImGui::FindWindowByName(kDirtyIntentModal);
    const bool escape_closes = after_escape == nullptr || !after_escape->Active;
    std::cout << "MAR-182 C19 measured: Escape "
              << (escape_closes ? "DOES" : "does NOT")
              << " close a p_open == nullptr modal under this harness.\n";

    /** @brief Closes the front popup by a route that is NOT one of its buttons. */
    const auto close_externally = [&]() {
        if (escape_closes) {
            io.AddKeyEvent(ImGuiKey_Escape, true);
            render_frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
        } else {
            ImGui::ClosePopupToLevel(0, true);
        }
        render_frame();
        render_frame();
    };

    state.dirty_intent.reset();
    render_frame();
    render_frame();

    // --- Phase 1: the menu reaches the gate, and the prompt is real. --------
    std::vector<MenuProbe> bar_probes{{"File", ProbeIdKind::MenuBarMenu}};
    if (!sweep(bar_probes, "##MainMenuBar", "phase 1 (menu bar)")) {
        return fail("");
    }
    click_position(bar_probes[0].position);

    const ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr || context->OpenPopupStack.Size == 0) {
        return fail(
            "MAR-182 C19 phase 1: clicking \"File\" opened no popup, so the menu "
            "is undrivable under this harness.\n");
    }
    const ImGuiWindow* menu_window =
        context->OpenPopupStack[context->OpenPopupStack.Size - 1].Window;
    if (menu_window == nullptr) {
        return fail("MAR-182 C19 phase 1: the File menu popup has no window yet.\n");
    }
    const std::string menu_window_name = menu_window->Name;
    std::cout << "MAR-182 C19 measured: the open File menu popup's ImGui window "
                 "name is \"" << menu_window_name << "\".\n";

    std::vector<MenuProbe> menu_probes{{"Reload Project", ProbeIdKind::MenuItem}};
    if (!sweep(menu_probes, menu_window_name.c_str(), "phase 1 (File menu)")) {
        return fail("");
    }
    click_position(menu_probes[0].position);

    const ImGuiWindow* prompt = ImGui::FindWindowByName(kDirtyIntentModal);
    if (prompt == nullptr || !prompt->Active) {
        return fail(
            "MAR-182 C19 phase 1: a real click on \"Reload Project\" over a DIRTY "
            "session did not open \"" + std::string(kDirtyIntentModal) +
            "\". Either the menu item is not wired to begin_session_intent, or "
            "the prompt is not drawn at root scope -- ImGui::OpenPopup inside "
            "BeginMenu hashes against the MENU window's id stack and cannot open "
            "a root-level modal.\n");
    }
    if (!state.dirty_intent.has_value() ||
        state.dirty_intent->intent != SessionIntent::Reload) {
        return fail(
            "MAR-182 C19 phase 1: the click must arm a Reload intent through the "
            "gate.\n");
    }
    if (state.pending_file_application.has_value() || state.session.dirty() == false) {
        return fail(
            "MAR-182 C19 phase 1: the reload must NOT have been performed behind "
            "the prompt.\n");
    }

    std::vector<MenuProbe> prompt_probes{
        {"Save", ProbeIdKind::Direct},
        {"Discard", ProbeIdKind::Direct},
        {"Cancel", ProbeIdKind::Direct}};
    if (!sweep(prompt_probes, kDirtyIntentModal, "phase 1 (prompt)")) {
        return fail("");
    }

    // --- Phase 2: Cancel closes it and touches nothing. ---------------------
    const SessionSnapshot before = capture_session_snapshot(state);
    click_position(prompt_probes[2].position);
    // CloseCurrentPopup runs inside the frame the window was already submitted
    // in, so `Active` only falls on the following NewFrame.
    render_frame();
    render_frame();
    const ImGuiWindow* prompt_after = ImGui::FindWindowByName(kDirtyIntentModal);
    if (prompt_after != nullptr && prompt_after->Active) {
        return fail("MAR-182 C19 phase 2: Cancel did not close the prompt.\n");
    }
    if (state.dirty_intent.has_value()) {
        return fail("MAR-182 C19 phase 2: Cancel must clear the intent.\n");
    }
    if (!(capture_session_snapshot(state) == before)) {
        return fail(
            "MAR-182 C19 phase 2: cancelling the prompt must not touch the "
            "session.\n");
    }

    // --- Phase 3: an EXTERNAL close of the prompt is a Cancel (AC5). --------
    begin_session_intent(&state, SessionIntent::Reload);
    render_frame();
    render_frame();
    const ImGuiWindow* reraised = ImGui::FindWindowByName(kDirtyIntentModal);
    if (reraised == nullptr || !reraised->Active) {
        return fail(
            "MAR-182 C19 phase 3: the prompt must be re-raisable after a "
            "Cancel.\n");
    }
    close_externally();
    if (state.dirty_intent.has_value()) {
        return fail(
            "MAR-182 C19 phase 3: a prompt closed by any route other than its own "
            "buttons must be treated as a Cancel and clear the intent. It is "
            "still set, so an AwaitingSave intent could never resolve and the "
            "prompt would never reopen.\n");
    }

    // --- Phase 4: the QUIT item is wired to the gate too. -------------------
    // Every other phase drives Reload Project, so an unwired Quit item is
    // invisible to them. MEASURED: inversion I9 -- reverting Quit to MAR-181's
    // "report upward and let the frame body handle it" -- left C12-C18 AND every
    // other phase of C19 green. This phase is the only observer of that item,
    // and it is why the story's headline gate is not trusted on faith.
    std::vector<MenuProbe> quit_bar_probes{{"File", ProbeIdKind::MenuBarMenu}};
    if (!sweep(quit_bar_probes, "##MainMenuBar", "phase 4 (menu bar)")) {
        return fail("");
    }
    click_position(quit_bar_probes[0].position);
    const ImGuiContext* quit_context = ImGui::GetCurrentContext();
    if (quit_context == nullptr || quit_context->OpenPopupStack.Size == 0) {
        return fail("MAR-182 C19 phase 4: clicking \"File\" opened no popup.\n");
    }
    const ImGuiWindow* quit_menu_window =
        quit_context->OpenPopupStack[quit_context->OpenPopupStack.Size - 1].Window;
    if (quit_menu_window == nullptr) {
        return fail("MAR-182 C19 phase 4: the File menu popup has no window.\n");
    }
    const std::string quit_menu_name = quit_menu_window->Name;
    std::vector<MenuProbe> quit_probes{{"Quit", ProbeIdKind::MenuItem}};
    if (!sweep(quit_probes, quit_menu_name.c_str(), "phase 4 (File menu)")) {
        return fail("");
    }
    click_position(quit_probes[0].position);

    if (state.should_exit) {
        return fail(
            "MAR-182 C19 phase 4: File > Quit over a DIRTY session must NOT exit. "
            "should_exit is set, so the unsaved work would be destroyed without a "
            "prompt.\n");
    }
    if (!state.dirty_intent.has_value() ||
        state.dirty_intent->intent != SessionIntent::Quit) {
        return fail(
            "MAR-182 C19 phase 4: the Quit menu item must arm a Quit intent "
            "through begin_session_intent. It did not, so Quit is handled "
            "somewhere this smoke's frame body does not run -- which is exactly "
            "how MAR-181 shipped it, and is invisible to every other case.\n");
    }
    const ImGuiWindow* quit_prompt = ImGui::FindWindowByName(kDirtyIntentModal);
    if (quit_prompt == nullptr || !quit_prompt->Active) {
        return fail(
            "MAR-182 C19 phase 4: the Quit intent must raise the prompt.\n");
    }
    close_externally();
    if (state.dirty_intent.has_value()) {
        return fail("MAR-182 C19 phase 4: the closed Quit prompt must clear.\n");
    }

    // --- Phase 5: the same rule for the chooser (the C15 precondition). -----
    begin_file_action(&state, FileAction::SaveAs);
    render_frame();
    render_frame();
    const ImGuiWindow* chooser = ImGui::FindWindowByName(kFilePathModal);
    if (chooser == nullptr || !chooser->Active) {
        return fail("MAR-182 C19 phase 5 needs the chooser open.\n");
    }
    close_externally();
    if (state.file_path_request.has_value()) {
        return fail(
            "MAR-182 C19 phase 5: a chooser closed externally must clear "
            "file_path_request. A stale request makes tick_dirty_intent read \"a "
            "destination is being chosen\" forever, which hangs an AwaitingSave "
            "intent.\n");
    }

    render_frame();
    io.ConfigMacOSXBehaviors = macos_behaviors_before;
    std::cout << "MAR-182 C19: a real mouse reaches File > Reload Project, a dirty "
                 "click raises \"" << kDirtyIntentModal
              << "\" at root scope with Save/Discard/Cancel all present, Cancel "
                 "closes it bit-identically, and an external close of either the "
                 "prompt or the chooser clears its request.\n";
    return true;
}


// ===========================================================================
// MAR-187 S1-S6 -- the Problems router and the shell wiring.
//
// EVERY case here is UI-FREE WITH RESPECT TO WIDGETS. They call
// `activate_problem_row` and `refresh_problems_if_revised` directly, so they
// prove the ROUTER and not that anything is drawn. AGENTS.md records the
// MAR-178 worked example where deleting a button's body left that story's own
// scenario printing its full success line while the frame smoke failed by name;
// F1 in shell_smoke_frames.cpp is the only case here that can see a widget.
// ===========================================================================

namespace {

/** @brief Seeds a shell state whose project carries the problems S1-S4 need. */
bool seed_problem_project(
    ShellState* state,
    const std::filesystem::path& project_path,
    const std::filesystem::path& scratch) {
    state->project_path = project_path;
    if (!reload_project(state) || state->load_result.skeleton_data == nullptr) {
        std::cerr << "MAR-187 S: could not load " << project_path << ".\n";
        return false;
    }
    marrow::editor::ProjectData project = *state->session.project();

    // A bone-targeted orphan overlay (Timeline panel).
    marrow::editor::TransformTimelineEdit transform;
    transform.animation_name = "ghost";
    transform.bone_name = "arm_l";
    transform.channel = marrow::editor::TransformTimelineChannel::Rotate;
    transform.keyframes.push_back({});
    project.transform_timeline_edits.push_back(transform);

    // A weight problem (Weights panel) and an orphan weight target whose
    // AttachmentSelection deliberately does not resolve (S4).
    const auto body_slot = state->load_result.skeleton_data->find_slot_index("body");
    const auto* attachment = body_slot.has_value()
        ? state->load_result.skeleton_data->find_attachment(
              "mesh_base", *body_slot, "body_mesh")
        : nullptr;
    if (attachment == nullptr) {
        std::cerr << "MAR-187 S: the fixture lost mesh_base/body/body_mesh.\n";
        return false;
    }
    marrow::editor::MeshWeightAttachmentEdit edit =
        marrow::editor::mesh_weight_model::mesh_weight_edit_from_runtime(
            *state->load_result.skeleton_data, "mesh_base", "body", "body_mesh",
            *attachment);
    for (auto& vertex : edit.vertices) {
        (void)marrow::editor::mesh_weight_model::canonicalize_mesh_weight_vertex(
            *state->load_result.skeleton_data, &vertex);
    }
    if (edit.vertices.size() < 2U) {
        std::cerr << "MAR-187 S: body_mesh has too few vertices.\n";
        return false;
    }
    for (auto& influence : edit.vertices[1].influences) {
        influence.weight *= 0.7;
    }
    marrow::editor::MeshWeightAttachmentEdit orphan = edit;
    orphan.attachment_name = "ghost_mesh";
    project.mesh_weight_attachment_edits = {edit, orphan};

    // A stale preview skin (Project panel).
    project.editor_metadata.preview_skins = {"default", "ghost_skin"};

    const std::filesystem::path seeded = scratch / "mar187_shell.marrow";
    const auto saved = marrow::editor::save_project(project, seeded);
    if (!saved) {
        std::cerr << "MAR-187 S: save_project failed: " << saved.error->message << ".\n";
        return false;
    }
    state->project_path = seeded;
    if (!reload_project(state)) {
        std::cerr << "MAR-187 S: could not reload the seeded project.\n";
        return false;
    }
    refresh_problems_if_revised(state);
    return true;
}

bool find_shell_issue(
    const ShellState& state,
    std::string_view identity,
    std::string_view label,
    marrow::editor::DiagnosticIssue* out) {
    if (!state.problems.report.has_value()) {
        std::cerr << label << ": the shell holds no report.\n";
        return false;
    }
    const auto index =
        marrow::editor::find_issue_by_identity(*state.problems.report, identity);
    if (!index.has_value()) {
        std::cerr << label << ": the shell's report does not carry '" << identity
                  << "'.\n";
        return false;
    }
    *out = state.problems.report->issues[*index];
    return true;
}

}  // namespace

bool validate_mar187_problems_shell_smoke(
    const std::filesystem::path& project_path) {
    const ScopedPreferenceIsolation isolation("mar187-problems");
    if (!isolation.installed()) {
        std::cerr << "MAR-187 problems shell smoke could not isolate "
                     "MARROW_CONFIG_HOME.\n";
        return false;
    }
    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() / "mar187-shell-smoke";
    std::error_code ec;
    std::filesystem::remove_all(scratch, ec);
    std::filesystem::create_directories(scratch, ec);

    ShellState state;
    if (!seed_problem_project(&state, project_path, scratch)) {
        return false;
    }
    if (!state.problems.report.has_value() || state.problems.report->issues.empty()) {
        std::cerr << "MAR-187 S: the seeded project reports no problems.\n";
        return false;
    }

    // -- S1 -- timeline activation. -----------------------------------------
    {
        marrow::editor::DiagnosticIssue issue;
        if (!find_shell_issue(
                state, "overlay.orphan_animation|transform|ghost|arm_l|rotate",
                "MAR-187 S1", &issue)) {
            return false;
        }
        state.selected_animation_name.clear();
        activate_problem_row(&state, issue);
        const auto* bone = state.selection.active() != nullptr
            ? std::get_if<marrow::editor::BoneSelection>(state.selection.active())
            : nullptr;
        if (bone == nullptr || bone->bone_name != "arm_l") {
            std::cerr << "MAR-187 S1: activating the transform-overlay row left the "
                         "selection "
                      << (state.selection.active() == nullptr ? "empty"
                                                              : "on the wrong item")
                      << "; expected active bone 'arm_l'.\n";
            return false;
        }
        if (state.selected_animation_name != "ghost") {
            std::cerr << "MAR-187 S1: selected_animation_name is '"
                      << state.selected_animation_name << "', expected 'ghost'.\n";
            return false;
        }
        if (state.problems.focus_request != kTimelineWindowTitle) {
            std::cerr << "MAR-187 S1: the focus request is '"
                      << state.problems.focus_request << "', expected '"
                      << kTimelineWindowTitle << "'.\n";
            return false;
        }
        if (state.problems.selected_identity != issue.identity) {
            std::cerr << "MAR-187 S1: the row was not remembered by identity.\n";
            return false;
        }
    }

    // -- S2 -- weight activation: selection, mode, FFD vertex, focus. --------
    {
        marrow::editor::DiagnosticIssue issue;
        if (!find_shell_issue(
                state, "weights.non_canonical|mesh_base|body|body_mesh|1",
                "MAR-187 S2", &issue)) {
            return false;
        }
        activate_problem_row(&state, issue);
        const auto* attachment = state.selection.active() != nullptr
            ? std::get_if<marrow::editor::AttachmentSelection>(state.selection.active())
            : nullptr;
        if (attachment == nullptr || attachment->slot_name != "body" ||
            attachment->skin_name != "mesh_base" ||
            attachment->attachment_name != "body_mesh") {
            std::cerr << "MAR-187 S2: the active selection is not "
                         "AttachmentSelection{slot 'body', skin 'mesh_base', "
                         "attachment 'body_mesh'} -- read BY NAME, because "
                         "AttachmentSelection and MeshWeightTarget are transposed.\n";
            return false;
        }
        if (current_shell_mode(&state) != ShellMode::WeightPaint) {
            std::cerr << "MAR-187 S2: shell_mode is not WeightPaint after activating a "
                         "weight issue. The numeric influence table is unreachable "
                         "outside WeightPaint (shell_inspector.cpp's "
                         "inspector_bone_pose_editable gate).\n";
            return false;
        }
        if (!state.viewport_ffd_selection.has_value() ||
            state.viewport_ffd_selection->vertex_indices !=
                std::vector<std::size_t>{1U}) {
            std::cerr << "MAR-187 S2: the FFD selection does not name exactly vertex "
                         "1.\n";
            return false;
        }
        if (state.problems.focus_request != kPropertiesWindowTitle) {
            std::cerr << "MAR-187 S2: the focus request is '"
                      << state.problems.focus_request << "', expected '"
                      << kPropertiesWindowTitle
                      << "'. There is no Weight window; weight authoring is the "
                         "Properties window in WeightPaint mode.\n";
            return false;
        }
    }

    // -- S3 -- preview activation: Project panel, nothing selectable. --------
    {
        marrow::editor::DiagnosticIssue issue;
        if (!find_shell_issue(
                state, "preview.stale_skin|ghost_skin", "MAR-187 S3", &issue)) {
            return false;
        }
        const marrow::editor::SelectionSet before = state.selection;
        activate_problem_row(&state, issue);
        if (state.problems.focus_request != kProjectWindowTitle) {
            std::cerr << "MAR-187 S3: the focus request is '"
                      << state.problems.focus_request << "', expected '"
                      << kProjectWindowTitle
                      << "' -- the preview reference is shown in the Project "
                         "window.\n";
            return false;
        }
        if ((state.selection.active() == nullptr) != (before.active() == nullptr) ||
            (state.selection.active() != nullptr &&
             *state.selection.active() != *before.active())) {
            std::cerr << "MAR-187 S3: activating a preview row changed the selection; "
                         "a stale preview skin names no selectable identity.\n";
            return false;
        }
    }

    // -- S4 -- a REMOVED target changes nothing it should not. ---------------
    {
        state.selection.replace(marrow::editor::BoneSelection{"spine"});
        marrow::editor::DiagnosticIssue issue;
        if (!find_shell_issue(
                state, "overlay.orphan_weight_target|mesh_base|body|ghost_mesh",
                "MAR-187 S4", &issue)) {
            return false;
        }
        state.status_message.clear();
        activate_problem_row(&state, issue);
        const auto* bone = state.selection.active() != nullptr
            ? std::get_if<marrow::editor::BoneSelection>(state.selection.active())
            : nullptr;
        if (bone == nullptr || bone->bone_name != "spine") {
            std::cerr << "MAR-187 S4: activating the orphan weight-target row replaced "
                         "the selection with an identity no runtime resolves. The "
                         "previous BoneSelection{spine} had to survive.\n";
            return false;
        }
        for (const char* token : {"mesh_base", "body", "ghost_mesh"}) {
            if (state.status_message.find(token) == std::string::npos) {
                std::cerr << "MAR-187 S4: status_message '" << state.status_message
                          << "' does not name '" << token << "'.\n";
                return false;
            }
        }
        if (state.problems.focus_request != kPropertiesWindowTitle) {
            std::cerr << "MAR-187 S4: the focus request is '"
                      << state.problems.focus_request << "', expected '"
                      << kPropertiesWindowTitle << "'.\n";
            return false;
        }
    }

    // -- S5 -- a fix through the shell path. --------------------------------
    {
        marrow::editor::DiagnosticIssue issue;
        if (!find_shell_issue(
                state, "weights.non_canonical|mesh_base|body|body_mesh|1",
                "MAR-187 S5", &issue)) {
            return false;
        }
        activate_problem_row(&state, issue);
        const std::string selected_before = state.problems.selected_identity;
        const std::size_t undo_before = state.session.undo_count();
        // Counted EXCLUDING `project.unsaved_changes`: applying a fix makes the
        // session dirty, so the collector ADDS that Warning in the same refresh
        // it drops the repaired issue, and a raw size comparison does not move.
        // Measured -- the first version of this assertion read "did not shrink"
        // on correct code.
        const auto real_issue_count = [&]() {
            std::size_t count = 0;
            for (const auto& candidate : state.problems.report->issues) {
                if (candidate.identity != "project.unsaved_changes") {
                    ++count;
                }
            }
            return count;
        };
        const std::size_t issues_before = real_issue_count();

        const marrow::editor::SafeFixResult applied =
            marrow::editor::apply_safe_fix(state.session, issue);
        if (!applied.ok || !applied.changed) {
            std::cerr << "MAR-187 S5: the shell-path fix reported ok=" << applied.ok
                      << " changed=" << applied.changed << " error='" << applied.error
                      << "'.\n";
            return false;
        }
        if (state.session.undo_count() != undo_before + 1U) {
            std::cerr << "MAR-187 S5: the fix produced " << (state.session.undo_count() - undo_before)
                      << " history entries, expected exactly one.\n";
            return false;
        }
        sync_shell_from_editor_session_if_revised(&state);
        if (state.observed_project_revision != state.session.project_revision()) {
            std::cerr << "MAR-187 S5: the shell did not catch up with the session's "
                         "project revision after the fix.\n";
            return false;
        }
        refresh_problems_if_revised(&state);
        if (!state.problems.report.has_value() ||
            real_issue_count() != issues_before - 1U) {
            std::cerr << "MAR-187 S5: the refreshed report holds "
                      << (state.problems.report.has_value() ? real_issue_count() : 0U)
                      << " problems excluding project.unsaved_changes, expected "
                      << (issues_before - 1U) << ".\n";
            return false;
        }
        if (marrow::editor::find_issue_by_identity(
                *state.problems.report, selected_before).has_value()) {
            std::cerr << "MAR-187 S5: the repaired problem is still reported.\n";
            return false;
        }
        // The remembered row is GONE, so it must be CLEARED -- not left pointing
        // at whatever now occupies that position.
        if (!state.problems.selected_identity.empty()) {
            std::cerr << "MAR-187 S5: after the repaired row vanished, "
                         "selected_identity is '" << state.problems.selected_identity
                      << "', expected empty.\n";
            return false;
        }
    }

    // -- S6 -- inspection invariants in the shell. ---------------------------
    {
        // SEVEN values, not four. The first version of this case snapshotted
        // bytes, dirty and undo_count only, and a `seek(0.5)` planted in the
        // refresh path did not move ANY of them -- it moves `preview_revision`,
        // which nothing here was reading. The case passed and the inversion was
        // recorded as "did not bite" until the mutation was moved and the
        // snapshot widened. That is AGENTS.md's H4 in this story's own test
        // code: an assertion that reads state a passing run leaves at its
        // default proves nothing.
        const std::string bytes_before =
            marrow::editor::serialize_project(*state.session.project());
        const bool dirty_before = state.session.dirty();
        const std::size_t undo_before = state.session.undo_count();
        const std::size_t redo_before = state.session.redo_count();
        const std::uint64_t project_before = state.session.project_revision();
        const std::uint64_t runtime_before = state.session.runtime_revision();
        const std::uint64_t preview_before = state.session.preview_revision();
        const std::size_t collects_before = state.problems.collect_count;
        for (int pass = 0; pass < 10; ++pass) {
            refresh_problems_if_revised(&state);
        }
        if (state.session.redo_count() != redo_before ||
            state.session.project_revision() != project_before ||
            state.session.runtime_revision() != runtime_before ||
            state.session.preview_revision() != preview_before) {
            std::cerr << "MAR-187 S6: refreshing the Problems view moved a session "
                         "revision -- preview_revision " << preview_before << " -> "
                      << state.session.preview_revision() << ", project "
                      << project_before << " -> " << state.session.project_revision()
                      << ", runtime " << runtime_before << " -> "
                      << state.session.runtime_revision()
                      << ". Inspection must not mutate the session.\n";
            return false;
        }
        if (state.problems.collect_count != collects_before) {
            std::cerr << "MAR-187 S6: ten refreshes with no edit in between ran "
                      << (state.problems.collect_count - collects_before)
                      << " collections, expected 0. The view is keyed on the two "
                         "revisions precisely so an idle frame costs nothing.\n";
            return false;
        }
        if (state.session.dirty() != dirty_before ||
            state.session.undo_count() != undo_before ||
            marrow::editor::serialize_project(*state.session.project()) != bytes_before) {
            std::cerr << "MAR-187 S6: refreshing the Problems view mutated the "
                         "session.\n";
            return false;
        }
    }

    std::cout << "MAR-187 S1-S6: activating a timeline row selects its bone, names its "
                 "animation and asks for the Timeline; a weight row selects its "
                 "attachment BY NAME, enters WeightPaint and narrows the FFD "
                 "selection to one vertex; a preview row asks for Project and "
                 "selects nothing; a removed target leaves the previous selection "
                 "standing and says what is gone; a fix through the shell path is one "
                 "history entry that shrinks the list and clears the vanished row; and "
                 "ten idle refreshes run no collection at all.\n";
    return true;
}

bool validate_shell_foundation_smoke(
    ShellState& shell_state,
    const Options& options) {
    if (!validate_selection_set_shell_smoke(&shell_state)) {
        return false;
    }
    if (!validate_timeline_p0_authoring_smoke(options.project_path)) {
        return false;
    }

    if (!inspector_bone_pose_editable(shell_state)) {
        std::cerr << "Animation mode did not enable inspector transform authoring.\n";
        return false;
    }
    apply_shell_mode(&shell_state, ShellMode::Setup);
    const auto setup_bone_index =
        shell_state.load_result.skeleton_data->find_bone_index("arm_l");
    if (!setup_bone_index.has_value() ||
        current_shell_mode(&shell_state) != ShellMode::Setup ||
        !shell_state.selected_animation_name.empty() ||
        !shell_state.session.preview_state().animation_name.empty() ||
        inspector_bone_pose_editable(shell_state)) {
        std::cerr << "Setup mode did not make inspector transforms read-only.\n";
        return false;
    }
    const auto& setup_pose =
        shell_state.load_result.skeleton_data->bones()[*setup_bone_index].setup_pose;
    const auto& setup_preview =
        shell_state.preview_skeleton()->bone_poses()[*setup_bone_index].local_pose;
    if (std::abs(setup_pose.x - setup_preview.x) > 1e-6 ||
        std::abs(setup_pose.y - setup_preview.y) > 1e-6 ||
        std::abs(setup_pose.rotation - setup_preview.rotation) > 1e-6) {
        std::cerr << "Setup mode did not display the runtime setup pose.\n";
        return false;
    }
    apply_shell_mode(&shell_state, ShellMode::Animation);
    if (current_shell_mode(&shell_state) != ShellMode::Animation ||
        !inspector_bone_pose_editable(shell_state) ||
        shell_state.session.preview_state().animation_name.empty()) {
        std::cerr << "Animation mode did not restore keyed inspector authoring.\n";
        return false;
    }

    const std::string parameter_mode_animation = shell_state.selected_animation_name;
    const double parameter_mode_time = shell_state.timeline_time_seconds;
    const bool parameter_mode_queue = shell_state.preview_queue_enabled;
    shell_state.timeline_playing = true;
    shell_state.session.set_playing(true);
    apply_shell_mode(&shell_state, ShellMode::Parameter);
    if (current_shell_mode(&shell_state) != ShellMode::Parameter ||
        shell_state.selected_animation_name != parameter_mode_animation ||
        shell_state.timeline_time_seconds != parameter_mode_time ||
        shell_state.preview_queue_enabled != parameter_mode_queue ||
        shell_state.timeline_playing || shell_state.session.preview_state().playing ||
        shell_state.weight_paint.enabled || inspector_bone_pose_editable(shell_state)) {
        std::cerr << "Parameter mode did not preserve the pose while disabling playback and bone tools.\n";
        return false;
    }
    apply_shell_mode(&shell_state, ShellMode::Animation);

    const bool initial_loop = shell_state.timeline_loop;
    const std::uint64_t initial_preview_revision = shell_state.observed_preview_revision;
    marrow::editor::PreviewState revised_preview = shell_state.session.preview_state();
    revised_preview.loop = !initial_loop;
    if (!marrow::editor::EditorSessionShellBinding::sync_preview_state(
            shell_state.session,
            revised_preview) ||
        shell_state.session.preview_revision() <= initial_preview_revision ||
        shell_state.timeline_loop != initial_loop) {
        std::cerr << "Session revision smoke could not stage an out-of-band preview change.\n";
        return false;
    }
    sync_shell_from_editor_session_if_revised(&shell_state);
    if (shell_state.timeline_loop == initial_loop ||
        shell_state.observed_preview_revision != shell_state.session.preview_revision()) {
        std::cerr << "ShellState did not react to the EditorSession preview revision.\n";
        return false;
    }
    revised_preview = shell_state.session.preview_state();
    revised_preview.loop = initial_loop;
    if (!marrow::editor::EditorSessionShellBinding::sync_preview_state(
            shell_state.session,
            revised_preview)) {
        std::cerr << "Session revision smoke could not restore the preview loop mode.\n";
        return false;
    }
    sync_shell_from_editor_session_if_revised(&shell_state);

    shell_state.session.clear_history();
    const auto metadata_descriptor = [](
                                         std::string label,
                                         std::string group) {
        return CoalescedEditDescriptor{
            EditActionKind::EditProperty,
            std::move(label),
            std::move(group),
            false,
            CoalescedEditPolicy::ProjectMetadataOnly,
            {}};
    };
    const auto mutate_notes = [&](const CoalescedEditFrame& frame,
                                  std::string suffix,
                                  std::string label,
                                  std::string group) {
        return apply_coalesced_edit_frame(
            &shell_state,
            frame,
            metadata_descriptor(std::move(label), std::move(group)),
            [&]() {
                shell_state.load_result.project->editor_metadata.notes += suffix;
            });
    };

    const std::string notes_before_coalesced =
        shell_state.load_result.project->editor_metadata.notes;
    constexpr ImGuiID no_movement_id = 0x4d415201U;
    if (!mutate_notes(
            CoalescedEditFrame{no_movement_id, true, false, false, false},
            {},
            "No-movement smoke edit",
            "coalesced-no-movement") ||
        !mutate_notes(
            CoalescedEditFrame{no_movement_id, false, false, false, true},
            {},
            "No-movement smoke edit",
            "coalesced-no-movement") ||
        shell_state.pending_edit_action.has_value() || shell_state.session.can_undo() ||
        shell_state.load_result.project->editor_metadata.notes != notes_before_coalesced) {
        std::cerr << "No-movement coalesced edit created history or changed metadata.\n";
        return false;
    }

    constexpr ImGuiID multi_sample_id = 0x4d415202U;
    const std::string multi_sample_suffix = " [sample one] [sample two]";
    if (!mutate_notes(
            CoalescedEditFrame{multi_sample_id, true, true, false, false},
            " [sample one]",
            "Multi-sample smoke edit",
            "coalesced-multi-sample") ||
        !mutate_notes(
            CoalescedEditFrame{multi_sample_id, false, true, false, false},
            " [sample two]",
            "Multi-sample smoke edit",
            "coalesced-multi-sample") ||
        !mutate_notes(
            CoalescedEditFrame{multi_sample_id, false, false, true, true},
            {},
            "Multi-sample smoke edit",
            "coalesced-multi-sample") ||
        !shell_state.session.can_undo() ||
        shell_state.load_result.project->editor_metadata.notes !=
            notes_before_coalesced + multi_sample_suffix ||
        !undo_project_change(&shell_state) || shell_state.session.can_undo() ||
        shell_state.load_result.project->editor_metadata.notes != notes_before_coalesced ||
        !redo_project_change(&shell_state) ||
        shell_state.load_result.project->editor_metadata.notes !=
            notes_before_coalesced + multi_sample_suffix ||
        !undo_project_change(&shell_state)) {
        std::cerr << "Multi-sample coalesced edit did not produce exactly one undo action.\n";
        return false;
    }
    shell_state.session.clear_history();

    if (shell_state.load_result.project->ik_constraint_edits.empty()) {
        std::cerr << "Coalesced runtime rollback smoke requires an IK constraint edit.\n";
        return false;
    }
    const std::string target_before_failure =
        shell_state.load_result.project->ik_constraint_edits.front().target_bone_name;
    constexpr ImGuiID rebuild_failure_id = 0x4d415203U;
    if (apply_coalesced_edit_frame(
            &shell_state,
            CoalescedEditFrame{rebuild_failure_id, true, true, false, false},
            CoalescedEditDescriptor{
                EditActionKind::EditProperty,
                "Invalid runtime smoke edit",
                "coalesced-runtime-failure",
                false,
                CoalescedEditPolicy::ProjectRuntime,
                "Runtime smoke edit failed"},
            [&]() {
                shell_state.load_result.project->ik_constraint_edits.front()
                    .target_bone_name.clear();
            }) ||
        shell_state.pending_edit_action.has_value() || shell_state.session.can_undo() ||
        shell_state.load_result.project->ik_constraint_edits.front().target_bone_name !=
            target_before_failure ||
        shell_state.error_message.empty() ||
        shell_state.status_message != "Runtime smoke edit failed") {
        std::cerr << "Failed coalesced runtime sample did not roll back atomically.\n";
        return false;
    }
    shell_state.error_message.clear();
    shell_state.status_message.clear();

    constexpr ImGuiID orphan_id = 0x4d415204U;
    if (!mutate_notes(
            CoalescedEditFrame{orphan_id, true, true, false, false},
            " [orphaned edit]",
            "Finalize orphaned smoke edit",
            "orphaned-smoke-edit")) {
        std::cerr << "Could not stage an orphaned coalesced edit.\n";
        return false;
    }
    finalize_orphaned_coalesced_edit(&shell_state);
    if (shell_state.pending_edit_action.has_value() || !shell_state.session.can_undo() ||
        shell_state.load_result.project->editor_metadata.notes == notes_before_coalesced ||
        !undo_project_change(&shell_state) ||
        shell_state.load_result.project->editor_metadata.notes != notes_before_coalesced) {
        std::cerr << "Orphaned shell gesture did not finalize into unified history.\n";
        return false;
    }
    shell_state.session.clear_history();

    const auto validate_cancelled_coalesced_edit = [&](ImGuiID item_id,
                                                        std::string_view reason) {
        if (!mutate_notes(
                CoalescedEditFrame{item_id, true, true, false, false},
                " [cancelled edit]",
                "Cancelled smoke edit",
                "coalesced-cancel")) {
            return false;
        }
        cancel_authoring_gestures(&shell_state, reason);
        const bool restored = !shell_state.pending_edit_action.has_value() &&
            !shell_state.session.can_undo() &&
            shell_state.load_result.project->editor_metadata.notes == notes_before_coalesced;
        shell_state.status_message.clear();
        return restored;
    };
    if (!validate_cancelled_coalesced_edit(0x4d415205U, "focus loss") ||
        !validate_cancelled_coalesced_edit(0x4d415206U, "shutdown")) {
        std::cerr << "Focus-loss/shutdown did not cancel a coalesced edit cleanly.\n";
        return false;
    }
    shell_state.session.clear_history();

    if (!validate_derived_cache_smoke(&shell_state)) {
        return false;
    }

    if (!validate_animation_catalog_smoke(options.project_path) ||
        !validate_animation_duration_shell_smoke(options.project_path) ||
        !validate_viewport_camera_smoke(options.project_path) ||
        !validate_viewport_snap_smoke(options.project_path) ||
        !validate_viewport_prepared_scene_renderer_smoke(options.project_path)) {
        return false;
    }

    if (!validate_runtime_asset_hot_reload_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar180_failed_hot_reload_shell_coherence(shell_state)) {
        return false;
    }
    if (!validate_mar181_path_resolution_smoke()) {
        return false;
    }
    if (!validate_mar181_new_project_writes_nothing(shell_state)) {
        return false;
    }
    if (!validate_mar181_save_as_moves_the_shell_path(shell_state)) {
        return false;
    }
    if (!validate_mar181_failed_save_as_preserves_shell_path(shell_state)) {
        return false;
    }
    if (!validate_mar181_failed_open_preserves_shell(shell_state)) {
        return false;
    }
    if (!validate_mar181_save_shortcut_smoke(options.project_path)) {
        return false;
    }
    if (!validate_mar181_file_menu_mouse_smoke(options.project_path)) {
        return false;
    }
    if (!validate_mar182_intent_gate_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar182_save_completes_intent_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar182_failed_save_holds_intent_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar182_save_path_cancel_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar182_discard_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar182_cancel_and_repeat_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar182_close_request_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar182_dirty_prompt_mouse_smoke(options.project_path)) {
        return false;
    }
    if (!validate_mar183_shell_list_algebra_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar183_recent_gate_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar183_recording_policy_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar183_missing_entries_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar183_non_interference_smoke(shell_state)) {
        return false;
    }
    if (!validate_mar183_recent_menu_mouse_smoke(options.project_path)) {
        return false;
    }
    // C11 arms here and is asserted after render_headless_smoke_frames.
    if (!validate_mar181_arm_deferred_action_for_frame_body(&shell_state)) {
        return false;
    }
    if (!validate_mar180_failed_shell_save_preserves_file(shell_state)) {
        return false;
    }
    return true;
}

} // namespace marrow::editor::shell
