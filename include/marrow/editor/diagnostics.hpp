#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/project.hpp"
#include "marrow/editor/selection.hpp"
#include "marrow/runtime/json.hpp"
#include "marrow/runtime/skeleton.hpp"

/**
 * @file diagnostics.hpp
 * @brief UI-free, read-only structured project diagnostics (MAR-186).
 *
 * The collector walks an opened project and returns a deterministic,
 * stably-identified list of problems. It draws nothing, focuses nothing,
 * selects nothing and repairs nothing: MAR-187 builds the Problems view and the
 * three safe fixes on top of this vocabulary.
 *
 * This header is **public** rather than living under `src/editor/` because
 * `marrow_editor`'s `src/editor` include directory is `PRIVATE`
 * (`CMakeLists.txt:522`) and `marrow_project_smoke` adds it only for its own
 * use; MAR-187 and the smoke both need these declarations.
 *
 * @par What the compiler polices here, and what it does not
 * Clang enables `-Wswitch` **by default**, with no `-Wall` and no explicit flag
 * (measured, not inferred: a two-of-three switch compiled with `c++ -std=c++17
 * -c` and nothing else warns). So every exhaustive switch over the four enums
 * below is checked, and adding a value produces a diagnostic at every site that
 * missed it. Two consequences are deliberate:
 *
 * - the four `*_name` functions are `switch`es with **no `default:` arm**,
 *   because a `default:` silences exactly the diagnostic that makes them safe;
 * - every switch over these enums lives in `src/editor/diagnostics.cpp`, so
 *   `grep -rn "DiagnosticPanel::\|DiagnosticSeverity::\|DiagnosticCode::\|DiagnosticOverlayFamily::"`
 *   finds the whole policed surface in one file.
 *
 * What the compiler does **not** police is a fixed-length list of calls — and
 * `collect_orphan_animation_overlays` has one, with one call per overlay family.
 * MAR-185 shipped a defect in precisely that class (`clipboard_track_count`, a
 * six-term sum never extended to a seventh family), so that list carries a
 * hand-written `static_assert` guard rather than relying on the compiler.
 */
