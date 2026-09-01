#include "marrow/editor/diagnostics.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "marrow/editor/session.hpp"
#include "mesh_weight_model.hpp"

namespace marrow::editor {
namespace {

/**
 * @brief Escapes one identity token so a join on `|` is unambiguous.
 *
 * Animation, bone, slot, skin and attachment names are arbitrary user-authored
 * strings and nothing in `validate_project_for_save` constrains their character
 * set. Two different issues whose names differ only in where a `|` falls would
 * otherwise collide on one identity -- and a collision is worse than a wrong
 * string, because `finalize`'s de-duplication cannot tell a collision from a
 * duplicate and DELETES one of the two issues.
 *
 * The backslash is escaped BEFORE the pipe. Doing it the other way round would
 * double-escape a backslash that precedes a pipe.
 */
std::string escape_identity_token(std::string_view token) {
    std::string escaped;
    escaped.reserve(token.size());
    for (const char character : token) {
        if (character == '\\' || character == '|') {
            escaped.push_back('\\');
        }
        escaped.push_back(character);
    }
    return escaped;
}

/**
 * @brief Builds an identity from a code and the coordinates of its subject.
 *
 * Never from a vector index, never from iteration order, never from a float.
 * The code is not escaped: it comes from a closed enum whose spellings contain
 * neither `|` nor `\`.
 */
std::string join_identity(
    DiagnosticCode code,
    const std::vector<std::string_view>& tokens) {
    std::string identity(diagnostic_code_name(code));
    for (const std::string_view token : tokens) {
        identity.push_back('|');
        identity += escape_identity_token(token);
    }
    return identity;
}

DiagnosticIssue make_issue(
    DiagnosticCode code,
    DiagnosticOverlayFamily family,
    DiagnosticSeverity severity,
    std::string identity,
    std::string message,
    DiagnosticTarget target,
    std::string_view safe_fix_id) {
    DiagnosticIssue issue;
    issue.code = code;
    issue.family = family;
    issue.severity = severity;
    issue.identity = std::move(identity);
    issue.message = std::move(message);
    issue.target = std::move(target);
    issue.safe_fix_id = std::string(safe_fix_id);
    return issue;
}

/** @brief The `.marrow` key a transform channel is stored under. */
std::string_view transform_channel_token(TransformTimelineChannel channel) {
    // Exhaustive, no `default:` -- see the header's note.
    switch (channel) {
    case TransformTimelineChannel::Rotate:
        return "rotate";
    case TransformTimelineChannel::Translate:
        return "translate";
    case TransformTimelineChannel::Scale:
        return "scale";
    case TransformTimelineChannel::Shear:
        return "shear";
    }
    return "rotate";
}

/**
 * @brief Sorts, de-duplicates and counts a collected issue list.
 *
 * De-duplication is adjacent-unique over the SORTED range rather than a hash
 * set, deliberately: it keeps this a single pass, and it means deleting the
 * sort is observable as a surviving duplicate rather than only as a reordering.
 *
 * Severity is NOT part of the sort. MAR-187 groups and filters by severity;
 * a collector that pre-grouped would force it to re-sort to get a stable row
 * identity.
 */
void finalize(DiagnosticReport* report) {
    std::sort(
        report->issues.begin(),
        report->issues.end(),
        [](const DiagnosticIssue& left, const DiagnosticIssue& right) {
            return left.identity < right.identity;
        });
    report->issues.erase(
        std::unique(
            report->issues.begin(),
            report->issues.end(),
            [](const DiagnosticIssue& left, const DiagnosticIssue& right) {
                return left.identity == right.identity;
            }),
        report->issues.end());

    report->error_count = 0;
    report->warning_count = 0;
    for (const DiagnosticIssue& issue : report->issues) {
        if (issue.severity == DiagnosticSeverity::Error) {
            ++report->error_count;
        } else {
            ++report->warning_count;
        }
    }
}

/** @brief Whether `name` is absent from a sorted authored-animation list. */
bool is_orphan_animation(
    const std::vector<std::string>& authored,
    const std::string& name) {
    return !std::binary_search(authored.begin(), authored.end(), name);
}

std::string orphan_animation_message(
    std::string_view family_label,
    const std::string& animation_name,
    std::string_view scope) {
    std::string message = "The ";
    message += family_label;
    message += " overlay";
    if (!scope.empty()) {
        message += " on ";
        message += scope;
    }
    message += " names animation '";
    message += animation_name;
    message +=
        "', which the project does not author. Building the runtime document "
        "creates that animation, so the phantom is written into every .mskl and "
        ".mbin export.";
    return message;
}

/**
 * @brief Emits one `overlay.orphan_animation` issue for one overlay record.
 *
 * `scope_tokens` are the family's own coordinates after the animation name, and
 * they are what makes two overlays of one family on one animation distinct.
 */
DiagnosticIssue make_orphan_animation_issue(
    DiagnosticOverlayFamily family,
    std::string_view family_token,
    std::string_view family_label,
    const std::string& animation_name,
    const std::vector<std::string_view>& scope_tokens,
    std::string_view scope_label,
    std::optional<SelectionItem> selection) {
    std::vector<std::string_view> tokens;
    tokens.reserve(scope_tokens.size() + 2U);
    tokens.push_back(family_token);
    tokens.push_back(animation_name);
    for (const std::string_view token : scope_tokens) {
        tokens.push_back(token);
    }

    DiagnosticTarget target;
    target.panel = DiagnosticPanel::Timeline;
    target.selection = std::move(selection);
    target.animation_name = animation_name;

    return make_issue(
        DiagnosticCode::OverlayOrphanAnimation,
        family,
        // Error, not Warning: unlike every other family here, this one corrupts
        // what ships.
        DiagnosticSeverity::Error,
        join_identity(DiagnosticCode::OverlayOrphanAnimation, tokens),
        orphan_animation_message(family_label, animation_name, scope_label),
        std::move(target),
        kSafeFixRemoveOrphanOverlay);
}

/**
 * @brief Sweeps every animation-scoped overlay vector for orphaned animations.
 *
 * The authority is `authored_animation_names` -- the `animation_edits` fold over
 * the base document -- and NOT the materialized `SkeletonData`. The materialized
 * skeleton already contains the phantom animations this function exists to
 * find, because `build_runtime_document`'s merge loops call
 * `ensure_object_member(animations, edit.animation_name)` and CREATE them. A
 * collector resolving against it reports a clean project, always, and every
 * count-only test still passes.
 */
void collect_orphan_animation_overlays(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document,
    std::vector<DiagnosticIssue>* issues) {
    const std::vector<std::string> authored =
        authored_animation_names(project, base_skeleton_document);

    // ===================================================================
    // Every animation-scoped overlay vector in ProjectData must be swept
    // here, and NOTHING CHECKS THIS LIST -- not the compiler, not any
    // shipped case. The four enums in this module ARE compiler-policed
    // (Clang's `-Wswitch` is on by default), but a fixed-length list of
    // calls is exactly the class MAR-185 shipped a defect into:
    // `clipboard_track_count` was a six-term sum never extended to the
    // seventh timeline family, and it broke the paste remap in both
    // directions.
    //
    // If you add an overlay vector to ProjectData, add a call below AND
    // bump this number. The `static_assert` cannot detect the omission on
    // its own -- nothing can, in C++17 without reflection -- but it puts
    // the number in your edit path, and the project smoke's G1 asserts all
    // seven identities, so a changed number unmatched by a call fails.
    // ===================================================================
    constexpr std::size_t kSweptOverlayFamilyCount = 7;
    static_assert(
        kSweptOverlayFamilyCount == 7,
        "an overlay family was added to ProjectData without a sweep here");

    // 1/7 -- transform. The channel token is part of the key: two channels on
    // one bone are two independent overlays.
    for (const TransformTimelineEdit& edit : project.transform_timeline_edits) {
        if (!is_orphan_animation(authored, edit.animation_name)) {
            continue;
        }
        issues->push_back(make_orphan_animation_issue(
            DiagnosticOverlayFamily::Transform,
            "transform",
            "transform timeline",
            edit.animation_name,
            {edit.bone_name, transform_channel_token(edit.channel)},
            "bone '" + edit.bone_name + "'",
            BoneSelection{edit.bone_name}));
    }

    // 2/7 -- bone inherit.
    for (const BoneInheritTimelineEdit& edit : project.bone_inherit_timeline_edits) {
        if (!is_orphan_animation(authored, edit.animation_name)) {
            continue;
        }
        issues->push_back(make_orphan_animation_issue(
            DiagnosticOverlayFamily::Inherit,
            "inherit",
            "bone-inherit timeline",
            edit.animation_name,
            {edit.bone_name},
            "bone '" + edit.bone_name + "'",
            BoneSelection{edit.bone_name}));
    }

    // 3/7 -- mesh deform.
    for (const MeshDeformTimelineEdit& edit : project.mesh_deform_timeline_edits) {
        if (!is_orphan_animation(authored, edit.animation_name)) {
            continue;
        }
        issues->push_back(make_orphan_animation_issue(
            DiagnosticOverlayFamily::Deform,
            "deform",
            "mesh-deform timeline",
            edit.animation_name,
            {edit.slot_name, edit.attachment_name},
            "slot '" + edit.slot_name + "' attachment '" + edit.attachment_name +
                "'",
            SlotSelection{edit.slot_name}));
    }

    // 4/7 -- draw order. Animation-scoped only; nothing selectable.
    for (const DrawOrderTimelineEdit& edit : project.draw_order_timeline_edits) {
        if (!is_orphan_animation(authored, edit.animation_name)) {
            continue;
        }
        issues->push_back(make_orphan_animation_issue(
            DiagnosticOverlayFamily::DrawOrder,
            "draw_order",
            "draw-order timeline",
            edit.animation_name,
            {},
            {},
            std::nullopt));
    }

    // 5/7 -- events. Animation-scoped only; nothing selectable.
    for (const EventTimelineEdit& edit : project.event_timeline_edits) {
        if (!is_orphan_animation(authored, edit.animation_name)) {
            continue;
        }
        issues->push_back(make_orphan_animation_issue(
            DiagnosticOverlayFamily::Event,
            "event",
            "event timeline",
            edit.animation_name,
            {},
            {},
            std::nullopt));
    }

    // 6/7 -- slot colour.
    for (const SlotColorTimelineEdit& edit : project.slot_color_timeline_edits) {
        if (!is_orphan_animation(authored, edit.animation_name)) {
            continue;
        }
        issues->push_back(make_orphan_animation_issue(
            DiagnosticOverlayFamily::SlotColor,
            "slot_color",
            "slot-colour timeline",
            edit.animation_name,
            {edit.slot_name},
            "slot '" + edit.slot_name + "'",
            SlotSelection{edit.slot_name}));
    }

    // 7/7 -- slot attachment.
    for (const SlotAttachmentTimelineEdit& edit :
         project.slot_attachment_timeline_edits) {
        if (!is_orphan_animation(authored, edit.animation_name)) {
            continue;
        }
        issues->push_back(make_orphan_animation_issue(
            DiagnosticOverlayFamily::SlotAttachment,
            "slot_attachment",
            "slot-attachment timeline",
            edit.animation_name,
            {edit.slot_name},
            "slot '" + edit.slot_name + "'",
            SlotSelection{edit.slot_name}));
    }
}

/**
 * @brief Reports mesh-weight overlays whose skin/slot/attachment resolves to
 * nothing, and the canonicality of the ones that do resolve.
 *
 * The two live in one function because of the SUPERSESSION: when a target does
 * not resolve, that edit's vertices are NOT examined. Their weights are dead
 * data the runtime never sees -- `build_runtime_document` skips the whole edit
 * when `find_skin_attachment_value` returns null -- so reporting that the dead
 * data is also badly sorted would give the user two rows for one problem, and
 * MAR-187's `remove_orphan_overlay` would then make the second row vanish
 * without the user having addressed it. One edit, one issue.
 *
 * Resolving the triple against the MATERIALIZED skeleton is correct here, and
 * is not the same mistake as resolving an animation against it:
 * `ensure_object_member` is used on `animations`, and on bones/slots/deform
 * WITHIN an animation, but never on the skeleton's skins, slots or attachments.
 * Those come from the base document unchanged, so the materialized skeleton
 * cannot manufacture the thing being looked for.
 */
void collect_mesh_weight_overlays(
    const ProjectData& project,
    const runtime::SkeletonData& skeleton,
    std::vector<DiagnosticIssue>* issues) {
    for (const MeshWeightAttachmentEdit& edit : project.mesh_weight_attachment_edits) {
        const auto slot_index = skeleton.find_slot_index(edit.slot_name);
        const runtime::AttachmentData* attachment = slot_index.has_value()
            ? skeleton.find_attachment(edit.skin_name, *slot_index, edit.attachment_name)
            : nullptr;

        if (attachment == nullptr) {
            DiagnosticTarget target;
            target.panel = DiagnosticPanel::Weights;
            // AttachmentSelection's field order is slot_name, skin_name,
            // attachment_name -- while MeshWeightAttachmentEdit's is skin_name,
            // slot_name, attachment_name. THE TWO ARE TRANSPOSED. Aggregate
            // initialization in the edit's field order silently produces a
            // target with skin and slot swapped, and every assertion that only
            // checks a count, a code or an identity still passes.
            target.selection =
                AttachmentSelection{edit.slot_name, edit.skin_name, edit.attachment_name};
            // This selection deliberately does NOT resolve. It names the missing
            // triple, because naming it is the only way the user can see what is
            // orphaned; MAR-187's "handles removed targets without stale
            // selection or crashes" is the clause that covers it.

            issues->push_back(make_issue(
                DiagnosticCode::OverlayOrphanWeightTarget,
                DiagnosticOverlayFamily::MeshWeight,
                // Warning, not Error: this is dead data. Nothing downstream
                // ever sees it.
                DiagnosticSeverity::Warning,
                join_identity(
                    DiagnosticCode::OverlayOrphanWeightTarget,
                    {edit.skin_name, edit.slot_name, edit.attachment_name}),
                "The mesh-weight overlay for skin '" + edit.skin_name + "', slot '" +
                    edit.slot_name + "', attachment '" + edit.attachment_name +
                    "' resolves to no attachment. Building the runtime document "
                    "skips it, so the weights it holds are dead data that "
                    "survives every save.",
                std::move(target),
                kSafeFixRemoveOrphanOverlay));
            continue;
        }

        for (std::size_t index = 0; index < edit.vertices.size(); ++index) {
            // The canonicalizer IS the predicate: there is no shipped "is this
            // canonical" test, and canonicalization is documented as a
            // BIT-EXACT FIXED POINT -- measured for this fixture before this
            // code was written, so the `==` below is licensed rather than
            // hopeful. An approximate comparison would report an
            // already-canonical vertex as non-canonical on some inputs and not
            // others, which is the opposite of deterministic.
            MeshWeightVertexEdit candidate = edit.vertices[index];
            const std::string error =
                mesh_weight_model::canonicalize_mesh_weight_vertex(skeleton, &candidate);

            const std::string vertex_token = std::to_string(index);
            const std::vector<std::string_view> scoped = {
                edit.skin_name, edit.slot_name, edit.attachment_name, vertex_token};

            DiagnosticTarget target;
            target.panel = DiagnosticPanel::Weights;
            // Slot FIRST -- the two structs are transposed, see above.
            target.selection =
                AttachmentSelection{edit.slot_name, edit.skin_name, edit.attachment_name};
            target.vertex_index = index;

            if (!error.empty()) {
                issues->push_back(make_issue(
                    DiagnosticCode::WeightsUncanonicalizable,
                    DiagnosticOverlayFamily::MeshWeight,
                    DiagnosticSeverity::Error,
                    join_identity(DiagnosticCode::WeightsUncanonicalizable, scoped),
                    "Vertex " + vertex_token + " of skin '" + edit.skin_name +
                        "', slot '" + edit.slot_name + "', attachment '" +
                        edit.attachment_name + "' cannot be canonicalized: " + error,
                    std::move(target),
                    // NO safe fix, deliberately. Normalization is precisely what
                    // cannot repair a vertex the canonicalizer rejects.
                    std::string_view()));
                continue;
            }

            if (candidate.influences.size() == edit.vertices[index].influences.size()) {
                bool identical = true;
                for (std::size_t i = 0; i < candidate.influences.size(); ++i) {
                    const MeshWeightInfluenceEdit& before = edit.vertices[index].influences[i];
                    const MeshWeightInfluenceEdit& after = candidate.influences[i];
                    if (before.bone_name != after.bone_name || !(before.x == after.x) ||
                        !(before.y == after.y) || !(before.weight == after.weight)) {
                        identical = false;
                        break;
                    }
                }
                if (identical) {
                    continue;
                }
            }

            issues->push_back(make_issue(
                DiagnosticCode::WeightsNonCanonical,
                DiagnosticOverlayFamily::MeshWeight,
                // Warning: the runtime normalizes at load, so what renders is
                // right; only the stored data is not the fixed point.
                DiagnosticSeverity::Warning,
                join_identity(DiagnosticCode::WeightsNonCanonical, scoped),
                "Vertex " + vertex_token + " of skin '" + edit.skin_name +
                    "', slot '" + edit.slot_name + "', attachment '" +
                    edit.attachment_name +
                    "' is not the canonical form: canonicalization would reorder "
                    "or renormalize its influences.",
                std::move(target),
                kSafeFixNormalizeWeights));
        }
    }
}

/**
 * @brief Reports preview references that resolve to nothing.
 *
 * Both halves resolve against the MATERIALIZED skeleton, and NOT against
 * `authored_animation_names`. That asymmetry with the orphan-animation sweep is
 * deliberate: the materialized skeleton is what the preview actually binds
 * against, so if an orphan overlay has resurrected the animation the project
 * points at, the preview genuinely works and there is nothing stale to report.
 * Using the authored list here would be a plausible and wrong symmetry.
 *
 * `PreviewController::normalize_state` silently SUBSTITUTES rather than failing
 * -- an unresolvable active animation is replaced by the first animation, and
 * unresolvable skins are dropped -- so the stale name stays on disk and every
 * open re-substitutes. That silence is exactly what makes this class worth
 * reporting.
 *
 * Repeated skin names are emitted once per vector entry and collapsed by
 * `finalize`. Doing it that way, rather than de-duplicating here, is what makes
 * the de-duplication observable at all: `preview_skins` is the one vector
 * nothing else refuses duplicates in.
 */
void collect_stale_preview_references(
    const ProjectData& project,
    const runtime::SkeletonData& skeleton,
    std::vector<DiagnosticIssue>* issues) {
    const std::string& active = project.editor_metadata.active_animation;
    // An empty active animation is the setup pose, which is legal and is not a
    // stale reference.
    if (!active.empty() && skeleton.find_animation(active) == nullptr) {
        DiagnosticTarget target;
        target.panel = DiagnosticPanel::Project;
        target.animation_name = active;
        issues->push_back(make_issue(
            DiagnosticCode::PreviewStaleAnimation,
            DiagnosticOverlayFamily::None,
            // Warning: the preview falls back silently and nothing exported is
            // affected.
            DiagnosticSeverity::Warning,
            join_identity(DiagnosticCode::PreviewStaleAnimation, {active}),
            "The preview animation '" + active +
                "' does not resolve. Opening the project silently substitutes "
                "the first animation, and the stale name stays on disk.",
            std::move(target),
            kSafeFixResetPreviewReference));
    }

    for (const std::string& skin_name : project.editor_metadata.preview_skins) {
        // An empty entry is not a state a loaded project can be in --
        // `validate_project_for_save` already refuses to save one -- so
        // reporting it would be an unreachable branch.
        if (skin_name.empty() || skeleton.find_skin_index(skin_name).has_value()) {
            continue;
        }
        DiagnosticTarget target;
        target.panel = DiagnosticPanel::Project;
        issues->push_back(make_issue(
            DiagnosticCode::PreviewStaleSkin,
            DiagnosticOverlayFamily::None,
            DiagnosticSeverity::Warning,
            join_identity(DiagnosticCode::PreviewStaleSkin, {skin_name}),
            "The preview skin '" + skin_name +
                "' does not resolve. It is silently dropped at bind time, and "
                "the stale name stays on disk.",
            std::move(target),
            kSafeFixResetPreviewReference));
    }
}

}  // namespace

bool is_allowlisted_safe_fix(std::string_view safe_fix_id) {
    return safe_fix_id == kSafeFixRemoveOrphanOverlay ||
        safe_fix_id == kSafeFixNormalizeWeights ||
        safe_fix_id == kSafeFixResetPreviewReference;
}

// Every `*_name` function below is an exhaustive switch with NO `default:` arm.
// Clang enables `-Wswitch` by default, so adding an enumerator names every site
// that missed it; a `default:` would silence exactly that diagnostic. Do not add
// one. The trailing `return` after each switch is unreachable for a valid value
// and exists only because the function must return on every path.
std::string_view diagnostic_severity_name(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Error:
        return "error";
    case DiagnosticSeverity::Warning:
        return "warning";
    }
    return "warning";
}

