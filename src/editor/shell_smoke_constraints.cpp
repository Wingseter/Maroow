// MAR-178 -- headless coverage of the constraint rename and delete surfaces.
//
// Every MAR-178 case drives the UI-free helpers the modals call, never ImGui,
// so the confirmation flow, the selection cascade and the undo/redo reconcile
// are asserted without a frame being rendered.
//
// MAR-179 appends the opposite kind of scenario deliberately. Its deliverable
// IS "the widget is on the screen", and a UI-free assertion cannot observe a
// DELETED widget -- it would keep passing after the widget was removed. So
// validate_constraint_parameter_shell_smoke() renders real frames and aims a
// real mouse at the panel, exactly as the MAR-168/170 timeline scenarios do.

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "shell_constraints.hpp"
#include "shell_preferences.hpp"
#include "shell_preview.hpp"
#include "shell_selection.hpp"
#include "shell_smoke_scenarios.hpp"
#include "shell_state.hpp"

#include "marrow/editor/agent_dispatch.hpp"
#include "marrow/editor/constraint_catalog.hpp"
#include "marrow/editor/project.hpp"

namespace marrow::editor::shell {

namespace {

using marrow::editor::ConstraintKind;

/** @brief The four `(family, name)` upserts `player_idle.marrow` declares. */
struct FamilyFixture {
    ConstraintKind family;
    const char* name;
    const char* renamed;
};

constexpr std::array<FamilyFixture, 4> kFamilies{{
    {ConstraintKind::Ik, "editor_arm_reach", "arm_reach_v2"},
    {ConstraintKind::Path, "editor_guide_follow", "guide_follow_v2"},
    {ConstraintKind::Transform, "editor_transform_follow", "transform_follow_v2"},
    {ConstraintKind::Physics, "editor_ribbon_secondary", "ribbon_secondary_v2"},
}};

bool constraint_present(
    const marrow::runtime::SkeletonData& skeleton,
    ConstraintKind family,
    std::string_view name) {
    switch (family) {
    case ConstraintKind::Ik:
        return find_named_constraint(skeleton.ik_constraints(), name) != nullptr;
    case ConstraintKind::Path:
        return find_named_constraint(skeleton.path_constraints(), name) != nullptr;
    case ConstraintKind::Transform:
        return find_named_constraint(skeleton.transform_constraints(), name) != nullptr;
    case ConstraintKind::Physics:
        return find_named_constraint(skeleton.physics_constraints(), name) != nullptr;
    }
    return false;
}

std::size_t constraint_selection_count(
    const marrow::editor::SelectionSet& selection,
    std::string_view name) {
    std::size_t count = 0U;
    for (const auto& item : selection.items()) {
        const auto* constraint =
            std::get_if<marrow::editor::ConstraintSelection>(&item);
        if (constraint != nullptr && constraint->constraint_name == name) {
            ++count;
        }
    }
    return count;
}

bool bone_selected(
    const marrow::editor::SelectionSet& selection,
    std::string_view bone_name) {
    for (const auto& item : selection.items()) {
        const auto* bone = std::get_if<marrow::editor::BoneSelection>(&item);
        if (bone != nullptr && bone->bone_name == bone_name) {
            return true;
        }
    }
    return false;
}

std::string serialized(const ShellState& state) {
    return state.session.project() != nullptr
        ? marrow::editor::serialize_project(*state.session.project())
        : std::string{};
}

/**
 * @brief Writes a `.marrow` over `skin_inherit_constraints.mskl`.
 *
 * That fixture is the only one in the tree where a skin names a constraint, and
 * it ships no `.marrow` and no atlas, so the project borrows
 * `player_idle.matl`. Nothing cross-validates an atlas against a skeleton.
 */
bool write_skin_fixture_project(const std::filesystem::path& project_path) {
    marrow::editor::MinimalProjectOptions options;
    options.project_path = project_path;
    options.skeleton_path =
        std::filesystem::absolute("assets/fixtures/skin_inherit_constraints.mskl");
    options.atlas_paths = {
        std::filesystem::absolute("assets/fixtures/player_idle.matl")};
    options.name = "mar178_skin";
    const marrow::editor::ProjectData project =
        marrow::editor::create_minimal_project(options);
    const auto saved = marrow::editor::save_project(project, project_path);
    if (!saved) {
        std::cerr << "MAR-178 shell smoke could not write " << project_path << ": "
                  << saved.error->message << '\n';
        return false;
    }
    return true;
}

} // namespace

bool validate_constraint_lifecycle_shell_smoke(
    const std::filesystem::path& project_path) {
    const ScopedPreferenceIsolation isolation("constraint-lifecycle");
    if (!isolation.installed()) {
        std::cerr << "Constraint lifecycle shell smoke could not isolate "
                     "MARROW_CONFIG_HOME.\n";
        return false;
    }

    const std::size_t operation_count_before =
        marrow::editor::agent_operation_descriptor_count();
    if (operation_count_before != 64U) {
        std::cerr << "Constraint lifecycle shell smoke requires the exact 64-operation registry.\n";
        return false;
    }

    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) || state.load_result.skeleton_data == nullptr) {
        std::cerr << "Constraint lifecycle shell smoke could not load "
                  << project_path << ".\n";
        return false;
    }
    state.session.clear_history();

    // --- Case 1: a cancelled confirmation begins no transaction. -----------
    {
        const std::string before = serialized(state);
        const std::size_t undo_before = state.session.undo_count();
        request_constraint_delete(
            &state, ConstraintKind::Ik, "editor_arm_reach");
        cancel_constraint_catalog(&state);
        if (state.session.undo_count() != undo_before || serialized(state) != before) {
            std::cerr << "MAR-178 shell case 1: cancelling the delete confirmation "
                         "must begin no transaction; undo_count "
                      << undo_before << " -> " << state.session.undo_count() << ".\n";
            return false;
        }
        // A confirmed delete applies, and a cancelled one having left the popup
        // state clear means confirm must now refuse.
        if (confirm_constraint_delete(&state)) {
            std::cerr << "MAR-178 shell case 1: confirming after a cancel must not "
                         "apply an abandoned request.\n";
            return false;
        }
        request_constraint_delete(
            &state, ConstraintKind::Ik, "editor_arm_reach");
        if (!confirm_constraint_delete(&state)) {
            std::cerr << "MAR-178 shell case 1: a confirmed delete must apply: "
                      << state.error_message << '\n';
            return false;
        }
        if (constraint_present(
                *state.load_result.skeleton_data, ConstraintKind::Ik,
                "editor_arm_reach") ||
            state.session.undo_count() != undo_before + 1U) {
            std::cerr << "MAR-178 shell case 1: the confirmed delete did not apply as "
                         "exactly one history entry.\n";
            return false;
        }
        if (!undo_project_change(&state)) {
            std::cerr << "MAR-178 shell case 1: could not undo the confirmed delete.\n";
            return false;
        }
        if (serialized(state) != before) {
            std::cerr << "MAR-178 shell case 1: undo did not restore the exact "
                         "pre-delete serialization.\n";
            return false;
        }
    }

    // --- Case 2: a rename carries the selection with it. -------------------
    {
        select_constraint(
            &state, ConstraintKind::Transform, "editor_transform_follow",
            "MAR-178 smoke", false);
        const std::size_t members_before = state.selection.items().size();
        if (!apply_constraint_catalog_action(
                &state, ConstraintCatalogAction::Rename, ConstraintKind::Transform,
                "editor_transform_follow", "transform_follow_v2")) {
            std::cerr << "MAR-178 shell case 2: the rename was refused: "
                      << state.error_message << '\n';
            return false;
        }
        const auto* active = state.selection.active_constraint();
        if (active == nullptr || active->constraint_name != "transform_follow_v2") {
            std::cerr << "MAR-178 shell case 2: the selection did not follow the "
                         "rename. rebuild_project_runtime() does not reconcile, and "
                         "reconcile can only prune, so without the remap the panel "
                         "falls back to \"Select a Transform constraint to edit it.\" "
                         "while the constraint is still there under its new name.\n";
            return false;
        }
        if (state.selection.items().size() != members_before) {
            std::cerr << "MAR-178 shell case 2: a rename must not change the "
                         "selection size.\n";
            return false;
        }
        if (!undo_project_change(&state)) {
            std::cerr << "MAR-178 shell case 2: could not undo the rename.\n";
            return false;
        }
    }

    // --- Case 3: a delete prunes the selection instead of leaving a ghost. --
    {
        state.selection.clear();
        state.selection.replace(marrow::editor::BoneSelection{"root"});
        state.selection.toggle(marrow::editor::ConstraintSelection{
            ConstraintKind::Path, "editor_guide_follow"});
        const std::size_t members_before = state.selection.items().size();
        if (!apply_constraint_catalog_action(
                &state, ConstraintCatalogAction::Delete, ConstraintKind::Path,
                "editor_guide_follow", {})) {
            std::cerr << "MAR-178 shell case 3: the delete was refused: "
                      << state.error_message << '\n';
            return false;
        }
        if (state.selection.items().size() != members_before - 1U) {
            std::cerr << "MAR-178 shell case 3: the deleted constraint left a ghost. "
                         "The status line reports items().size() as \"; N selected\", "
                         "so the ghost is user-visible as an inflated count; measured "
                      << state.selection.items().size() << " against "
                      << members_before << ".\n";
            return false;
        }
        if (constraint_selection_count(state.selection, "editor_guide_follow") != 0U) {
            std::cerr << "MAR-178 shell case 3: a ConstraintSelection for the deleted "
                         "name survived.\n";
            return false;
        }
        if (!bone_selected(state.selection, "root")) {
            std::cerr << "MAR-178 shell case 3: the co-selected bone must survive a "
                         "constraint delete.\n";
            return false;
        }
    }

    // --- Case 4: undo restores the project, drops the constraint ghost the ---
    //     undo itself creates, and leaves every other selection kind alone.
    {
        if (!undo_project_change(&state)) {
            std::cerr << "MAR-178 shell case 4: could not undo the delete.\n";
            return false;
        }
        if (!constraint_present(
                *state.load_result.skeleton_data, ConstraintKind::Path,
                "editor_guide_follow")) {
            std::cerr << "MAR-178 shell case 4: undo did not restore the deleted "
                         "constraint.\n";
            return false;
        }
        if (constraint_selection_count(state.selection, "editor_guide_follow") != 0U) {
            std::cerr << "MAR-178 shell case 4: undo must NOT restore the selection. "
                         "EditorHistorySnapshot carries no SelectionSet, and adding "
                         "one reproduces MAR-174's Ctrl+Z bounce.\n";
            return false;
        }

        // A rename followed by an undo is where the ghost actually comes from:
        // the cascade moved the selection to the new name, and the undo puts the
        // old name back, so the selection now names something that no longer
        // resolves. `rebuild_project_runtime()` does not reconcile, so without
        // the undo-path call the ghost survives and inflates "; N selected".
        //
        // `phantom_bone` is co-selected deliberately: it does not resolve
        // either, so it discriminates the NARROW reconcile from
        // `reconcile_selection_to_runtime()`, which would prune it and thereby
        // change bone-selection behaviour after every undo in the editor.
        state.selection.clear();
        state.selection.replace(marrow::editor::BoneSelection{"root"});
        state.selection.toggle(marrow::editor::BoneSelection{"phantom_bone"});
        state.selection.toggle(marrow::editor::ConstraintSelection{
            ConstraintKind::Ik, "editor_arm_reach"});
        if (!apply_constraint_catalog_action(
                &state, ConstraintCatalogAction::Rename, ConstraintKind::Ik,
                "editor_arm_reach", "arm_reach_ghost")) {
            std::cerr << "MAR-178 shell case 4: the rename was refused: "
                      << state.error_message << '\n';
            return false;
        }
        if (constraint_selection_count(state.selection, "arm_reach_ghost") != 1U) {
            std::cerr << "MAR-178 shell case 4: the selection did not follow the "
                         "rename.\n";
            return false;
        }
        if (!undo_project_change(&state)) {
            std::cerr << "MAR-178 shell case 4: could not undo the rename.\n";
            return false;
        }
        if (constraint_selection_count(state.selection, "arm_reach_ghost") != 0U) {
            std::cerr << "MAR-178 shell case 4: undoing a rename left the renamed "
                         "constraint selected as a ghost. Nothing else reconciles "
                         "after an ordinary edit, so the status line would report it "
                         "in \"; N selected\" until the next full reload.\n";
            return false;
        }
        if (!bone_selected(state.selection, "root") ||
            !bone_selected(state.selection, "phantom_bone")) {
            std::cerr << "MAR-178 shell case 4: the undo-path reconcile pruned a bone "
                         "selection. reconcile_constraint_selection() must leave every "
                         "non-constraint alternative alone -- widening it to "
                         "reconcile_selection_to_runtime() prunes bone, slot and "
                         "attachment selections after EVERY undo in the editor, which "
                         "is a behaviour change far outside this story.\n";
            return false;
        }

        // Redo re-applies the rename; the selection is not restored either way.
        if (!redo_project_change(&state)) {
            std::cerr << "MAR-178 shell case 4: could not redo the rename.\n";
            return false;
        }
        if (!constraint_present(
                *state.load_result.skeleton_data, ConstraintKind::Ik,
                "arm_reach_ghost")) {
            std::cerr << "MAR-178 shell case 4: redo did not re-apply the rename.\n";
            return false;
        }
        if (!bone_selected(state.selection, "phantom_bone")) {
            std::cerr << "MAR-178 shell case 4: the redo-path reconcile pruned a bone "
                         "selection.\n";
            return false;
        }
        if (!undo_project_change(&state)) {
            std::cerr << "MAR-178 shell case 4: could not undo the redone rename.\n";
            return false;
        }
    }

    // --- Case 5: every family renames and deletes, and its neighbours live. -
    {
        for (const FamilyFixture& fixture : kFamilies) {
            ShellState family_state;
            family_state.project_path = project_path;
            if (!reload_project(&family_state)) {
                std::cerr << "MAR-178 shell case 5: could not reload for "
                          << fixture.name << ".\n";
                return false;
            }
            family_state.session.clear_history();

            if (!apply_constraint_catalog_action(
                    &family_state, ConstraintCatalogAction::Rename, fixture.family,
                    fixture.name, fixture.renamed)) {
                std::cerr << "MAR-178 shell case 5: renaming " << fixture.name
                          << " was refused: " << family_state.error_message << '\n';
                return false;
            }
            if (!constraint_present(
                    *family_state.load_result.skeleton_data, fixture.family,
                    fixture.renamed)) {
                std::cerr << "MAR-178 shell case 5: " << fixture.renamed
                          << " did not materialize after the rename.\n";
                return false;
            }
            if (!apply_constraint_catalog_action(
                    &family_state, ConstraintCatalogAction::Delete, fixture.family,
                    fixture.renamed, {})) {
                std::cerr << "MAR-178 shell case 5: deleting " << fixture.renamed
                          << " was refused: " << family_state.error_message << '\n';
                return false;
            }
            if (constraint_present(
                    *family_state.load_result.skeleton_data, fixture.family,
                    fixture.renamed)) {
                std::cerr << "MAR-178 shell case 5: " << fixture.renamed
                          << " survived its own delete.\n";
                return false;
            }
            // Deleting the last constraint of a family must succeed -- there is
            // no last-constraint gate -- and the other three families must be
            // untouched by it.
            for (const FamilyFixture& neighbour : kFamilies) {
                if (neighbour.family == fixture.family) continue;
                if (!constraint_present(
                        *family_state.load_result.skeleton_data, neighbour.family,
                        neighbour.name)) {
                    std::cerr << "MAR-178 shell case 5: editing " << fixture.name
                              << " dropped the unrelated " << neighbour.name << ".\n";
                    return false;
                }
            }
        }
    }

    // --- Case 6: a base-backed rename rewrites the skin reference. ---------
    std::string collision_message;
    std::string atlas_message;
    {
        const auto skin_project_path =
            std::filesystem::temp_directory_path() / "marrow_mar178_shell_skin.marrow";
        std::error_code ignored;
        std::filesystem::remove(skin_project_path, ignored);
        if (!write_skin_fixture_project(skin_project_path)) {
            return false;
        }

        ShellState skin_state;
        skin_state.project_path = skin_project_path;
        if (!reload_project(&skin_state) ||
            skin_state.load_result.skeleton_data == nullptr) {
            std::cerr << "MAR-178 shell case 6: could not load the skin fixture "
                         "project.\n";
            return false;
        }
        skin_state.session.clear_history();

        const auto& affected = marrow::editor::constraint_affected_skins(
            *skin_state.load_result.skeleton_data, ConstraintKind::Transform,
            "cape_pull");
        if (affected.size() != 1U || affected.front() != "cape") {
            std::cerr << "MAR-178 shell case 6: the affected-skin summary must report "
                         "exactly [cape]; measured "
                      << affected.size() << " entries.\n";
            return false;
        }

        if (!apply_constraint_catalog_action(
                &skin_state, ConstraintCatalogAction::Rename, ConstraintKind::Transform,
                "cape_pull", "cape_drag")) {
            std::cerr << "MAR-178 shell case 6: the base-backed rename was refused: "
                      << skin_state.error_message << '\n';
            return false;
        }
        const marrow::runtime::SkeletonData& rebuilt =
            *skin_state.load_result.skeleton_data;
        const auto skin = std::find_if(
            rebuilt.skins().begin(), rebuilt.skins().end(),
            [](const marrow::runtime::SkinData& candidate) {
                return candidate.name == "cape";
            });
        if (skin == rebuilt.skins().end() ||
            skin->transform_constraint_indices.size() != 1U) {
            std::cerr << "MAR-178 shell case 6: skin `cape` must still resolve exactly "
                         "one transform constraint after the rename.\n";
            return false;
        }
        const std::size_t index = skin->transform_constraint_indices.front();
        if (index >= rebuilt.transform_constraints().size() ||
            rebuilt.transform_constraints()[index].name != "cape_drag") {
            std::cerr << "MAR-178 shell case 6: skin `cape` resolves a transform "
                         "constraint that is not the renamed one. A root-only rewrite "
                         "makes the project unopenable, not merely wrong.\n";
            return false;
        }

        // --- Case 7: a collision through the shell path is refused, and the -
        //     message is the primitive's own, for comparison with the agent.
        if (apply_constraint_catalog_action(
                &skin_state, ConstraintCatalogAction::Rename, ConstraintKind::Transform,
                "cape_drag", "cape_drag")) {
            std::cerr << "MAR-178 shell case 7: an unchanged rename must be refused.\n";
            return false;
        }
        collision_message = skin_state.error_message;
        if (collision_message.empty()) {
            std::cerr << "MAR-178 shell case 7: a refused rename must surface the "
                         "primitive's message.\n";
            return false;
        }

        // --- Case 8: delete, save, and RELOAD. -----------------------------
        //
        // `validate_project_for_save()` takes no base document
        // (`project.cpp:5502`+), so it cannot resolve a skin reference against a
        // skeleton and a successful save proves nothing. `reload_project()` goes
        // through `load_project()`, which calls `build_project_runtime()`
        // (`project.cpp:7495`) and `load_skeleton_data`. The RELOAD is the
        // assertion.
        if (!apply_constraint_catalog_action(
                &skin_state, ConstraintCatalogAction::Delete, ConstraintKind::Transform,
                "cape_drag", {})) {
            std::cerr << "MAR-178 shell case 8: the base-backed delete was refused: "
                      << skin_state.error_message << '\n';
            return false;
        }
        if (!save_project_file(&skin_state, false)) {
            std::cerr << "MAR-178 shell case 8: the project failed to save after the "
                         "delete.\n";
            return false;
        }
        if (!reload_project(&skin_state) ||
            skin_state.load_result.skeleton_data == nullptr) {
            std::cerr << "MAR-178 shell case 8: the project SAVED but could not be "
                         "REOPENED: "
                      << skin_state.error_message
                      << "\n           A delete that leaves a dangling "
                         "skins[*].<family> reference is invisible at save time and "
                         "fails only here.\n";
            return false;
        }
        if (!skin_state.load_result.skeleton_data->transform_constraints().empty()) {
            std::cerr << "MAR-178 shell case 8: the reloaded rig still carries a "
                         "transform constraint.\n";
            return false;
        }
        {
            const auto& skins = skin_state.load_result.skeleton_data->skins();
            const auto reloaded_cape = std::find_if(
                skins.begin(), skins.end(),
                [](const marrow::runtime::SkinData& candidate) {
                    return candidate.name == "cape";
                });
            if (reloaded_cape == skins.end() ||
                !reloaded_cape->transform_constraint_indices.empty()) {
                std::cerr << "MAR-178 shell case 8: skin `cape` must survive the "
                             "reload with no transform constraint indices.\n";
                return false;
            }
        }

        std::filesystem::remove(skin_project_path, ignored);
    }

    // --- Case 9: the atlas message (spec §4.2). ----------------------------
    //
    // The refusal's text comes from `validate_project_for_save()`. Surfaced raw
    // under a "Rename Transform constraint" modal it reads as a non-sequitur, so
    // the surface adds a clause naming what was refused -- and keeps the
    // validator's own sentence verbatim, so a grep for that text still finds
    // every path that can produce it.
    //
    // The rename must name a constraint that EXISTS, or the primitive rejects at
    // the name preflight and never reaches step 5's save validator at all.
    {
        const auto atlas_project_path =
            std::filesystem::temp_directory_path() / "marrow_mar178_shell_atlas.marrow";
        std::error_code ignored;
        std::filesystem::remove(atlas_project_path, ignored);
        if (!write_skin_fixture_project(atlas_project_path)) {
            return false;
        }
        ShellState atlas_state;
        atlas_state.project_path = atlas_project_path;
        if (!reload_project(&atlas_state)) {
            std::cerr << "MAR-178 shell case 9: could not load the atlas fixture "
                         "project.\n";
            return false;
        }
        atlas_state.session.clear_history();
        {
            auto strip = atlas_state.session.begin_edit(
                {marrow::editor::EditKind::EditProperty, "Emptied atlas list",
                 "constraint-catalog", false,
                 marrow::editor::EditImpact::Project |
                     marrow::editor::EditImpact::Runtime |
                     marrow::editor::EditImpact::Preview});
            if (!strip) {
                std::cerr << "MAR-178 shell case 9 could not open a transaction.\n";
                return false;
            }
            strip.project()->runtime_assets.atlas_paths.clear();
            if (!strip.commit()) {
                std::cerr << "MAR-178 shell case 9 could not empty the atlas list.\n";
                return false;
            }
        }

        const std::string atlas_serialized_before = serialized(atlas_state);
        const std::size_t atlas_undo_before = atlas_state.session.undo_count();
        if (apply_constraint_catalog_action(
                &atlas_state, ConstraintCatalogAction::Rename, ConstraintKind::Transform,
                "cape_pull", "cape_drag")) {
            std::cerr << "MAR-178 shell case 9: a rename on an atlas-free project must "
                         "be refused.\n";
            return false;
        }
        if (atlas_state.error_message.find("at least one atlas path is required") ==
            std::string::npos) {
            std::cerr << "MAR-178 shell case 9: the validator's sentence must survive "
                         "the surface VERBATIM; measured '"
                      << atlas_state.error_message << "'.\n";
            return false;
        }
        if (atlas_state.error_message.find(
                "Cannot rename: the project must reference at least one atlas before "
                "it can be saved. ") != 0U) {
            std::cerr << "MAR-178 shell case 9: the surface's clause must sit in FRONT "
                         "of the validator's sentence, so the user learns which of the "
                         "two facts is wrong; measured '"
                      << atlas_state.error_message << "'.\n";
            return false;
        }
        atlas_message = atlas_state.error_message;
        if (atlas_state.session.undo_count() != atlas_undo_before ||
            serialized(atlas_state) != atlas_serialized_before) {
            std::cerr << "MAR-178 shell case 9: the atlas refusal changed the project "
                         "or the history.\n";
            return false;
        }
        std::filesystem::remove(atlas_project_path, ignored);
    }

    std::cout << "MAR-178 constraint lifecycle shell smoke: cancel begins no "
                 "transaction, rename carries the selection, delete prunes it "
                 "without a ghost, undo restores the project but not the selection "
                 "while the co-selected bone survives, all four families rename and "
                 "delete down to an empty family, a base-backed rename rewrites "
                 "skins.cape, a delete SAVES AND RELOADS with skins.cape carrying no "
                 "transform indices, a refused rename reports \""
              << collision_message << "\", and an atlas-free rename reports \""
              << atlas_message << "\".\n";
    return true;
}

