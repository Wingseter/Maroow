/**
 * @file
 * @brief MAR-190 shell cases: W1, N1-N5, F1, F2.
 *
 * A NEW file rather than an addition to `shell_smoke_project.cpp`, which is over
 * six thousand lines and shared with in-flight stories. MAR-187 set that
 * precedent and it is the reason this story's landing touches five shared files
 * instead of six.
 *
 * The fixture is built with the REAL importer over the tracked sample PSD copied
 * into a temp directory. MAR-188's synthesiser lives inside the anonymous
 * namespace of `src/samples/psd_import_smoke.cpp`, which is its own executable,
 * so `marrow_editor_shell` cannot link a line of it -- and extracting another
 * story's file to share it is not this story's business.
 *
 * EVERYTHING happens under `temp_directory_path()`. `marrow.editor_shell_smoke`
 * runs with `WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}`, so a committing case
 * pointed at a project inside the source tree would overwrite tracked assets.
 * W1 asserts the tracked fixtures are byte-identical at the end of the run.
 */

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "imgui.h"
#include "imgui_internal.h"

#include "marrow/editor/project.hpp"
#include "marrow/editor/psd_import.hpp"
#include "marrow/editor/psd_reimport_commit.hpp"
#include "marrow/editor/psd_reimport_plan.hpp"
#include "marrow/editor/psd_reimport_review.hpp"
#include "marrow/editor/selection.hpp"
#include "psd_reimport_commit_internal.hpp"
#include "shell_psd_reimport.hpp"
#include "shell_project_panels.hpp"
#include "shell_smoke_scenarios.hpp"
#include "shell_state.hpp"