std::string_view diagnostic_panel_name(DiagnosticPanel panel) {
    switch (panel) {
    case DiagnosticPanel::Project:
        return "project";
    case DiagnosticPanel::Timeline:
        return "timeline";
    case DiagnosticPanel::Weights:
        return "weights";
    }
    return "project";
}

std::string_view diagnostic_code_name(DiagnosticCode code) {
    switch (code) {
    case DiagnosticCode::OverlayOrphanAnimation:
        return "overlay.orphan_animation";
    case DiagnosticCode::OverlayOrphanWeightTarget:
        return "overlay.orphan_weight_target";
    case DiagnosticCode::WeightsNonCanonical:
        return "weights.non_canonical";
    case DiagnosticCode::WeightsUncanonicalizable:
        return "weights.uncanonicalizable";
    case DiagnosticCode::PreviewStaleAnimation:
        return "preview.stale_animation";
    case DiagnosticCode::PreviewStaleSkin:
        return "preview.stale_skin";
    case DiagnosticCode::ProjectUnsavedChanges:
        return "project.unsaved_changes";
    }
    return "project.unsaved_changes";
}

std::string_view diagnostic_overlay_family_name(DiagnosticOverlayFamily family) {
    switch (family) {
    case DiagnosticOverlayFamily::None:
        return "none";
    case DiagnosticOverlayFamily::Transform:
        return "transform";
    case DiagnosticOverlayFamily::Inherit:
        return "inherit";
    case DiagnosticOverlayFamily::Deform:
        return "deform";
    case DiagnosticOverlayFamily::DrawOrder:
        return "draw_order";
    case DiagnosticOverlayFamily::Event:
        return "event";
    case DiagnosticOverlayFamily::SlotColor:
        return "slot_color";
    case DiagnosticOverlayFamily::SlotAttachment:
        return "slot_attachment";
    case DiagnosticOverlayFamily::MeshWeight:
        return "mesh_weight";
    }
    return "none";
}