namespace {

/**
 * @brief One parameter widget the panel must emit, and where the mouse found it.
 *
 * `id` is seeded from the Constraints window itself. That is only correct
 * because every parameter widget is emitted at plain window scope: the family
 * strip is `widgets::seg_toggle`, whose `PushID`/`PopID` are balanced, there is
 * no `BeginTabItem` anywhere in `shell_constraints.cpp`, and each family's
 * `BeginChild` list is closed with `EndChild` BEFORE the parameters. If that
 * ever changes, `window->GetID(label)` stops matching and every case below
 * fails loudly -- which is the intended failure mode, not a silent skip.
 */
struct WidgetProbe {
    const char* label;
    ImGuiID id{0};
    ImVec2 position{0.0f, 0.0f};
    float scroll_y{0.0f};
    bool found{false};
};

} // namespace

bool validate_constraint_parameter_shell_smoke(
    const std::filesystem::path& project_path) {
    const ScopedPreferenceIsolation isolation("constraint-parameters");
    if (!isolation.installed()) {
        std::cerr << "Constraint parameter shell smoke could not isolate "
                     "MARROW_CONFIG_HOME.\n";
        return false;
    }

    const std::size_t operation_count_before =
        marrow::editor::agent_operation_descriptor_count();
    if (operation_count_before != 64U) {
        std::cerr << "Constraint parameter shell smoke requires the exact 64-operation registry.\n";
        return false;
    }

    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) || state.load_result.skeleton_data == nullptr) {
        std::cerr << "Constraint parameter shell smoke could not load "
                  << project_path << ".\n";
        return false;
    }
    state.session.clear_history();

    ImGuiIO& io = ImGui::GetIO();
    const auto render_frame = [&]() {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        // A fixed, generous rectangle so the panel is scrollable rather than
        // clipped away, and so the sweep below has a stable coordinate frame.
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(460.0f, 880.0f), ImGuiCond_Always);
        draw_constraints_window(&state);
        ImGui::Render();
    };

    /**
     * @brief Sweeps a real mouse down the panel and records what it hovered.
     *
     * `ImGuiContext::HoveredId` names whatever the cursor is over. Production
     * already reads the context this way (`shell_core.cpp` uses `GetActiveID()`
     * and `ActiveIdIsAlive`). A label that is never hovered at any scanned
     * position is a FAILURE, never a skip: a widget that is not emitted cannot
     * own an id equal to `window->GetID(label)`.
     */
    const auto sweep_for_widgets = [&](std::vector<WidgetProbe>& probes,
                                       const char* case_label,
                                       const char* window_title =
                                           kConstraintsWindowTitle) -> bool {
        ImGuiWindow* window = ImGui::FindWindowByName(window_title);
        if (window == nullptr) {
            std::cerr << "MAR-179 shell " << case_label
                      << ": the Constraints window was never submitted.\n";
            return false;
        }
        for (WidgetProbe& probe : probes) {
            probe.id = window->GetID(probe.label);
            probe.found = false;
        }
        ImGui::SetScrollY(window, 0.0f);
        render_frame();

        for (int pass = 0; pass < 24; ++pass) {
            window = ImGui::FindWindowByName(window_title);
            if (window == nullptr) {
                std::cerr << "MAR-179 shell " << case_label
                          << ": lost the Constraints window mid-sweep.\n";
                return false;
            }
            const float scroll_y = window->Scroll.y;
            const float first_y = window->InnerClipRect.Min.y + 2.0f;
            const float last_y = window->InnerClipRect.Max.y - 2.0f;
            const float item_x = window->WorkRect.Min.x;
            std::size_t missing = 0U;
            for (const WidgetProbe& probe : probes) {
                if (!probe.found) ++missing;
            }
            if (missing == 0U) return true;

            for (float y = first_y; y <= last_y; y += 4.0f) {
                // Three columns. A checkbox owns only its small square at the
                // item's left edge and a slider owns the whole frame, so both
                // contain the +6 column, with +26 as a cheap second chance. The
                // +110 column exists for `Delete...`, which sits on a SameLine()
                // to the RIGHT of `Rename...` and is the one widget here that no
                // left-edge column can reach.
                for (const float dx : {6.0f, 26.0f, 110.0f}) {
                    io.AddMousePosEvent(item_x + dx, y);
                    render_frame();
                    const ImGuiContext* context = ImGui::GetCurrentContext();
                    const ImGuiID hovered = context != nullptr ? context->HoveredId : 0U;
                    if (hovered == 0U) continue;
                    for (WidgetProbe& probe : probes) {
                        if (!probe.found && probe.id == hovered) {
                            probe.found = true;
                            probe.position = ImVec2(item_x + dx, y);
                            probe.scroll_y = scroll_y;
                        }
                    }
                }
            }

            window = ImGui::FindWindowByName(window_title);
            if (window == nullptr) return false;
            const float next_scroll =
                std::min(window->ScrollMax.y, window->Scroll.y + 240.0f);
            if (next_scroll == window->Scroll.y) break;
            ImGui::SetScrollY(window, next_scroll);
            render_frame();
        }

        bool complete = true;
        for (const WidgetProbe& probe : probes) {
            if (!probe.found) {
                std::cerr << "MAR-179 shell " << case_label << ": \"" << window_title
                          << "\" never emitted a widget with the id of \"" << probe.label
                          << "\". A real mouse swept every row of the Constraints "
                             "window at every scroll offset and HoveredId never "
                             "equalled window->GetID(\"" << probe.label
                          << "\"), so the widget is absent -- this is the one "
                             "observation a UI-free assertion cannot make.\n";
                complete = false;
            }
        }
        return complete;
    };

    const auto restore_scroll = [&](float scroll_y) {
        ImGuiWindow* window = ImGui::FindWindowByName(kConstraintsWindowTitle);
        if (window != nullptr) {
            ImGui::SetScrollY(window, scroll_y);
        }
        render_frame();
    };

    // Two presses at the same pixel inside io.MouseDoubleClickTime are a
    // DOUBLE CLICK, and ImGui turns a double-clicked DragScalar into a text
    // input instead of dragging it (imgui_widgets.cpp, DragBehavior's
    // `double_clicked` branch). Measured: the second Softness drag left the
    // widget ACTIVE as a temp input and the value unchanged. Advancing the
    // simulated clock past the window is what makes a repeated gesture a
    // second gesture rather than a double click.
    const auto settle = [&]() {
        for (int frame = 0; frame < 3; ++frame) {
            io.DeltaTime = 0.25f;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(460.0f, 880.0f), ImGuiCond_Always);
            draw_constraints_window(&state);
            ImGui::Render();
        }
    };

    const auto drag_widget = [&](const WidgetProbe& probe, float dx, int steps) {
        settle();
        restore_scroll(probe.scroll_y);
        io.AddMousePosEvent(probe.position.x, probe.position.y);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_frame();
        for (int step = 1; step <= steps; ++step) {
            io.AddMousePosEvent(
                probe.position.x + dx * static_cast<float>(step), probe.position.y);
            render_frame();
        }
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_frame();
    };

    const auto click_position = [&](ImVec2 position) {
        settle();
        io.AddMousePosEvent(position.x, position.y);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_frame();
        render_frame();
    };

    const auto click_widget = [&](const WidgetProbe& probe) {
        settle();
        restore_scroll(probe.scroll_y);
        io.AddMousePosEvent(probe.position.x, probe.position.y);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_frame();
    };

    // --- C1 (IK half): Softness, Compress and Stretch are on the screen. ---
    state.constraints_tab = 0;
    select_constraint(
        &state, ConstraintKind::Ik, "editor_arm_reach", "MAR-179 smoke", false);
    render_frame();
    render_frame();

    // "Mix" and "Bend Positive" shipped long before MAR-179 and are POSITIVE
    // CONTROLS: they keep C1 from being a case that can only ever fail. If the
    // window-scope id seed were wrong, or the sweep never reached the rows, the
    // controls would go missing too and say so by name.
    // `Rename...` and `Delete...` are MAR-178's catalog buttons, carried into
    // this list on the MAR-178 reviewer's recommendation. MAR-178 asserted them
    // by CALLING `request_/confirm_/cancel_constraint_*` directly, so its tests
    // would have passed unchanged if `draw_constraint_catalog_buttons()` were
    // deleted outright -- a construction-based assertion cannot observe a
    // missing widget. They are emitted by the same plain `ImGui::Button` at the
    // same window scope as everything else here, so covering them costs two
    // list entries plus C6 below.
    std::vector<WidgetProbe> ik_probes{
        {"Mix"},     {"Bend Positive"}, {"Softness"}, {"Compress"},
        {"Stretch"}, {"Rename..."},     {"Delete..."}};
    constexpr std::size_t kIkSoftness = 2U;
    constexpr std::size_t kIkCompress = 3U;
    constexpr std::size_t kIkStretch = 4U;
    constexpr std::size_t kIkRenameButton = 5U;
    constexpr std::size_t kIkDeleteButton = 6U;
    if (!sweep_for_widgets(ik_probes, "case C1 (IK)")) {
        return false;
    }

    const auto ik_edit_index = [&]() -> std::size_t {
        const auto* project = state.session.project();
        if (project == nullptr) return static_cast<std::size_t>(-1);
        for (std::size_t index = 0; index < project->ik_constraint_edits.size(); ++index) {
            if (project->ik_constraint_edits[index].name == "editor_arm_reach") {
                return index;
            }
        }
        return static_cast<std::size_t>(-1);
    };

    // --- C2: each Softness drag is exactly one history entry. --------------
    //
    // TWO successive drags, not one. A single drag would still show one entry
    // even with allow_merge = true, because there is no earlier entry in the
    // same group to merge INTO -- so a one-drag case could not observe the
    // merge flag at all. The second drag is what makes it observable.
    {
        const std::string before = serialized(state);
        const std::size_t undo_before = state.session.undo_count();
        const std::size_t index = ik_edit_index();
        if (index == static_cast<std::size_t>(-1)) {
            std::cerr << "MAR-179 shell case C2: the fixture must carry an "
                         "editor_arm_reach IK constraint edit.\n";
            return false;
        }
        double previous_softness =
            state.session.project()->ik_constraint_edits[index].softness;
        for (int drag = 1; drag <= 2; ++drag) {
            drag_widget(ik_probes[kIkSoftness], 8.0f, 5);
            const std::size_t index_after = ik_edit_index();
            if (index_after == static_cast<std::size_t>(-1) ||
                state.session.project()->ik_constraint_edits[index_after].softness ==
                    previous_softness) {
                std::cerr << "MAR-179 shell case C2: Softness drag " << drag
                          << " did not change the value (still " << previous_softness
                          << ").\n";
                return false;
            }
            previous_softness =
                state.session.project()->ik_constraint_edits[index_after].softness;
            if (state.session.undo_count() !=
                undo_before + static_cast<std::size_t>(drag)) {
                std::cerr << "MAR-179 shell case C2: Softness drag " << drag
                          << " must coalesce into EXACTLY one history entry and must "
                             "NOT merge into the previous drag; undo_count "
                          << undo_before << " -> " << state.session.undo_count()
                          << ", expected " << (undo_before + drag) << ".\n";
                return false;
            }
        }
        if (!undo_project_change(&state) || !undo_project_change(&state) ||
            serialized(state) != before) {
            std::cerr << "MAR-179 shell case C2: two undos did not restore the exact "
                         "pre-drag serialization.\n";
            return false;
        }
        render_frame();
    }

    // --- C4: the checkboxes reach the agent. ------------------------------
    {
        for (std::size_t probe_index = kIkCompress; probe_index <= kIkStretch;
             ++probe_index) {
            const WidgetProbe& probe = ik_probes[probe_index];
            const std::string before = serialized(state);
            const std::size_t undo_before = state.session.undo_count();
            const std::size_t index = ik_edit_index();
            if (index == static_cast<std::size_t>(-1)) return false;
            const bool value_before = probe_index == kIkCompress
                ? state.session.project()->ik_constraint_edits[index].compress
                : state.session.project()->ik_constraint_edits[index].stretch;
            click_widget(probe);
            const std::size_t index_after = ik_edit_index();
            if (index_after == static_cast<std::size_t>(-1)) return false;
            const bool value_after = probe_index == kIkCompress
                ? state.session.project()->ik_constraint_edits[index_after].compress
                : state.session.project()->ik_constraint_edits[index_after].stretch;
            if (value_after == value_before) {
                std::cerr << "MAR-179 shell case C4: clicking \"" << probe.label
                          << "\" did not flip the project value. The checkbox "
                             "dispatches edit_ik_constraint, so this fails both when "
                             "the widget is missing and when the agent does not read "
                             "the argument.\n";
                return false;
            }
            if (state.session.undo_count() != undo_before + 1U) {
                std::cerr << "MAR-179 shell case C4: \"" << probe.label
                          << "\" must produce exactly one history entry; undo_count "
                          << undo_before << " -> " << state.session.undo_count()
                          << ".\n";
                return false;
            }
            if (!undo_project_change(&state) || serialized(state) != before) {
                std::cerr << "MAR-179 shell case C4: undoing the \"" << probe.label
                          << "\" click did not restore the exact serialization.\n";
                return false;
            }
            if (!redo_project_change(&state)) {
                std::cerr << "MAR-179 shell case C4: could not redo the \""
                          << probe.label << "\" click.\n";
                return false;
            }
            const std::size_t index_redone = ik_edit_index();
            if (index_redone == static_cast<std::size_t>(-1)) return false;
            const bool value_redone = probe_index == kIkCompress
                ? state.session.project()->ik_constraint_edits[index_redone].compress
                : state.session.project()->ik_constraint_edits[index_redone].stretch;
            if (value_redone != value_after) {
                std::cerr << "MAR-179 shell case C4: redo did not restore the \""
                          << probe.label << "\" value.\n";
                return false;
            }
            if (!undo_project_change(&state)) return false;
            render_frame();
        }
    }

    // --- C6: MAR-178's catalog buttons, driven by a real mouse. ------------
    //
    // Carried into MAR-179 on the MAR-178 reviewer's recommendation. MAR-178
    // asserted `Rename...` and `Delete...` by CALLING
    // `request_/confirm_/cancel_constraint_*` directly, so its tests would have
    // passed unchanged had `draw_constraint_catalog_buttons()` been deleted
    // outright. C1 above now covers the two buttons' presence; this covers that
    // a real click opens the modal, and — for delete, which needs no typing —
    // that the modal's own button applies the edit. The whole round trip is
    // mouse-driven; no `confirm_*` helper is called anywhere in this case.
    {
        // These mirror the anonymous-namespace constants in
        // `shell_constraints.cpp`. A popup's ImGui window name is the full
        // string passed to BeginPopupModal, `##` suffix included. If either
        // drifts, the modal is not found and the case fails by name.
        static constexpr char kRenameModal[] =
            "Rename Constraint##constraint_catalog";
        static constexpr char kDeleteModal[] =
            "Delete Constraint##constraint_catalog";

        const auto modal_is_open = [&](const char* title) {
            const ImGuiWindow* window = ImGui::FindWindowByName(title);
            return window != nullptr && window->Active;
        };

        // --- C6a: Delete... -> modal -> the modal's Delete button. ----------
        select_constraint(
            &state, ConstraintKind::Ik, "editor_arm_reach", "MAR-179 smoke", false);
        cancel_constraint_catalog(&state);
        render_frame();
        render_frame();

        const std::string delete_before = serialized(state);
        const std::size_t delete_undo_before = state.session.undo_count();
        click_widget(ik_probes[kIkDeleteButton]);
        if (!modal_is_open(kDeleteModal)) {
            std::cerr << "MAR-179 shell case C6a: a real click on \"Delete...\" did "
                         "not open the confirmation modal, so the click never "
                         "reached request_constraint_delete(). This is the "
                         "observation MAR-178 could not make: its tests call that "
                         "helper directly and would pass with the button deleted.\n";
            return false;
        }
        std::vector<WidgetProbe> delete_modal_probes{{"Delete"}, {"Cancel"}};
        if (!sweep_for_widgets(
                delete_modal_probes, "case C6a (delete modal)", kDeleteModal)) {
            return false;
        }
        click_position(delete_modal_probes[0].position);
        if (modal_is_open(kDeleteModal)) {
            std::cerr << "MAR-179 shell case C6a: the modal's Delete button did not "
                         "close the modal.\n";
            return false;
        }
        if (constraint_present(
                *state.load_result.skeleton_data, ConstraintKind::Ik,
                "editor_arm_reach")) {
            std::cerr << "MAR-179 shell case C6a: the mouse-driven delete did not "
                         "remove the constraint from the rebuilt runtime.\n";
            return false;
        }
        if (state.session.undo_count() != delete_undo_before + 1U) {
            std::cerr << "MAR-179 shell case C6a: the mouse-driven delete must be "
                         "exactly one history entry; undo_count "
                      << delete_undo_before << " -> " << state.session.undo_count()
                      << ".\n";
            return false;
        }
        if (!undo_project_change(&state) || serialized(state) != delete_before) {
            std::cerr << "MAR-179 shell case C6a: undo did not restore the exact "
                         "pre-delete serialization.\n";
            return false;
        }
        render_frame();

        // --- C6b: Rename... -> modal -> the modal's Cancel button. ----------
        //
        // Cancel rather than Rename: the modal seeds its InputText with the
        // source name and the primitive refuses a rename to the same name, so a
        // pure-mouse rename would have to type. The rename ITSELF is already
        // covered UI-free by MAR-178; what was missing, and what this asserts,
        // is that the catalog button's click reaches the handler at all.
        select_constraint(
            &state, ConstraintKind::Ik, "editor_arm_reach", "MAR-179 smoke", false);
        cancel_constraint_catalog(&state);
        render_frame();
        render_frame();

        const std::string rename_before = serialized(state);
        const std::size_t rename_undo_before = state.session.undo_count();
        click_widget(ik_probes[kIkRenameButton]);
        if (!modal_is_open(kRenameModal)) {
            std::cerr << "MAR-179 shell case C6b: a real click on \"Rename...\" did "
                         "not open the rename modal, so the click never reached "
                         "request_constraint_rename().\n";
            return false;
        }
        std::vector<WidgetProbe> rename_modal_probes{{"Cancel"}};
        if (!sweep_for_widgets(
                rename_modal_probes, "case C6b (rename modal)", kRenameModal)) {
            return false;
        }
        click_position(rename_modal_probes[0].position);
        if (modal_is_open(kRenameModal)) {
            std::cerr << "MAR-179 shell case C6b: the modal's Cancel button did not "
                         "close the modal.\n";
            return false;
        }
        if (state.session.undo_count() != rename_undo_before ||
            serialized(state) != rename_before) {
            std::cerr << "MAR-179 shell case C6b: a cancelled rename must change "
                         "neither the project nor the history.\n";
            return false;
        }
        render_frame();
    }

    // --- C1 (physics half): the eight absent physics scalars. --------------
    state.constraints_tab = 3;
    select_constraint(
        &state, ConstraintKind::Physics, "editor_ribbon_secondary",
        "MAR-179 smoke", false);
    render_frame();
    render_frame();

    // "Inertia" is both this list's positive control and C5's clamp target.
    std::vector<WidgetProbe> physics_probes{
        {"Step"},            {"X##physics"},       {"Y##physics"},
        {"Rotate##physics"}, {"Scale X##physics"}, {"Shear X##physics"},
        {"Limit"},           {"Mass Inverse"},     {"Inertia"}};
    constexpr std::size_t kPhysicsStep = 0U;
    constexpr std::size_t kPhysicsInertia = 8U;
    if (!sweep_for_widgets(physics_probes, "case C1 (physics)")) {
        return false;
    }

    const auto physics_edit_index = [&]() -> std::size_t {
        const auto* project = state.session.project();
        if (project == nullptr) return static_cast<std::size_t>(-1);
        for (std::size_t index = 0;
             index < project->physics_constraint_edits.size(); ++index) {
            if (project->physics_constraint_edits[index].name ==
                "editor_ribbon_secondary") {
                return index;
            }
        }
        return static_cast<std::size_t>(-1);
    };

    // --- C3: one Step drag is exactly one history entry. -------------------
    {
        const std::string before = serialized(state);
        const std::size_t undo_before = state.session.undo_count();
        const std::size_t index = physics_edit_index();
        if (index == static_cast<std::size_t>(-1)) {
            std::cerr << "MAR-179 shell case C3: the fixture must carry an "
                         "editor_ribbon_secondary physics constraint edit.\n";
            return false;
        }
        const double step_before =
            state.session.project()->physics_constraint_edits[index].step;
        drag_widget(physics_probes[kPhysicsStep], 8.0f, 5);
        const std::size_t index_after = physics_edit_index();
        if (index_after == static_cast<std::size_t>(-1) ||
            state.session.project()->physics_constraint_edits[index_after].step ==
                step_before) {
            std::cerr << "MAR-179 shell case C3: a real mouse drag on Step did not "
                         "change the value (still " << step_before << ").\n";
            return false;
        }
        if (state.session.undo_count() != undo_before + 1U) {
            std::cerr << "MAR-179 shell case C3: a multi-frame Step drag must "
                         "coalesce into EXACTLY one history entry; undo_count "
                      << undo_before << " -> " << state.session.undo_count() << ".\n";
            return false;
        }
        if (!undo_project_change(&state) || serialized(state) != before) {
            std::cerr << "MAR-179 shell case C3: undo did not restore the exact "
                         "pre-drag serialization.\n";
            return false;
        }
        render_frame();
    }

    // --- C5: the clamp bites on a Ctrl+click typed value. ------------------
    //
    // Distinguishable in BOTH directions. Without ImGuiSliderFlags_AlwaysClamp
    // ImGui stores 5.0, the runtime parse rejects it, apply_coalesced_edit_frame
    // rolls the project back, and Inertia stays at the fixture's 0.85 -- neither
    // 5.0 nor the asserted 1.0.
    {
        const WidgetProbe& inertia = physics_probes[kPhysicsInertia];
        const std::size_t index = physics_edit_index();
        if (index == static_cast<std::size_t>(-1)) return false;
        const double inertia_before =
            state.session.project()->physics_constraint_edits[index].inertia;
        // ImGui swaps Cmd and Ctrl at the EVENT layer when
        // io.ConfigMacOSXBehaviors is set (imgui.cpp, AddKeyAnalogEvent), which
        // is the default on Apple. A naive AddKeyEvent(ImGuiMod_Ctrl) therefore
        // raises io.KeySuper, and AddMouseButtonEvent then converts the left
        // press into a RIGHT click -- measured here as mdown1=1, active=0. The
        // widget under test is the clamp, not the platform's modifier mapping,
        // so the harness drives the platform-independent path and restores the
        // flag.
        const bool macos_behaviors_before = io.ConfigMacOSXBehaviors;
        io.ConfigMacOSXBehaviors = false;
        settle();
        restore_scroll(inertia.scroll_y);
        io.AddMousePosEvent(inertia.position.x, inertia.position.y);
        render_frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        render_frame();
        if (ImGui::GetCurrentContext()->TempInputId != inertia.id) {
            std::cerr << "MAR-179 shell case C5: Ctrl+click did not turn Inertia into "
                         "a text input, so the typed-value path was never exercised. "
                         "TempInputId="
                      << ImGui::GetCurrentContext()->TempInputId << " expected "
                      << inertia.id << ".\n";
            io.ConfigMacOSXBehaviors = macos_behaviors_before;
            return false;
        }
        io.AddInputCharacter('5');
        render_frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        render_frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        render_frame();
        render_frame();
        io.ConfigMacOSXBehaviors = macos_behaviors_before;
        const std::size_t index_after = physics_edit_index();
        if (index_after == static_cast<std::size_t>(-1)) return false;
        const double inertia_after =
            state.session.project()->physics_constraint_edits[index_after].inertia;
        if (inertia_after != 1.0) {
            std::cerr << "MAR-179 shell case C5: Ctrl+click-typing 5 into Inertia "
                         "must commit the CLAMPED 1.0, not " << inertia_after
                      << " (fixture value was " << inertia_before
                      << "). Without ImGuiSliderFlags_AlwaysClamp ImGui stores 5.0, "
                         "the runtime parse rejects it, and the frame rolls back to "
                         "the old value -- so this assertion fails either way.\n";
            return false;
        }
        while (state.session.undo_count() > 0U) {
            if (!undo_project_change(&state)) break;
        }
        render_frame();
    }

    std::cout << "MAR-179 constraint parameter shell smoke: a real mouse found all "
              << (ik_probes.size() + physics_probes.size())
              << " widgets by HoveredId (three of them positive controls that "
                 "shipped before MAR-179, two of them MAR-178's catalog buttons), "
                 "two successive Softness drags and a Step drag each coalesced into "
                 "exactly one history entry without merging into the previous one, "
                 "the Compress and Stretch checkboxes reached edit_ik_constraint and "
                 "survived undo/redo, Ctrl+click-typing 5 into Inertia committed the "
                 "clamped 1.0, and a real click on Delete.../Rename... opened their "
                 "modals whose own Delete and Cancel buttons were then clicked too -- "
                 "the round trip MAR-178's UI-free tests could not make.\n";
    return true;
}

} // namespace marrow::editor::shell