namespace marrow::editor {

class EditorSession;

/**
 * @brief How badly a diagnostic issue affects what the project exports.
 *
 * `Error` means the exported runtime bundle is wrong — data that silently
 * corrupts what ships. `Warning` means the project is untidy or a reference has
 * quietly degraded, but nothing downstream is wrong yet.
 */
enum class DiagnosticSeverity {
    Error,
    Warning,
};

/**
 * @brief The editor panel an issue is about.
 *
 * Three values, because those are the three MAR-186 emits. MAR-187's AC2 names
 * five panels; the two it adds are MAR-187's to add, and Clang's default
 * `-Wswitch` turns that extension into a checklist. Shipping dead enumerators
 * now would let MAR-187 believe the sweep had already been done.
 *
 * This names a **panel**, never a window. The mapping onto
 * `kTimelineWindowTitle` and friends belongs to MAR-187, in the shell, where
 * those constants live — a model-layer module must not include `shell_state.hpp`.
 */
enum class DiagnosticPanel {
    Project,
    Timeline,
    Weights,
};

/**
 * @brief The issue vocabulary, typed.
 *
 * `code` is deliberately **not** a `std::string`. A fix handler holding only a
 * `safe_fix_id` cannot dispatch, and its only route back to the fact would be
 * splitting `identity` on `|` — the exact operation the identity escaping exists
 * to make unsafe. The wire spelling is produced by `diagnostic_code_name`.
 */
enum class DiagnosticCode {
    OverlayOrphanAnimation,
    OverlayOrphanWeightTarget,
    WeightsNonCanonical,
    WeightsUncanonicalizable,
    PreviewStaleAnimation,
    PreviewStaleSkin,
    ProjectUnsavedChanges,
};

/**
 * @brief Which `ProjectData` overlay vector an issue came from.
 *
 * `None` for issues that are not about an overlay. This exists so MAR-187's
 * `remove_orphan_overlay` fix can find the vector to erase from **directly**,
 * rather than parsing it back out of the identity string.
 */
enum class DiagnosticOverlayFamily {
    None,
    Transform,
    Inherit,
    Deform,
    DrawOrder,
    Event,
    SlotColor,
    SlotAttachment,
    MeshWeight,
};

/**
 * @brief The record coordinates a repair needs. Added by MAR-187 (see below).
 *
 * @par Why this exists, and why it is a DOWNSTREAM amendment
 * MAR-186 shipped `code` and `family` so a fix could find the right *vector*
 * without splitting `identity` on `|`. That answers "which of the eight
 * vectors" completely. It does **not** answer "which record **within** the
 * vector", and for two families that question has an answer neither MAR-186's
 * design nor MAR-187's asked for:
 *
 * - `Transform` records are keyed `{animation, bone, channel}` — two channels on
 *   one bone are two independent overlays, as
 *   `collect_orphan_animation_overlays` says in as many words — but
 *   `DiagnosticTarget` carries only a `BoneSelection`. Rotate and Translate on
 *   one bone produce two issues with **identical** targets.
 * - `Deform` records are keyed `{animation, slot, attachment}` while the target
 *   carries only a `SlotSelection`.
 * - `PreviewStaleSkin` is not an overlay at all, and its target carries only a
 *   panel — so the skin name it is about exists nowhere but inside `identity`.
 *   `reset_preview_reference` needs it to know which `preview_skins` entry to
 *   erase. Found the same way as the other two: by writing the repair and
 *   discovering it had nothing typed to work from.
 *
 * Without these fields `remove_orphan_overlay` can only recover the remainder by
 * parsing `identity` — the exact operation the escaping in `join_identity`
 * exists to make unsafe, because a `|` inside a user animation name would route
 * the erase to the wrong record.
 *
 * MAR-187 added this rather than reopening MAR-186 because MAR-186 had already
 * shipped and was under review when the gap was measured, and it uses none of
 * these fields itself. Recorded in `AGENTS.md` as a deliberate downstream
 * amendment, not as a precedent.
 *
 * @par What the compiler does and does not police here
 * The `switch (issue.family)` that consumes this **is** policed — a new
 * `DiagnosticOverlayFamily` value warns at every arm. **Populating** it is not:
 * the writes live inside `collect_orphan_animation_overlays`' seven-call list,
 * and nothing compile-time protects that list. The `static_assert` beside it is
 * a **tautology** — MAR-186's own review measured this by deleting a whole
 * family loop and watching an all-targets rebuild succeed silently. The real
 * guards are the cases that compare full identity lists: MAR-186's G1, and
 * MAR-187's X1, which additionally carries a sibling record per key so a fix
 * that erases the right vector but the wrong RECORD fails by name.
 */
struct OverlayRecordKey {
    /// Empty when the record is not animation-scoped (mesh-weight overlays).
    std::string animation_name;
    /// Set for `Transform` and `Inherit`.
    std::string bone_name;
    /// Set for `Deform`, `SlotColor`, `SlotAttachment` and `MeshWeight`.
    std::string slot_name;
    /// Set for `MeshWeight`, and for `PreviewStaleSkin`'s named skin.
    std::string skin_name;
    /// Set for `Deform` and `MeshWeight`.
    std::string attachment_name;
    /// Set for `Transform` only. The fourth key token that has no other home.
    std::optional<TransformTimelineChannel> channel;
};

/**
 * @brief Where the user should be taken to see the problem.
 *
 * `selection` is expressed in `SelectionSet`'s own vocabulary, so MAR-187
 * navigates by calling `SelectionSet::replace(*target.selection)` rather than
 * re-deriving an identity.
 *
 * This is about **navigation**, which is why MAR-187's `OverlayRecordKey` sits
 * on `DiagnosticIssue` beside `family` rather than here: a repair coordinate is
 * not a place to take the user, and two of its fields name records the user is
 * never navigated to.
 */
struct DiagnosticTarget {
    DiagnosticPanel panel{DiagnosticPanel::Project};
    /// Empty when the issue names nothing selectable (`draw_order`, `event`).
    std::optional<SelectionItem> selection;
    /// Empty when the issue is not animation-scoped.
    std::string animation_name;
    /// Set for weight issues only.
    std::optional<std::size_t> vertex_index;
};

/** @brief One structured project problem. */
struct DiagnosticIssue {
    DiagnosticCode code{DiagnosticCode::ProjectUnsavedChanges};
    DiagnosticOverlayFamily family{DiagnosticOverlayFamily::None};
    /**
     * @brief The record's own key fields, for a repair that erases one record.
     *
     * Default-constructed for every issue that is not about an overlay record.
     * MAR-187 (see `OverlayRecordKey`); MAR-186 emits it but never reads it.
     */
    OverlayRecordKey overlay_record;
    DiagnosticSeverity severity{DiagnosticSeverity::Warning};
    /**
     * @brief Stable identity, derived only from the code and the coordinates of
     * the thing the issue is about.
     *
     * Never from a vector index, never from iteration order, never from a float.
     * That is what makes the identity survive an unrelated edit that shifts the
     * offending record's position in its vector, which is what MAR-187 needs to
     * keep a selected row selected across a refresh.
     */
    std::string identity;
    std::string message;
    DiagnosticTarget target;
    /// Empty when no safe repair applies. Never a placeholder string.
    std::string safe_fix_id;
};

/** @brief A whole collection pass over one project. */
struct DiagnosticReport {
    /// Sorted by `identity`, ascending, and strictly increasing (de-duplicated).
    std::vector<DiagnosticIssue> issues;
    std::size_t error_count{0};
    std::size_t warning_count{0};
    bool project_dirty{false};
    std::uint64_t project_revision{0};
    std::uint64_t runtime_revision{0};
};

/// @brief Removes an orphaned overlay record. MAR-187 wires it.
inline constexpr std::string_view kSafeFixRemoveOrphanOverlay = "remove_orphan_overlay";
/// @brief Canonicalizes a weight vertex. The shipped agent operation's own name.
inline constexpr std::string_view kSafeFixNormalizeWeights = "normalize_weights";
/// @brief Clears a preview reference that resolves to nothing.
inline constexpr std::string_view kSafeFixResetPreviewReference = "reset_preview_reference";

/**
 * @brief Reports whether a safe-fix identifier is one of the allowed three.
 * @param safe_fix_id Identifier to check. An empty string is **not** allowlisted.
 * @return True only for the three `kSafeFix*` constants.
 */
bool is_allowlisted_safe_fix(std::string_view safe_fix_id);

/** @brief Wire spelling of a severity. Exhaustive switch, no `default:`. */
std::string_view diagnostic_severity_name(DiagnosticSeverity severity);
/** @brief Wire spelling of a panel. Exhaustive switch, no `default:`. */
std::string_view diagnostic_panel_name(DiagnosticPanel panel);
/** @brief Wire spelling of a code. Exhaustive switch, no `default:`. */
std::string_view diagnostic_code_name(DiagnosticCode code);
/** @brief Wire spelling of an overlay family. Exhaustive switch, no `default:`. */
std::string_view diagnostic_overlay_family_name(DiagnosticOverlayFamily family);

/**
 * @brief Collects every diagnostic a project itself can carry.
 * @param project Project overlays and editor metadata to inspect.
 * @param skeleton Materialized runtime skeleton the project resolves against.
 * @param base_skeleton_document Base runtime document, **before** overlay merge.
 * @return A sorted, de-duplicated, severity-counted report.
 *
 * A function of three `const&` values: no session, no history, no revisions, no
 * preview, no `SelectionSet` and no ImGui. AC2's "UI-free, read-only" is a
 * property of this signature rather than a claim about the body.
 *
 * `base_skeleton_document` is **required**, not optional. An optional input that
 * silently disabled the orphan sweep would be the exact shape of a test that
 * cannot fail: pass `nullptr`, the sweep never runs, and every "no orphans"
 * assertion passes for the wrong reason.
 *
 * The report's `project_dirty` is always false and its `project_revision` and
 * `runtime_revision` are always zero — those are facts only a session knows.
 */
DiagnosticReport collect_project_diagnostics(
    const ProjectData& project,
    const runtime::SkeletonData& skeleton,
    const runtime::json::Document& base_skeleton_document);

/**
 * @brief Collects diagnostics for a normally opened session.
 * @param session Session to inspect. Not modified.
 * @return The report, or `std::nullopt` when the session is not normally opened.
 *
 * Adds exactly what a session knows on top of `collect_project_diagnostics`:
 * `project_dirty`, the two revisions, and a `project.unsaved_changes` Warning
 * emitted exactly when `session.dirty()` — which is what keeps `warning_count`
 * numerically identical to the legacy `dirty ? 1 : 0` on a project with no other
 * warnings.
 *
 * The `std::nullopt` return is AC2's "limited to normally opened sessions" made
 * mechanical. Note that the dispatcher's own `ensure_project_loaded` guard checks
 * `has_project()`, `project()` and `runtime_data()` but **not**
 * `base_skeleton_document()`, so this function re-checks it by hand exactly as
 * `runtime.validate` does.
 */
std::optional<DiagnosticReport> collect_session_diagnostics(const EditorSession& session);

}  // namespace marrow::editor