namespace marrow::editor::shell {
namespace {

/** @brief Per-process, for the reason MAR-189 measured at 615 leaked roots. */
std::filesystem::path psd_scratch_root() {
#ifdef _WIN32
    const long long pid = static_cast<long long>(_getpid());
#else
    const long long pid = static_cast<long long>(::getpid());
#endif
    return std::filesystem::temp_directory_path() /
        ("mar190_shell-" + std::to_string(pid));
}

using ByteMap = std::map<std::string, std::string>;

std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(
        (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

/** @brief A recursive listing with contents, so a file nobody predicted is seen. */
void collect_bytes(const std::filesystem::path& root, ByteMap* out) {
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            break;
        }
        std::error_code file_error;
        if (iterator->is_regular_file(file_error)) {
            (*out)[iterator->path().generic_string()] = read_all(iterator->path());
        }
    }
}

ByteMap bundle_bytes(const marrow::editor::ProjectData& project) {
    ByteMap map;
    collect_bytes(project.source_path.parent_path(), &map);
    if (project.editor_metadata.import_sources.has_value() &&
        project.editor_metadata.import_sources->psd.has_value()) {
        collect_bytes(
            project.resolve_path(project.editor_metadata.import_sources->psd->layers_directory),
            &map);
    }
    return map;
}

/** @brief The selection as an ORDERED identity list. A count is not the fact. */
std::vector<std::string> selection_rows(const marrow::editor::SelectionSet& selection) {
    std::vector<std::string> rows;
    for (const marrow::editor::SelectionItem& item : selection.items()) {
        rows.push_back(std::visit(
            [](const auto& value) -> std::string {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, marrow::editor::BoneSelection>) {
                    return "bone:" + value.bone_name;
                } else if constexpr (std::is_same_v<T, marrow::editor::SlotSelection>) {
                    return "slot:" + value.slot_name;
                } else if constexpr (std::is_same_v<
                                         T, marrow::editor::AttachmentSelection>) {
                    return "attachment:" + value.skin_name + "/" + value.slot_name + "/" +
                        value.attachment_name;
                } else {
                    return "constraint:" + value.constraint_name;
                }
            },
            item));
    }
    return rows;
}

/** @brief AC3's whole invariant, shell side -- six clauses, five callers. */
struct NoOpWitness {
    std::string serialized;
    std::uint64_t project_revision{0};
    std::uint64_t runtime_revision{0};
    std::uint64_t preview_revision{0};
    std::size_t undo_count{0};
    std::size_t redo_count{0};
    ByteMap bundle;
    std::filesystem::path skeleton_path;
    std::vector<std::filesystem::path> atlas_paths;
    std::vector<std::string> selection;
};

NoOpWitness capture_no_op(const ShellState& state) {
    NoOpWitness witness;
    const marrow::editor::ProjectData& project = *state.session.project();
    witness.serialized = marrow::editor::serialize_project(project);
    witness.project_revision = state.session.project_revision();
    witness.runtime_revision = state.session.runtime_revision();
    witness.preview_revision = state.session.preview_revision();
    witness.undo_count = state.session.undo_count();
    witness.redo_count = state.session.redo_count();
    witness.bundle = bundle_bytes(project);
    witness.skeleton_path = project.resolved_skeleton_path();
    witness.atlas_paths = project.resolved_atlas_paths();
    witness.selection = selection_rows(state.selection);
    return witness;
}

bool expect_reimport_no_op(
    const ShellState& state, const NoOpWitness& before, std::string_view label) {
    const marrow::editor::ProjectData& project = *state.session.project();
    bool ok = true;

    // (a) The authored project, by full byte identity.
    const std::string after = marrow::editor::serialize_project(project);
    if (after != before.serialized) {
        std::size_t offset = 0;
        const std::size_t shared = std::min(after.size(), before.serialized.size());
        while (offset < shared && after[offset] == before.serialized[offset]) {
            ++offset;
        }
        std::cerr << label << ": the project's serialization changed at offset " << offset
                  << ".\n";
        ok = false;
    }
    // (b) Revisions.
    if (state.session.project_revision() != before.project_revision ||
        state.session.runtime_revision() != before.runtime_revision ||
        state.session.preview_revision() != before.preview_revision) {
        std::cerr << label << ": a revision moved.\n";
        ok = false;
    }
    // (c) BOTH depths.
    if (state.session.undo_count() != before.undo_count ||
        state.session.redo_count() != before.redo_count) {
        std::cerr << label << ": history depth moved (undo " << before.undo_count << "->"
                  << state.session.undo_count() << ", redo " << before.redo_count << "->"
                  << state.session.redo_count() << ").\n";
        ok = false;
    }
    // (d) Every byte, both directions.
    const ByteMap after_bundle = bundle_bytes(project);
    for (const auto& entry : before.bundle) {
        const auto found = after_bundle.find(entry.first);
        if (found == after_bundle.end()) {
            std::cerr << label << ": only in before: " << entry.first << '\n';
            ok = false;
            continue;
        }
        if (found->second != entry.second) {
            std::size_t offset = 0;
            const std::size_t shared = std::min(entry.second.size(), found->second.size());
            while (offset < shared && entry.second[offset] == found->second[offset]) {
                ++offset;
            }
            std::cerr << label << ": " << entry.first << " differs at offset " << offset
                      << ".\n";
            ok = false;
        }
    }
    for (const auto& entry : after_bundle) {
        if (before.bundle.find(entry.first) == before.bundle.end()) {
            std::cerr << label << ": only in after: " << entry.first << '\n';
            ok = false;
        }
    }
    // (e) The runtime SOURCE is a path, not only bytes.
    if (project.resolved_skeleton_path() != before.skeleton_path ||
        project.resolved_atlas_paths() != before.atlas_paths) {
        std::cerr << label << ": a runtime source path changed.\n";
        ok = false;
    }
    // (f) Shell only: the selection, as an ordered identity list.
    const std::vector<std::string> selection_after = selection_rows(state.selection);
    if (selection_after != before.selection) {
        std::cerr << label << ": the selection changed (" << before.selection.size()
                  << " -> " << selection_after.size() << " members).\n";
        ok = false;
    }
    return ok;
}

/**
 * @brief Every tracked path this story could plausibly touch.
 *
 * `player_fixture.png`, NOT `player_idle.png` -- the latter does not exist, and
 * the plan's original list named it. A hash sweep over a path that is not there
 * is a check with nothing to check that reports success, so `tracked_bytes`
 * asserts existence BEFORE hashing and fails by absolute path. `player_idle.mbin`
 * was also missing from that list.
 */
std::vector<std::filesystem::path> tracked_fixture_paths() {
    const std::filesystem::path base = std::filesystem::absolute("assets/fixtures");
    return {
        base / "psd_import_sample.psd",
        base / "psd_import_sample_reimport.psd",
        base / "player_idle.marrow",
        base / "player_idle.mskl",
        base / "player_idle.matl",
        base / "player_idle.mbin",
        base / "player_fixture.png",
    };
}

bool tracked_bytes(ByteMap* out, std::string_view label) {
    for (const std::filesystem::path& path : tracked_fixture_paths()) {
        if (!std::filesystem::exists(path)) {
            std::cerr << label << ": tracked fixture " << path.generic_string()
                      << " does not exist. A byte map over a missing path checks "
                         "nothing and would report success.\n";
            return false;
        }
        (*out)[path.generic_string()] = read_all(path);
    }
    return true;
}

/** @brief The whole shell fixture: a real project over a real imported PSD. */
struct Fixture {
    std::filesystem::path root;
    std::filesystem::path psd;
    std::filesystem::path project_path;
    std::filesystem::path staging_root;
    marrow::editor::PsdReimportPlan plan;
};

bool build_fixture(const std::filesystem::path& root, Fixture* out, const char* label) {
    std::error_code error;
    std::filesystem::remove_all(root, error);
    // The bundle lives in its OWN directory, with staging and the source PSD as
    // SIBLINGS rather than children. `bundle_bytes` is a recursive listing of the
    // project's directory -- deliberately, so a file the commit created under a
    // name nobody predicted is still seen -- so anything parked beside the bundle
    // is inside the invariant. Staging under the project directory made N1 fail
    // on correct code: cancelling removed the staging tree, and the byte map
    // reported it as "only in before". Staging does not belong inside a project.
    const std::filesystem::path bundle_dir = root / "project";
    std::filesystem::create_directories(bundle_dir / "layers", error);
    std::filesystem::create_directories(root / "psd", error);
    out->root = root;

    const std::filesystem::path tracked =
        std::filesystem::absolute("assets/fixtures/psd_import_sample.psd");
    out->psd = root / "psd" / "source.psd";
    std::filesystem::copy_file(
        tracked, out->psd, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        std::cerr << label << ": could not copy the tracked PSD: " << error.message()
                  << '\n';
        return false;
    }

    marrow::editor::PsdImportOptions import_options;
    import_options.psd_path = out->psd;
    import_options.skeleton_output_path = bundle_dir / "bundle.mskl";
    import_options.atlas_output_path = bundle_dir / "bundle.matl";
    import_options.extracted_layers_directory = bundle_dir / "layers";
    import_options.atlas_name = "bundle";
    const marrow::editor::PsdImportResult imported =
        marrow::editor::import_psd_to_runtime_bundle(import_options);
    if (!imported) {
        std::cerr << label << ": the fixture PSD did not import: "
                  << imported.error->format() << '\n';
        return false;
    }

    out->project_path = bundle_dir / "bundle.marrow";
    marrow::editor::MinimalProjectOptions project_options;
    project_options.project_path = out->project_path;
    project_options.skeleton_path = import_options.skeleton_output_path;
    project_options.atlas_paths = {import_options.atlas_output_path};
    project_options.name = "mar190_shell";
    project_options.preview_skins = {};
    marrow::editor::ProjectData project =
        marrow::editor::create_minimal_project(project_options);
    marrow::editor::ProjectImportSources sources;
    sources.psd =
        marrow::editor::make_psd_provenance(imported, out->project_path, out->psd);
    // One remembered layer the PSD does NOT contain. Reimporting a file over
    // itself yields only `Updated` rows, so without this there is no `Missing`
    // section, no checkbox, and F2 cannot fail. This is exactly the situation the
    // checkbox exists for: an artist deleted a layer and the project still
    // remembers where it used to map.
    marrow::editor::PsdLayerProvenance retired;
    retired.layer_name = "retired_layer";
    retired.slot_name = "retired_slot";
    retired.attachment_name = "retired_slot";
    retired.bone_name = "root";
    retired.image_file = "retired_layer.png";
    sources.psd->layers.push_back(std::move(retired));
    project.editor_metadata.import_sources = std::move(sources);
    const marrow::editor::ProjectSaveResult saved =
        marrow::editor::save_project(project, out->project_path);
    if (!saved) {
        std::cerr << label << ": the fixture project did not save: "
                  << saved.error->format() << '\n';
        return false;
    }
    return true;
}

/** @brief Plans a reimport of the fixture's own PSD, under the target's names. */
bool plan_fixture(ShellState* state, Fixture* fixture, const char* label) {
    fixture->staging_root = fixture->root / "staging";
    marrow::editor::PsdReimportPlanOptions options;
    options.psd_path = fixture->psd;
    options.staging_root = fixture->staging_root;
    options.staged_skeleton_filename =
        state->session.project()->resolved_skeleton_path().filename().generic_string();
    options.staged_atlas_filename =
        state->session.project()->resolved_atlas_paths().front().filename().generic_string();
    fixture->plan = marrow::editor::plan_psd_reimport(*state->session.project(), options);
    if (!fixture->plan) {
        std::cerr << label << ": planning the fixture reimport failed: "
                  << fixture->plan.error->format() << '\n';
        return false;
    }
    return true;
}

} // namespace

