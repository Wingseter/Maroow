#include "marrow/editor/safe_fix.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "marrow/editor/authoring.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/editor/session.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace marrow::editor {
namespace {

SafeFixResult reject(std::string message, std::string identity) {
    SafeFixResult result;
    result.ok = false;
    result.changed = false;
    result.error = std::move(message);
    result.applied_identity = std::move(identity);
    return result;
}

/**
 * @brief Erases every record of one vector matching a predicate.
 * @return How many records went.
 */
template <typename Vector, typename Predicate>
std::size_t erase_matching(Vector* records, Predicate predicate) {
    const std::size_t before = records->size();
    records->erase(
        std::remove_if(records->begin(), records->end(), predicate), records->end());
    return before - records->size();
}

/**
 * @brief Erases the ONE overlay record an issue names.
 *
 * A `switch` over `DiagnosticOverlayFamily`, which is what makes an eighth
 * family a compile warning here rather than a silent no-op. The alternative --
 * reimplementing `erase_all_timeline_edits`' seven-vector call list -- was
 * refused deliberately: that list is a hand-maintained one of exactly the class
 * MAR-185 shipped a defect into, it is unreachable from here anyway (it lives
 * inside `authoring.cpp`'s anonymous namespace), and it erases a WHOLE
 * ANIMATION's overlays rather than one record.
 *
 * Every arm matches on the record's own key fields, taken from
 * `issue.overlay_record`. Two of those keys -- the transform CHANNEL and the
 * deform ATTACHMENT -- exist nowhere in `DiagnosticTarget`, so without that
 * typed key this function would have to split `identity` on `|`, which the
 * escaping exists to make unsafe.
 */
std::size_t erase_overlay_record(ProjectData* project, const DiagnosticIssue& issue) {
    const OverlayRecordKey& key = issue.overlay_record;
    switch (issue.family) {
    case DiagnosticOverlayFamily::Transform:
        // The channel is part of the key: two channels on one bone are two
        // independent overlays whose issues carry IDENTICAL targets.
        if (!key.channel.has_value()) {
            return 0;
        }
        return erase_matching(
            &project->transform_timeline_edits,
            [&](const TransformTimelineEdit& edit) {
                return edit.animation_name == key.animation_name &&
                    edit.bone_name == key.bone_name && edit.channel == *key.channel;
            });
    case DiagnosticOverlayFamily::Inherit:
        return erase_matching(
            &project->bone_inherit_timeline_edits,
            [&](const BoneInheritTimelineEdit& edit) {
                return edit.animation_name == key.animation_name &&
                    edit.bone_name == key.bone_name;
            });
    case DiagnosticOverlayFamily::Deform:
        // The attachment name is part of the key, for the same reason.
        return erase_matching(
            &project->mesh_deform_timeline_edits,
            [&](const MeshDeformTimelineEdit& edit) {
                return edit.animation_name == key.animation_name &&
                    edit.slot_name == key.slot_name &&
                    edit.attachment_name == key.attachment_name;
            });
    case DiagnosticOverlayFamily::DrawOrder:
        return erase_matching(
            &project->draw_order_timeline_edits,
            [&](const DrawOrderTimelineEdit& edit) {
                return edit.animation_name == key.animation_name;
            });
    case DiagnosticOverlayFamily::Event:
        return erase_matching(
            &project->event_timeline_edits, [&](const EventTimelineEdit& edit) {
                return edit.animation_name == key.animation_name;
            });
    case DiagnosticOverlayFamily::SlotColor:
        return erase_matching(
            &project->slot_color_timeline_edits,
            [&](const SlotColorTimelineEdit& edit) {
                return edit.animation_name == key.animation_name &&
                    edit.slot_name == key.slot_name;
            });
    case DiagnosticOverlayFamily::SlotAttachment:
        return erase_matching(
            &project->slot_attachment_timeline_edits,
            [&](const SlotAttachmentTimelineEdit& edit) {
                return edit.animation_name == key.animation_name &&
                    edit.slot_name == key.slot_name;
            });
    case DiagnosticOverlayFamily::MeshWeight:
        return erase_matching(
            &project->mesh_weight_attachment_edits,
            [&](const MeshWeightAttachmentEdit& edit) {
                return edit.skin_name == key.skin_name &&
                    edit.slot_name == key.slot_name &&
                    edit.attachment_name == key.attachment_name;
            });
    case DiagnosticOverlayFamily::None:
        return 0;
    }
    return 0;
}

/** @brief The `normalize_weights` arm. Repairs ONE vertex, never an attachment. */
std::string normalize_one_vertex(
    ProjectData* project,
    const runtime::SkeletonData& skeleton,
    const DiagnosticIssue& issue,
    std::size_t* affected_out) {
    const OverlayRecordKey& key = issue.overlay_record;
    if (!issue.target.vertex_index.has_value()) {
        return "This weight problem names no vertex, so there is nothing to "
               "normalize.";
    }
    const auto slot_index = skeleton.find_slot_index(key.slot_name);
    if (!slot_index.has_value()) {
        return "Slot '" + key.slot_name + "' is not in this runtime.";
    }
    const runtime::AttachmentData* attachment =
        skeleton.find_attachment(key.skin_name, *slot_index, key.attachment_name);
    if (attachment == nullptr) {
        return "Skin '" + key.skin_name + "', slot '" + key.slot_name +
            "', attachment '" + key.attachment_name + "' is not in this runtime.";
    }
    // Built from `OverlayRecordKey`, whose fields carry the record's OWN names,
    // and NOT from `issue.target.selection`: `AttachmentSelection` is
    // `{slot, skin, attachment}` while `MeshWeightTarget` is
    // `{skin, slot, attachment}` -- THE TWO ARE TRANSPOSED. Aggregate-
    // initializing one from the other compiles, runs, and was measured to
    // produce a target naming a skin called `body`, which
    // `normalize_mesh_weights` does NOT reject: it creates a fresh
    // `MeshWeightAttachmentEdit` at those bogus coordinates, the project still
    // saves and loads, and the only symptom is a brand-new
    // `overlay.orphan_weight_target` Warning where a problem was meant to go
    // away. Naming the fields explicitly is what keeps a swap a visible typo.
    const MeshWeightTarget target{key.skin_name, key.slot_name, key.attachment_name};
    // The SCOPE is the point. `normalize_weights_command` passes
    // `weight_command_scope(state)`, which is EMPTY -- meaning every vertex --
    // unless an FFD selection narrows it. Wiring a Problems row to that would
    // canonicalize the whole attachment and silently repair vertices the user
    // never saw a row for.
    const std::vector<std::size_t> scope = {*issue.target.vertex_index};
    const MeshWeightResult result =
        normalize_mesh_weights(project, skeleton, *attachment, target, scope);
    if (!result) {
        return result.error;
    }
    *affected_out = result.affected_vertices.size();
    return {};
}

/** @brief The `reset_preview_reference` arm. Two codes, two behaviours. */
std::string reset_preview_reference(
    ProjectData* project,
    const EditorSession& session,
    const DiagnosticIssue& issue,
    bool* changed_out) {
    switch (issue.code) {
    case DiagnosticCode::PreviewStaleAnimation: {
        // Write what the runtime ALREADY substituted, rather than clearing the
        // field. `PreviewController::normalize_state` replaces an unresolvable
        // active animation with the skeleton's first one (or empty when it has
        // none), so this makes the stored data agree with what the editor has
        // been showing all along: the repair changes DATA, not BEHAVIOUR.
        // Clearing it instead would switch the preview to the setup pose.
        const std::string& substituted = session.preview_state().animation_name;
        if (project->editor_metadata.active_animation == substituted) {
            return {};
        }
        project->editor_metadata.active_animation = substituted;
        *changed_out = true;
        return {};
    }
    case DiagnosticCode::PreviewStaleSkin: {
        // Erase EVERY entry equal to this issue's named skin, and nothing else.
        // MAR-186 emits one issue per DISTINCT unresolvable name, so all copies
        // of that one name are exactly the issue's extent. Rebuilding the vector
        // from `preview_state().skin_names` would additionally collapse a
        // RESOLVABLE duplicate that no issue named -- `normalize_state`
        // de-duplicates as well as filters.
        std::vector<std::string>& skins = project->editor_metadata.preview_skins;
        const std::size_t before = skins.size();
        skins.erase(
            std::remove(skins.begin(), skins.end(), issue.overlay_record.skin_name),
            skins.end());
        *changed_out = skins.size() != before;
        return {};
    }
    case DiagnosticCode::OverlayOrphanAnimation:
    case DiagnosticCode::OverlayOrphanWeightTarget:
    case DiagnosticCode::WeightsNonCanonical:
    case DiagnosticCode::WeightsUncanonicalizable:
    case DiagnosticCode::ProjectUnsavedChanges:
        // Unreachable: MAR-186 attaches `reset_preview_reference` to exactly the
        // two codes above. Written because the compiler named the arms, and
        // because a silent fall-through would repair nothing while reporting
        // success.
        return "This problem is not a preview reference.";
    }
    return "This problem is not a preview reference.";
}

}  // namespace