DiagnosticReport collect_project_diagnostics(
    const ProjectData& project,
    const runtime::SkeletonData& skeleton,
    const runtime::json::Document& base_skeleton_document) {
    DiagnosticReport report;
    collect_orphan_animation_overlays(project, base_skeleton_document, &report.issues);
    collect_mesh_weight_overlays(project, skeleton, &report.issues);
    collect_stale_preview_references(project, skeleton, &report.issues);
    finalize(&report);
    return report;
}

std::optional<DiagnosticReport> collect_session_diagnostics(const EditorSession& session) {
    // AC2's "limited to normally opened sessions" made mechanical. The
    // dispatcher's own `ensure_project_loaded` checks the first three but NOT
    // `base_skeleton_document()`, so this re-checks it by hand exactly as
    // `runtime.validate` does -- and the pure collector needs that document, so
    // an unchecked null here would be a crash rather than a nullopt.
    if (!session.has_project() || session.project() == nullptr ||
        session.runtime_data() == nullptr ||
        session.base_skeleton_document() == nullptr) {
        return std::nullopt;
    }

    DiagnosticReport report = collect_project_diagnostics(
        *session.project(), *session.runtime_data(), *session.base_skeleton_document());
    report.project_dirty = session.dirty();
    report.project_revision = session.project_revision();
    report.runtime_revision = session.runtime_revision();

    if (session.dirty()) {
        // This issue is what keeps `warning_count` numerically identical to the
        // legacy `session.dirty() ? 1 : 0`. `error_count` and `warning_count`
        // now count issues by severity -- they have to, since severity counts
        // are required -- and that would otherwise drop the shipped behaviour.
        // With this issue: a clean project with no problems is 0/0, and a dirty
        // project with no other problems is 0/1, exactly as before.
        //
        // No safe fix: saving is a reviewed, user-initiated operation, never an
        // automatic repair.
        DiagnosticTarget target;
        target.panel = DiagnosticPanel::Project;
        report.issues.push_back(make_issue(
            DiagnosticCode::ProjectUnsavedChanges,
            DiagnosticOverlayFamily::None,
            DiagnosticSeverity::Warning,
            join_identity(DiagnosticCode::ProjectUnsavedChanges, {}),
            "The project has unsaved changes.",
            std::move(target),
            std::string_view()));
        // RE-SORT and RE-COUNT. Pushing onto an already-sorted vector and
        // hoping would break the strictly-increasing invariant, and counting
        // severities before this append leaves `warning_count` one short on
        // every dirty project.
        finalize(&report);
        report.project_dirty = session.dirty();
        report.project_revision = session.project_revision();
        report.runtime_revision = session.runtime_revision();
    }
    return report;
}

}  // namespace marrow::editor
