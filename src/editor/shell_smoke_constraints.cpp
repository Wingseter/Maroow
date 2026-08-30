// MAR-178 -- headless coverage of the constraint rename and delete surfaces.
//
// Every case drives the UI-free helpers the modals call, never ImGui, so the
// confirmation flow, the selection cascade and the undo/redo reconcile are
// asserted without a frame being rendered.

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

} // namespace marrow::editor::shell
