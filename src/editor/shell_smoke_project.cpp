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

#include "shell_constraints.hpp"
#include "shell_asset_watch.hpp"
#include "shell_agent_panel.hpp"
#include "shell_coalesced_edit.hpp"
#include "shell_derived_cache.hpp"
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
        !validate_viewport_prepared_scene_renderer_smoke(options.project_path)) {
        return false;
    }

    if (!validate_runtime_asset_hot_reload_smoke(shell_state)) {
        return false;
    }
    return true;
}

} // namespace marrow::editor::shell