std::optional<SafeFixKind> safe_fix_kind_for(std::string_view safe_fix_id) {
    // THE one list in this story the compiler cannot check. Nothing warns if a
    // fourth entry is added or one is misspelled, which is why X7 sweeps a
    // corpus -- the three ids, the empty string, four near misses and all 66
    // shipped agent operation names -- and asserts the accepted set is EXACTLY
    // these three, as a named set difference.
    if (safe_fix_id == kSafeFixRemoveOrphanOverlay) {
        return SafeFixKind::RemoveOrphanOverlay;
    }
    if (safe_fix_id == kSafeFixNormalizeWeights) {
        return SafeFixKind::NormalizeWeights;
    }
    if (safe_fix_id == kSafeFixResetPreviewReference) {
        return SafeFixKind::ResetPreviewReference;
    }
    return std::nullopt;
}

SafeFixResult apply_safe_fix(EditorSession& session, const DiagnosticIssue& issue) {
    // --- 1. Session preflight. No transaction is opened. -------------------
    if (!session.has_project() || session.project() == nullptr ||
        session.runtime_data() == nullptr) {
        return reject(
            "A safe fix needs an open project; this session has none.", issue.identity);
    }
    if (session.transaction_active()) {
        return reject(
            "A safe fix cannot run while another edit transaction is open.",
            issue.identity);
    }

    // --- 2. Allowlist preflight. ------------------------------------------
    const std::optional<SafeFixKind> kind = safe_fix_kind_for(issue.safe_fix_id);
    if (!kind.has_value()) {
        return reject(
            "'" + issue.safe_fix_id +
                "' is not one of the three allowlisted safe fixes.",
            issue.identity);
    }

    // --- 3. Freshness preflight. ------------------------------------------
    //
    // A `ProblemsView` can be one revision behind the session: the user clicks
    // Fix on a row whose issue another edit already removed. Applying it would
    // mutate something nobody asked about, so the issue must still be there,
    // with the same identity AND the same fix.
    {
        const auto fresh = collect_session_diagnostics(session);
        if (!fresh.has_value()) {
            return reject(
                "A safe fix needs a normally opened session.", issue.identity);
        }
        const auto match = std::find_if(
            fresh->issues.begin(), fresh->issues.end(),
            [&](const DiagnosticIssue& candidate) {
                return candidate.identity == issue.identity &&
                    candidate.safe_fix_id == issue.safe_fix_id;
            });
        if (match == fresh->issues.end()) {
            return reject(
                "The problem '" + issue.identity +
                    "' is no longer present, so there is nothing to fix. The "
                    "Problems view is out of date.",
                issue.identity);
        }
    }

    // --- 4. One transaction. ----------------------------------------------
    EditorSession::EditTransaction transaction = session.begin_edit(
        {EditKind::EditProperty,
         "Fix problem: " + issue.identity,
         "mar187:safe-fix",
         false,
         EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
    if (!transaction) {
        return reject(
            transaction.error().has_value()
                ? transaction.error()->message
                : std::string("The session refused the edit."),
            issue.identity);
    }

    bool changed = false;
    std::string error;
    switch (*kind) {
    case SafeFixKind::RemoveOrphanOverlay:
        changed = erase_overlay_record(transaction.project(), issue) != 0U;
        break;
    case SafeFixKind::NormalizeWeights: {
        std::size_t affected = 0;
        error = normalize_one_vertex(
            transaction.project(), *session.runtime_data(), issue, &affected);
        changed = affected != 0U;
        break;
    }
    case SafeFixKind::ResetPreviewReference:
        error = reset_preview_reference(transaction.project(), session, issue, &changed);
        break;
    }

    if (!error.empty()) {
        // The primitive's message, VERBATIM. Paraphrasing it here would make
        // every rejection assertion in the suite a test of this file's prose.
        transaction.cancel();
        return reject(std::move(error), issue.identity);
    }
    if (!changed) {
        // A repair with nothing to repair must not record history, exactly as
        // `run_weight_command` already decides for the weight commands.
        transaction.cancel();
        SafeFixResult result;
        result.ok = true;
        result.changed = false;
        result.applied_identity = issue.identity;
        return result;
    }
    const SessionResult committed = transaction.commit();
    if (!committed) {
        return reject(committed.error->message, issue.identity);
    }
    SafeFixResult result;
    result.ok = true;
    result.changed = true;
    result.applied_identity = issue.identity;
    return result;
}

}  // namespace marrow::editor
