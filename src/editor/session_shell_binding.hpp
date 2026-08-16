#pragma once

#include <utility>

#include "marrow/editor/session.hpp"

namespace marrow::editor {

/**
 * @brief Source-private binding between feature-owned shell modules and EditorSession.
 *
 * Authoring and agent code mutates through EditTransaction. The shell additionally
 * needs synchronized mutable preview aliases and atomic live-preview adoption for
 * its ImGui gesture lifecycle. This type stays in the editor source tree and is
 * not part of the installed authoring API.
 */
class EditorSessionShellBinding {
public:
    static ProjectLoadResult& load_result(EditorSession& session) {
        return session.mutable_load_result();
    }

    static runtime::Skeleton* preview_skeleton(EditorSession& session) noexcept {
        return session.mutable_preview_skeleton();
    }

    static runtime::AnimationState* preview_animation_state(
        EditorSession& session) noexcept {
        return session.mutable_preview_animation_state();
    }

    /** Synchronizes shell playback/composition fields without adding history. */
    static bool sync_preview_state(
        EditorSession& session,
        const PreviewState& state) {
        return session.sync_preview_state(state);
    }

    /**
     * @brief Adopts an already-applied, live-previewed shell edit into session history.
     *
     * The session's current project and preview are treated as the after-state.
     * Failure restores the supplied before-state and creates no history entry.
     */
    static SessionResult commit_external_edit(
        EditorSession& session,
        const ProjectData& before_project,
        const PreviewState& before_preview,
        EditDescriptor descriptor,
        bool runtime_is_current = false) {
        return session.commit_external_edit(
            before_project,
            before_preview,
            std::move(descriptor),
            runtime_is_current);
    }

    /** Rebuilds and rebinds preview data after source-asset reload or rollback. */
    static SessionResult rebuild_runtime_without_history(EditorSession& session) {
        return session.rebuild_runtime_without_history();
    }
};

} // namespace marrow::editor