bool validate_mar190_psd_reimport_shell_smoke() {
    // Its OWN ShellState, following MAR-187. The scenario opens its own fixture
    // project, and doing that on the SHARED state leaves every later scenario
    // pointed at this story's temp bundle instead of `player_idle.marrow` --
    // measured: `SelectionSet shell smoke requires a constraint fixture`, a
    // failure in somebody else's case caused entirely by this one.
    ShellState state;
    const std::filesystem::path root = psd_scratch_root();

    // W1's baseline, captured BEFORE anything runs. The scenario works entirely
    // under the temp directory, but `marrow.editor_shell_smoke` runs with
    // WORKING_DIRECTORY set to the source tree, so a single mistaken path would
    // write into tracked assets -- and `marrow_project_smoke` SKIPS its whole
    // editing suite, exiting 0, when `player_idle` stops matching.
    ByteMap tracked_before;
    if (!tracked_bytes(&tracked_before, "MAR-190 W1")) {
        return false;
    }

    // ---- N1 -- Cancel changes nothing, and DISENGAGES the review. -----------
    {
        Fixture fixture;
        if (!build_fixture(root / "n1", &fixture, "MAR-190 N1")) {
            return false;
        }
        const marrow::editor::ProjectLoadResult loaded =
            state.session.open(fixture.project_path);
        if (!loaded) {
            std::cerr << "MAR-190 N1: the fixture project did not open: "
                      << loaded.error->format() << '\n';
            return false;
        }
        state.session.clear_history();
        if (!plan_fixture(&state, &fixture, "MAR-190 N1")) {
            return false;
        }

        begin_psd_reimport_review(&state, fixture.plan, fixture.staging_root, fixture.psd);
        if (!state.psd_reimport.review.has_value() || !state.psd_reimport.open) {
            std::cerr << "MAR-190 N1: the review did not engage.\n";
            return false;
        }
        // A choice is made, so cancelling has something to discard. Cancelling a
        // review nobody touched would pass whether or not the choice is dropped.
        const marrow::editor::PsdReviewSections sections =
            marrow::editor::group_psd_review(fixture.plan);
        if (!sections.missing.empty()) {
            marrow::editor::set_psd_review_deletion(
                &*state.psd_reimport.review,
                fixture.plan.layers[sections.missing.front()].identity, true);
        }

        const NoOpWitness before = capture_no_op(state);
        close_psd_reimport_review(&state, marrow::editor::PsdReviewOutcome::Cancelled);

        if (state.psd_reimport.review.has_value()) {
            std::cerr << "MAR-190 N1: the review is still engaged after Cancel.\n";
            return false;
        }
        if (state.psd_reimport.open) {
            std::cerr << "MAR-190 N1: the modal is still marked open after Cancel.\n";
            return false;
        }
        if (!expect_reimport_no_op(state, before, "MAR-190 N1")) {
            return false;
        }
        if (std::filesystem::exists(fixture.staging_root)) {
            std::cerr << "MAR-190 N1: the staging root "
                      << std::filesystem::absolute(fixture.staging_root).generic_string()
                      << " survived Cancel.\n";
            return false;
        }
    }

    // ---- N2 -- Modal close, through the `p_open` path. ----------------------
    //
    // A SEPARATE case, not a second clause inside N1, because it is separate
    // code: Cancel calls `CloseCurrentPopup` and this route runs when
    // `BeginPopupModal` sees its `bool*` go false. Replacing the `bool*` with
    // `nullptr` leaves N1 green and only this case red.
    {
        Fixture fixture;
        if (!build_fixture(root / "n2", &fixture, "MAR-190 N2")) {
            return false;
        }
        const marrow::editor::ProjectLoadResult loaded =
            state.session.open(fixture.project_path);
        if (!loaded) {
            std::cerr << "MAR-190 N2: the fixture project did not open.\n";
            return false;
        }
        state.session.clear_history();
        if (!plan_fixture(&state, &fixture, "MAR-190 N2")) {
            return false;
        }

        begin_psd_reimport_review(&state, fixture.plan, fixture.staging_root, fixture.psd);
        const NoOpWitness before = capture_no_op(state);

        // REAL FRAMES, not a direct call to the seam. N1 already covers the
        // UI-free entry point; if N2 also just called `close_psd_reimport_review`
        // it would be a second spelling of N1 and the `p_open` argument -- the
        // whole reason this case exists -- would have no detector at all.
        ImGuiIO& io = ImGui::GetIO();
        const auto render_frame = [&]() {
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            draw_psd_reimport_modal(&state);
            ImGui::Render();
        };

        render_frame();
        ImGuiWindow* modal = ImGui::FindWindowByName(kPsdReimportModal);
        if (modal == nullptr || !modal->Active) {
            std::cerr << "MAR-190 N2: the modal \"" << kPsdReimportModal
                      << "\" is " << (modal == nullptr ? "absent" : "not Active")
                      << " after its opening frame.\n";
            return false;
        }

        // Exactly what the title-bar close control does: clear `p_open`. With a
        // `nullptr` there instead, this has no effect and the modal stays up.
        state.psd_reimport.open = false;
        // TWO frames. `BeginPopupModal` calls `Begin` BEFORE it tests `p_open`
        // (`imgui.cpp:13104` then `:13105`), so the modal window is submitted --
        // and therefore still `Active` -- during the very frame that closes it.
        // Only on the next frame, when the review is gone and the draw returns
        // early, does `EndFrame` clear it. Checking after one frame reports "still
        // Active" on correct code; measured here before it was fixed.
        render_frame();
        render_frame();

        modal = ImGui::FindWindowByName(kPsdReimportModal);
        if (modal != nullptr && modal->Active) {
            std::cerr << "MAR-190 N2: the modal is still Active after the close control "
                         "was driven; BeginPopupModal was given no bool*.\n";
            return false;
        }
        if (state.psd_reimport.review.has_value()) {
            std::cerr << "MAR-190 N2: the review is still engaged after modal close.\n";
            return false;
        }
        if (!expect_reimport_no_op(state, before, "MAR-190 N2")) {
            return false;
        }
        if (std::filesystem::exists(fixture.staging_root)) {
            std::cerr << "MAR-190 N2: the staging root survived modal close.\n";
            return false;
        }
    }

    // ---- N3 -- STALE, project side. Provenance changed under the review. ----
    //
    // The two checked-in PSD fixtures differ in 4 bytes and have IDENTICAL layer
    // sets, so swapping one for the other does not move the digest. Editing the
    // PROJECT is both a realistic race and the stronger claim, because it proves
    // the digest covers the project side and not only the PSD.
    {
        Fixture fixture;
        if (!build_fixture(root / "n3", &fixture, "MAR-190 N3")) {
            return false;
        }
        if (!state.session.open(fixture.project_path)) {
            std::cerr << "MAR-190 N3: the fixture project did not open.\n";
            return false;
        }
        state.session.clear_history();
        if (!plan_fixture(&state, &fixture, "MAR-190 N3")) {
            return false;
        }
        begin_psd_reimport_review(&state, fixture.plan, fixture.staging_root, fixture.psd);

        // A REAL transaction that drops one remembered layer. It must drop a
        // whole layer rather than edit a mapping: the digest is the ordered
        // (identity, change) row list, so a renamed slot would not move it.
        {
            auto edit = state.session.begin_edit(
                {marrow::editor::EditKind::EditProperty,
                 "mar190 drop a remembered layer",
                 {},
                 false,
                 marrow::editor::EditImpact::Project});
            if (!edit) {
                std::cerr << "MAR-190 N3: could not begin the provenance edit.\n";
                return false;
            }
            auto& layers = edit.project()->editor_metadata.import_sources->psd->layers;
            if (layers.empty()) {
                std::cerr << "MAR-190 N3: the fixture remembers no layers to drop.\n";
                return false;
            }
            layers.pop_back();
            const marrow::editor::SessionResult sealed = edit.commit();
            if (!sealed) {
                std::cerr << "MAR-190 N3: could not commit the provenance edit: "
                          << sealed.error->format() << '\n';
                return false;
            }
        }

        // The baseline is captured AFTER the deliberate edit. Pinning it before
        // would make this case fail on correct code -- the edit is the premise,
        // not the thing under test.
        const NoOpWitness before = capture_no_op(state);

        marrow::editor::PsdReimportReviewOptions options;
        options.project_path = fixture.project_path;
        options.restage_root = root / "n3_restage";
        const marrow::editor::PsdReviewApplyResult applied =
            marrow::editor::apply_psd_reimport_review(
                state.session, *state.psd_reimport.review, options);

        if (applied.outcome != marrow::editor::PsdReviewOutcome::Stale) {
            std::cerr << "MAR-190 N3: expected Stale, got "
                      << marrow::editor::psd_review_outcome_text(applied.outcome)
                      << " (error '" << applied.error << "').\n";
            return false;
        }
        if (!expect_reimport_no_op(state, before, "MAR-190 N3")) {
            return false;
        }
        close_psd_reimport_review(&state, applied.outcome);
    }

    // ---- N4 -- PLANNING FAILURE. The PSD is gone. ---------------------------
    {
        Fixture fixture;
        if (!build_fixture(root / "n4", &fixture, "MAR-190 N4")) {
            return false;
        }
        if (!state.session.open(fixture.project_path)) {
            std::cerr << "MAR-190 N4: the fixture project did not open.\n";
            return false;
        }
        state.session.clear_history();
        if (!plan_fixture(&state, &fixture, "MAR-190 N4")) {
            return false;
        }
        begin_psd_reimport_review(&state, fixture.plan, fixture.staging_root, fixture.psd);

        std::error_code remove_error;
        std::filesystem::remove(fixture.psd, remove_error);

        const NoOpWitness before = capture_no_op(state);
        marrow::editor::PsdReimportReviewOptions options;
        options.project_path = fixture.project_path;
        options.restage_root = root / "n4_restage";
        const marrow::editor::PsdReviewApplyResult applied =
            marrow::editor::apply_psd_reimport_review(
                state.session, *state.psd_reimport.review, options);

        if (applied.outcome != marrow::editor::PsdReviewOutcome::PlanFailed) {
            std::cerr << "MAR-190 N4: expected PlanFailed, got "
                      << marrow::editor::psd_review_outcome_text(applied.outcome) << ".\n";
            return false;
        }
        if (!expect_reimport_no_op(state, before, "MAR-190 N4")) {
            return false;
        }
        // Staging is NOT a target, so no byte-map clause anywhere can see a
        // leaked restage tree. This is its only detector on the shell side.
        if (std::filesystem::exists(options.restage_root)) {
            std::cerr << "MAR-190 N4: the restage root "
                      << std::filesystem::absolute(options.restage_root).generic_string()
                      << " still exists after a planning failure.\n";
            return false;
        }
        close_psd_reimport_review(&state, applied.outcome);
    }

    // ---- N5 -- COMMIT FAILURE, one representative step. ---------------------
    // V8 owns the exhaustive per-step sweep; this proves the SHELL path reports
    // it and leaves everything alone.
    {
        Fixture fixture;
        if (!build_fixture(root / "n5", &fixture, "MAR-190 N5")) {
            return false;
        }
        if (!state.session.open(fixture.project_path)) {
            std::cerr << "MAR-190 N5: the fixture project did not open.\n";
            return false;
        }
        state.session.clear_history();
        if (!plan_fixture(&state, &fixture, "MAR-190 N5")) {
            return false;
        }
        begin_psd_reimport_review(&state, fixture.plan, fixture.staging_root, fixture.psd);

        const NoOpWitness before = capture_no_op(state);
        marrow::editor::PsdReimportReviewOptions options;
        options.project_path = fixture.project_path;
        options.restage_root = root / "n5_restage";

        marrow::editor::detail::set_psd_commit_failpoint_for_testing(
            [](marrow::editor::PsdCommitStep reached) -> std::string {
                return reached == marrow::editor::PsdCommitStep::PlaceSkeleton
                    ? "MAR-190 N5 injected failure"
                    : std::string();
            });
        const marrow::editor::PsdReviewApplyResult applied =
            marrow::editor::apply_psd_reimport_review(
                state.session, *state.psd_reimport.review, options);
        marrow::editor::detail::set_psd_commit_failpoint_for_testing({});

        if (applied.outcome != marrow::editor::PsdReviewOutcome::CommitFailed) {
            std::cerr << "MAR-190 N5: expected CommitFailed, got "
                      << marrow::editor::psd_review_outcome_text(applied.outcome) << ".\n";
            return false;
        }
        if (applied.error.find("PlaceSkeleton") == std::string::npos) {
            std::cerr << "MAR-190 N5: the error must name the failing step; got '"
                      << applied.error << "'.\n";
            return false;
        }
        if (!expect_reimport_no_op(state, before, "MAR-190 N5")) {
            return false;
        }
        close_psd_reimport_review(&state, applied.outcome);
    }

    // ---- F1/F2 -- the rows and the checkbox, by a REAL MOUSE. --------------
    //
    // Every UI-free case above calls the seams directly and would stay green with
    // the modal drawing nothing at all. This is the only thing in the story that
    // can see a widget.
    {
        Fixture fixture;
        if (!build_fixture(root / "f1", &fixture, "MAR-190 F1")) {
            return false;
        }
        if (!state.session.open(fixture.project_path)) {
            std::cerr << "MAR-190 F1: the fixture project did not open.\n";
            return false;
        }
        state.session.clear_history();
        state.project_path = fixture.project_path;
        // This block opens the session DIRECTLY rather than through the shell's
        // `open_project`, so `adopt_session_project_into_shell` -- the only path
        // that points `preview_skeleton`/`animation_state` at the session -- never
        // runs, and both aliases stay null. The real shell has them populated from
        // the moment a project is adopted and re-synced every frame
        // (shell_main.cpp:538). Without this line F3 below compares nullptr against
        // a pointer, which fails for the right reason by accident: it would catch a
        // MISSING sync but not a STALE one, and its own message would be a lie.
        sync_shell_from_editor_session(&state);

        ImGuiIO& io = ImGui::GetIO();
        const auto render_frame = [&]() {
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            draw_project_window(&state);
            ImGui::Render();
        };
        render_frame();

        /** @brief Sweeps a real mouse over a window and returns where an id was hovered. */
        const auto sweep_for = [&](ImGuiWindow* window, ImGuiID target, float step_x,
                                   float inset_left, ImVec2* out) -> bool {
            if (window == nullptr) {
                return false;
            }
            const ImRect bounds = window->Rect();
            for (float y = bounds.Min.y + 2.0f; y <= bounds.Max.y - 2.0f; y += 3.0f) {
                for (float x = bounds.Min.x + inset_left; x <= bounds.Max.x - 2.0f;
                     x += step_x) {
                    io.AddMousePosEvent(x, y);
                    render_frame();
                    const ImGuiContext* context = ImGui::GetCurrentContext();
                    if (context != nullptr && context->HoveredId == target) {
                        *out = ImVec2(x, y);
                        return true;
                    }
                }
            }
            return false;
        };
        const auto click_at = [&](ImVec2 position) {
            io.AddMousePosEvent(position.x, position.y);
            render_frame();
            io.AddMouseButtonEvent(0, true);
            render_frame();
            io.AddMouseButtonEvent(0, false);
            render_frame();
        };

        ImGuiWindow* project = ImGui::FindWindowByName(kProjectWindowTitle);
        if (project == nullptr) {
            std::cerr << "MAR-190 F1: the Project window was never submitted.\n";
            return false;
        }

        // CONTROL SWEEP FIRST, against a widget that shipped long before this
        // story. If the id seed is broken, this fails too -- and a broken seed
        // reports every widget "absent", which is indistinguishable from a
        // missing button unless something known-present is swept as well.
        // The Project window is auto-sized too, and its rect one frame after
        // submission is a STUB -- measured at (60,60)-(92,97), 32x37. A sweep over
        // that scans a sliver and reports every widget absent, including the
        // control. Settle it before capturing bounds, exactly as the modal is
        // settled below; this case originally settled only the modal and the
        // control sweep failed for that reason alone.
        for (int settle = 0; settle < 3; ++settle) {
            render_frame();
        }
        project = ImGui::FindWindowByName(kProjectWindowTitle);
        if (project == nullptr) {
            std::cerr << "MAR-190 F1: lost the Project window while it settled.\n";
            return false;
        }

        ImVec2 control_at{};
        if (!sweep_for(project, project->GetID("Export .mbin"), 8.0f, 4.0f, &control_at)) {
            std::cerr << "MAR-190 F1: the CONTROL sweep failed -- \"Export .mbin\" "
                         "predates this story and must be hoverable. The id seed is "
                         "broken, so nothing else this case reports can be trusted.\n";
            return false;
        }
        std::cout << "  MAR-190 F1: control \"Export .mbin\" at (" << control_at.x << ", "
                  << control_at.y << ")\n";

        ImVec2 button_at{};
        if (!sweep_for(project, project->GetID("Reimport PSD...##psd_reimport_open"), 8.0f,
                       4.0f, &button_at)) {
            std::cerr << "MAR-190 F1: \"Reimport PSD...\" was never hovered, though the "
                         "control sweep succeeded -- so the seed is sound and the button "
                         "is genuinely absent or unreachable.\n";
            return false;
        }
        std::cout << "  MAR-190 F1: button at (" << button_at.x << ", " << button_at.y
                  << ")\n";
        click_at(button_at);

        ImGuiWindow* modal = ImGui::FindWindowByName(kPsdReimportModal);
        if (modal == nullptr || !modal->Active) {
            std::cerr << "MAR-190 F1: clicking the button did not open "
                      << kPsdReimportModal << ".\n";
            return false;
        }
        // THREE settle frames before bounds are captured. A modal is submitted at
        // a stub size on its opening frame; a sweep over that scans a sliver and
        // finds nothing.
        for (int settle = 0; settle < 3; ++settle) {
            render_frame();
        }
        modal = ImGui::FindWindowByName(kPsdReimportModal);
        if (modal == nullptr) {
            std::cerr << "MAR-190 F1: lost the modal while it settled.\n";
            return false;
        }
        std::cout << "  MAR-190 F1: modal rect (" << modal->Pos.x << ", " << modal->Pos.y
                  << ")-(" << (modal->Pos.x + modal->Size.x) << ", "
                  << (modal->Pos.y + modal->Size.y) << ")\n";

        // A10, asserted rather than trusted: ESCAPE MUST NOT CLOSE A MODAL.
        // `NavUpdateCancelRequest` reaches its popup-closing arm only for a
        // NON-modal popup (`imgui.cpp:14873`). A measured negative is worth one
        // clause, because the whole `p_open` design rests on it.
        io.AddKeyEvent(ImGuiKey_Escape, true);
        render_frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        render_frame();
        modal = ImGui::FindWindowByName(kPsdReimportModal);
        if (modal == nullptr || !modal->Active) {
            std::cerr << "MAR-190 F1: Escape CLOSED the modal. AC3's \"modal close\" is "
                         "built on Escape being unable to, so this invalidates the "
                         "p_open design rather than merely failing a case.\n";
            return false;
        }

        // One row per section, located by the modal's own id seed, over the plan
        // the MODAL IS SHOWING -- not one this case computed for itself. F1 first
        // grouped `fixture.plan`, which the production button path never fills,
        // so it grouped an empty plan and reported 0 Updated / 0 Missing while the
        // modal on screen was drawing rows.
        if (!state.psd_reimport.review.has_value()) {
            std::cerr << "MAR-190 F1: no review is engaged after the modal opened.\n";
            return false;
        }
        const marrow::editor::PsdReimportPlan& shown = state.psd_reimport.review->plan;
        const marrow::editor::PsdReviewSections sections =
            marrow::editor::group_psd_review(shown);
        if (sections.missing.empty() || sections.updated.empty()) {
            std::cerr << "MAR-190 F1: the fixture needs at least one Updated and one "
                         "Missing row; it has "
                      << sections.updated.size() << " and " << sections.missing.size()
                      << ". Without them F2 cannot fail.\n";
            return false;
        }
        const std::string missing_identity =
            shown.layers[sections.missing.front()].identity;
        for (const auto& entry :
             {std::make_pair("updated", sections.updated.front()),
              std::make_pair("missing", sections.missing.front())}) {
            const std::string label =
                shown.layers[entry.second].identity + "##psd_row_" +
                shown.layers[entry.second].identity;
            ImVec2 row_at{};
            if (!sweep_for(modal, modal->GetID(label.c_str()), 10.0f, 4.0f, &row_at)) {
                std::cerr << "MAR-190 F1: the " << entry.first << " row \"" << label
                          << "\" was never hovered.\n";
                return false;
            }
            std::cout << "  MAR-190 F1: " << entry.first << " row at (" << row_at.x << ", "
                      << row_at.y << ")\n";
        }

        // ---- F2 -- the checkbox is REACHABLE, not merely present. ----------
        //
        // Swept in its OWN column: a column at the row's left edge reaches the
        // Selectable and misses everything after SameLine(). And "found
        // somewhere" is not the claim -- a default-width Selectable pushes the
        // checkbox past the window's right edge, where a sweep wide enough still
        // finds it and a USER cannot reach it.
        const std::string box_label = "Forget##psd_forget_" + missing_identity;
        ImVec2 box_at{};
        if (!sweep_for(modal, modal->GetID(box_label.c_str()), 4.0f,
                       modal->Size.x * 0.5f, &box_at)) {
            std::cerr << "MAR-190 F2: the delete checkbox \"" << box_label
                      << "\" was never hovered.\n";
            return false;
        }
        const float usable_right =
            modal->Pos.x + modal->Size.x - marrow::editor::shell::kPsdReviewControlColumn;
        std::cout << "  MAR-190 F2: checkbox at (" << box_at.x << ", " << box_at.y
                  << "), usable right edge " << usable_right << "\n";
        // The checkbox must sit INSIDE the modal and WITHIN the reserved control
        // column -- i.e. right of the label boundary and left of the window edge.
        // Both halves matter: a default-width `Selectable` spans the whole content
        // region and pushes the checkbox past the window edge, where a sweep wide
        // enough still finds it and a user cannot reach it. "Found somewhere" is
        // not the claim; "reachable" is.
        const float modal_right = modal->Pos.x + modal->Size.x;
        if (box_at.x >= modal_right || box_at.x < usable_right) {
            std::cerr << "MAR-190 F2: the checkbox for '" << missing_identity
                      << "' was located at x=" << box_at.x
                      << ", outside the reserved control column [" << usable_right
                      << ", " << modal_right
                      << ") -- a default Selectable spans the whole content region and "
                         "pushes a SameLine() control off the window.\n";
            return false;
        }

        const std::size_t undo_before = state.session.undo_count();
        click_at(box_at);
        const std::vector<std::string> chosen =
            marrow::editor::chosen_psd_deletions(*state.psd_reimport.review);
        if (chosen != std::vector<std::string>{missing_identity}) {
            std::cerr << "MAR-190 F2: after a real click the forget set is {";
            for (const std::string& identity : chosen) {
                std::cerr << ' ' << identity;
            }
            std::cerr << " }, expected exactly { " << missing_identity << " }.\n";
            return false;
        }

        // Two presses at the same pixel are a DOUBLE CLICK. Advance the clock
        // past the threshold before the next gesture, or the second is swallowed.
        io.DeltaTime = static_cast<float>(io.MouseDoubleClickTime) + 0.1f;
        render_frame();

        ImVec2 confirm_at{};
        if (!sweep_for(modal, modal->GetID("Confirm##psd_confirm"), 6.0f, 4.0f,
                       &confirm_at)) {
            std::cerr << "MAR-190 F2: the Confirm button was never hovered.\n";
            return false;
        }
        std::cout << "  MAR-190 F2: confirm at (" << confirm_at.x << ", " << confirm_at.y
                  << ")\n";
        click_at(confirm_at);

        if (state.psd_reimport.review.has_value()) {
            std::cerr << "MAR-190 F2: the review is still engaged after Confirm.\n";
            return false;
        }
        if (state.session.undo_count() != undo_before + 1U) {
            std::cerr << "MAR-190 F2: undo_count() is " << state.session.undo_count()
                      << ", expected " << (undo_before + 1U)
                      << " -- a confirmed reimport is exactly one history entry.\n";
            return false;
        }

        // F3. Reimport replaces preview objects before the remaining windows
        // render in this frame. Keep the real-button identity contract so those
        // windows pair the new atlas with the current skeleton. Phase 3 resolves
        // runtime views on demand; shell working values still synchronize after
        // adoption, but no raw-pointer rebinding is needed.
        if (state.preview_skeleton() !=
                marrow::editor::EditorSessionShellBinding::preview_skeleton(
                    state.session) ||
            state.animation_state() !=
                marrow::editor::EditorSessionShellBinding::preview_animation_state(
                    state.session)) {
            std::cerr << "MAR-190 F3: after Confirm the shell runtime views "
                         "must resolve the current session preview objects.\n";
            return false;
        }
    }

    // ---- W1 -- the repository is not written. Runs LAST. -------------------
    {
        ByteMap tracked_after;
        if (!tracked_bytes(&tracked_after, "MAR-190 W1")) {
            return false;
        }
        for (const auto& entry : tracked_before) {
            const auto found = tracked_after.find(entry.first);
            if (found == tracked_after.end() || found->second != entry.second) {
                std::cerr << "MAR-190 W1: tracked fixture " << entry.first
                          << " changed during the run.\n";
                return false;
            }
        }
    }

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);

    std::cout << "MAR-190 N1-N5: cancelling a review, closing its modal, a provenance "
                 "edit that outdates it, a vanished PSD and an injected commit failure "
                 "each leave the project, its runtime sources, its bytes, its history and "
                 "the selection unchanged, with the staging tree removed; and every "
                 "tracked fixture is byte-identical after the run; and a real mouse "
                 "opens the modal from the Project window, finds a row in each "
                 "section, reaches the Forget checkbox INSIDE the modal's usable "
                 "width, and confirms -- with Escape proven unable to close it.\n";
    return true;
}

} // namespace marrow::editor::shell
