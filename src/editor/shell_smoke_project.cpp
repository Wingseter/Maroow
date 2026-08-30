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
#include "shell_preview.hpp"
#include "shell_selection.hpp"
#include "shell_timeline.hpp"
#include "shell_weight_paint.hpp"
#include "shell_viewport_ui.hpp"
#include "shell_state.hpp"
#include "viewport_renderer.hpp"
#include "marrow/allocator.hpp"
#include "marrow/editor/module.hpp"
#include "marrow/editor/authoring.hpp"
#include "marrow/editor/project.hpp"
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

    hot_reload_state.animation_state->clear_tracks();
    hot_reload_state.animation_state->set_animation(0, "idle", true, 0.0);
    hot_reload_state.animation_state->update(0.5);
    hot_reload_state.selected_animation_name = "idle";
    hot_reload_state.timeline_time_seconds = 0.5;
    if (!apply_current_animation_state_to_preview(&hot_reload_state)) {
        std::cerr << hot_reload_state.error_message << '\n';
        return false;
    }
    hot_reload_state.animation_state->set_animation(0, "attack", false, 0.2);
    hot_reload_state.animation_state->update(0.1);
    hot_reload_state.selected_animation_name = "attack";
    hot_reload_state.timeline_time_seconds = 0.1;
    hot_reload_state.timeline_playing = true;

    if (!apply_current_animation_state_to_preview(&hot_reload_state)) {
        std::cerr << hot_reload_state.error_message << '\n';
        return false;
    }

    std::shared_ptr<marrow::runtime::TrackEntry> current =
        hot_reload_state.animation_state->get_current(0);
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
            hot_reload_state.preview_skeleton->bone_poses()[*arm_index].local_pose.rotation);
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

    current = hot_reload_state.animation_state->get_current(0);
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
            hot_reload_state.preview_skeleton
                ->bone_poses()[*remapped_arm_index]
                .local_pose.rotation);
    if (std::abs(post_reload_rotation - 22.5) > 1e-3) {
        std::cerr << "Hot-reload smoke did not sample the updated attack pose after reload.\n";
        return false;
    }

    hot_reload_state.animation_state->update(1.0 / 60.0);
    hot_reload_state.timeline_time_seconds =
        hot_reload_state.animation_state->get_current(0)->track_time;
    if (!apply_current_animation_state_to_preview(&hot_reload_state)) {
        std::cerr << hot_reload_state.error_message << '\n';
        return false;
    }
    if (hot_reload_state.animation_state->get_current(0)->track_time <= pre_reload_track_time) {
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
    if (state.preview_skeleton == nullptr || state.animation_state == nullptr ||
        state.animation_state->get_current(0) == nullptr) {
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
        state.animation_state != nullptr
        ? state.animation_state->get_current(0U)
        : nullptr;
    if (current_before_queue_rebuild == nullptr ||
        current_before_queue_rebuild->animation_name != "aim" ||
        !begin_animation_duration_gesture(&state, "aim") ||
        !apply_animation_duration_gesture(&state, 0.6)) {
        std::cerr << "Animation duration queue smoke could not start from the primary clip.\n";
        return false;
    }
    const auto current_after_queue_rebuild =
        state.animation_state != nullptr
        ? state.animation_state->get_current(0U)
        : nullptr;
    if (current_after_queue_rebuild == nullptr ||
        current_after_queue_rebuild->animation_name != "attack" ||
        std::abs(state.timeline_time_seconds - 0.7) > 1e-6) {
        std::cerr << "Animation duration live preview retained the old queue boundary.\n";
        return false;
    }
    (void)finish_animation_duration_gesture(&state, false);
    const auto current_after_queue_cancel =
        state.animation_state != nullptr
        ? state.animation_state->get_current(0U)
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
    const marrow::runtime::Skeleton* preview_before = state.preview_skeleton;
    const marrow::runtime::AnimationState* animation_before = state.animation_state;

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
    if (state.preview_skeleton != preview_before ||
        state.animation_state != animation_before ||
        state.preview_skeleton == nullptr) {
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
    if (state.load_result.skeleton_data == nullptr || state.preview_skeleton == nullptr) {
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
        bool reload_requested = false;
        (void)draw_menu_bar(&reload_requested, &state);
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
        bool reload_requested = false;
        (void)draw_menu_bar(&reload_requested, &state);
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
        shell_state.preview_skeleton->bone_poses()[*setup_bone_index].local_pose;
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
