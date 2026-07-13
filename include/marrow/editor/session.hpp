#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/project.hpp"
#include "marrow/runtime/animation_state.hpp"

namespace marrow::editor {

/**
 * @brief Classifies an authoring operation for history presentation and merging.
 */
enum class EditKind {
    Generic,
    MoveBone,
    AddKeyframe,
    RemoveKeyframe,
    EditProperty,
    PreviewComposition,
};

/**
 * @brief Describes which session-owned state an edit is expected to affect.
 */
enum class EditImpact : std::uint8_t {
    None = 0U,
    Project = 1U << 0U,
    Runtime = 1U << 1U,
    Preview = 1U << 2U,
};

constexpr EditImpact operator|(EditImpact left, EditImpact right) noexcept {
    return static_cast<EditImpact>(
        static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

constexpr EditImpact operator&(EditImpact left, EditImpact right) noexcept {
    return static_cast<EditImpact>(
        static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right));
}

constexpr bool has_edit_impact(EditImpact impacts, EditImpact impact) noexcept {
    return (impacts & impact) != EditImpact::None;
}

struct EditDescriptor {
    EditKind kind{EditKind::Generic};
    std::string label;
    std::string merge_key;
    bool allow_merge{false};
    EditImpact impacts{EditImpact::Project | EditImpact::Runtime | EditImpact::Preview};
};

using EditTransactionDescriptor = EditDescriptor;

/**
 * @brief One transient attachment choice applied only to the editor preview.
 */
struct PreviewAttachmentOverride {
    std::optional<std::size_t> skin_index;
    std::string attachment_name;
};

/**
 * @brief UI-independent playback and preview-composition state.
 */
struct PreviewState {
    std::string animation_name;
    double time_seconds{0.0};
    bool loop{true};
    bool playing{false};
    bool queue_enabled{false};
    std::string queued_animation_name;
    double queue_delay{0.0};
    std::optional<double> mix_duration;
    bool reverse{false};
    std::vector<std::string> skin_names;
    std::vector<std::optional<PreviewAttachmentOverride>> slot_overrides;
};

enum class SessionErrorCode {
    NoProject,
    TransactionAlreadyActive,
    InvalidTransaction,
    HistoryEmpty,
    RuntimeBuildFailed,
    PreviewUpdateFailed,
};

struct SessionError {
    SessionErrorCode code{SessionErrorCode::InvalidTransaction};
    std::string message;
    std::optional<runtime::json::LoadError> detail;

    /// @brief Formats the session error and any underlying runtime diagnostic.
    std::string format() const;
};

struct SessionResult {
    bool changed{false};
    std::optional<SessionError> error;

    explicit operator bool() const noexcept { return !error.has_value(); }
};

/**
 * @brief One animation-catalog mutation owned by EditorSession.
 *
 * Create uses `destination_animation`, delete uses `source_animation`, and
 * duplicate/rename use both names.
 */
enum class AnimationCatalogEditKind {
    Create,
    Duplicate,
    Rename,
    Delete,
};

struct AnimationCatalogEdit {
    AnimationCatalogEditKind kind{AnimationCatalogEditKind::Create};
    std::string source_animation;
    std::string destination_animation;
};

class EditorSessionShellBinding;

/**
 * @brief Owns UI-independent editor authoring, runtime preview, and history state.
 *
 * EditorSession is single-writer and move-only. An EditTransaction must not
 * outlive its session, and only one transaction may be active at a time.
 */
class EditorSession {
public:
    class EditTransaction;

    EditorSession();
    ~EditorSession();

    EditorSession(EditorSession&&) noexcept;
    EditorSession& operator=(EditorSession&&) noexcept;
    EditorSession(const EditorSession&) = delete;
    EditorSession& operator=(const EditorSession&) = delete;

    /**
     * @brief Opens a project and replaces the session atomically on success.
     * @return The attempted load result. A failed open leaves current state unchanged.
     */
    ProjectLoadResult open(const std::filesystem::path& path);
    /**
     * @brief Reloads the currently opened project from its source path.
     * @return The attempted load result. A failed reload leaves current state unchanged.
     */
    ProjectLoadResult reload();
    /**
     * @brief Saves the project, using its source path when `path` is empty.
     */
    ProjectSaveResult save(const std::filesystem::path& path = {});
    /**
     * @brief Exports the current authored runtime bundle.
     */
    ProjectExportResult export_runtime(const ProjectExportOptions& options = {}) const;

    bool has_project() const noexcept;
    const ProjectData* project() const noexcept;
    const runtime::json::Document* base_skeleton_document() const noexcept;
    const runtime::SkeletonData* runtime_data() const noexcept;
    const std::vector<std::shared_ptr<const runtime::AtlasData>>& atlas_data() const noexcept;

    const PreviewState& preview_state() const noexcept;
    const runtime::Skeleton* preview_skeleton() const noexcept;
    const runtime::AnimationState* preview_animation_state() const noexcept;
    const std::vector<runtime::AnimationEvent>& preview_events() const noexcept;
    runtime::RootMotionDelta preview_root_motion_delta() const noexcept;
    runtime::RootMotionDelta preview_root_motion_total() const noexcept;

    bool select_animation(std::string_view animation_name, bool reset_time = true);
    /** @brief Clears animation playback and shows the immutable setup pose. */
    bool select_setup_pose();
    bool seek(double time_seconds);
    bool advance(double delta_seconds);
    void set_playing(bool playing) noexcept;
    bool set_loop(bool loop);
    bool set_reverse(bool reverse);
    bool set_queue(
        std::string_view animation_name,
        double delay_seconds,
        std::optional<double> mix_duration = std::nullopt);
    bool clear_queue();

    /**
     * @brief Records an undoable transient skin-composition change.
     */
    SessionResult set_preview_skins(
        std::vector<std::string> skin_names,
        EditDescriptor descriptor = {
            EditKind::PreviewComposition,
            "Change preview skins",
            "preview-skins",
            true,
            EditImpact::Preview});
    /**
     * @brief Records an undoable transient slot-attachment override.
     */
    SessionResult set_preview_attachment(
        std::size_t slot_index,
        std::optional<std::size_t> skin_index,
        std::string attachment_name,
        EditDescriptor descriptor = {
            EditKind::PreviewComposition,
            "Change preview attachment",
            "preview-attachment",
            true,
            EditImpact::Preview});
    SessionResult reset_preview_attachment(
        std::size_t slot_index,
        EditDescriptor descriptor = {
            EditKind::PreviewComposition,
            "Reset preview attachment",
            "preview-attachment",
            true,
            EditImpact::Preview});

    /**
     * @brief Applies one catalog edit and its preview selection/queue remap atomically.
     *
     * Rename remaps matching current and queued animation references. Delete
     * selects the project's replacement animation when necessary and removes a
     * queue that references the deleted animation. The preview state is stored
     * in the same undo/redo snapshot as the project mutation.
     */
    SessionResult edit_animation_catalog(
        AnimationCatalogEdit edit,
        EditDescriptor descriptor = {
            EditKind::EditProperty,
            "Edit animation catalog",
            "animation-catalog",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});

    EditTransaction begin_edit(EditDescriptor descriptor);
    bool transaction_active() const noexcept;

    SessionResult undo();
    SessionResult redo();
    bool can_undo() const noexcept;
    bool can_redo() const noexcept;
    std::size_t undo_count() const noexcept;
    std::size_t redo_count() const noexcept;
    std::string_view undo_label() const noexcept;
    std::string_view redo_label() const noexcept;
    void clear_history() noexcept;

    bool dirty() const noexcept;
    std::uint64_t project_revision() const noexcept;
    std::uint64_t runtime_revision() const noexcept;
    std::uint64_t preview_revision() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    ProjectLoadResult& mutable_load_result();
    runtime::Skeleton* mutable_preview_skeleton() noexcept;
    runtime::AnimationState* mutable_preview_animation_state() noexcept;
    bool sync_preview_state(const PreviewState& state);
    SessionResult commit_external_edit(
        const ProjectData& before_project,
        const PreviewState& before_preview,
        EditDescriptor descriptor,
        bool runtime_is_current);
    SessionResult rebuild_runtime_without_history();

    friend class EditorSessionShellBinding;
};

/**
 * @brief A non-nestable RAII edit scoped to one EditorSession.
 *
 * Destroying an uncommitted transaction cancels it and restores its project
 * and preview snapshots. Runtime validation and rebinding happen at commit.
 */
class EditorSession::EditTransaction {
public:
    EditTransaction() noexcept;
    ~EditTransaction();

    EditTransaction(EditTransaction&& other) noexcept;
    EditTransaction& operator=(EditTransaction&& other) noexcept;
    EditTransaction(const EditTransaction&) = delete;
    EditTransaction& operator=(const EditTransaction&) = delete;

    explicit operator bool() const noexcept;
    const std::optional<SessionError>& error() const noexcept;
    ProjectData* project() noexcept;
    const ProjectData* project() const noexcept;

    bool set_preview_skins(std::vector<std::string> skin_names);
    bool set_preview_attachment(
        std::size_t slot_index,
        std::optional<std::size_t> skin_index,
        std::string attachment_name);
    bool reset_preview_attachment(std::size_t slot_index);

    /**
     * @brief Rebuilds runtime data and refreshes the preview without closing the edit.
     *
     * This supports live authoring gestures. Repeated calls replace the
     * transaction's transient runtime while commit still creates one history
     * entry. Cancel restores the project, runtime, and preview captured when
     * the transaction began.
     */
    SessionResult refresh_runtime();

    SessionResult commit();
    void cancel() noexcept;

private:
    EditTransaction(
        Impl* impl,
        std::uint64_t transaction_id,
        std::optional<SessionError> error = std::nullopt) noexcept;

    Impl* impl_{nullptr};
    std::uint64_t transaction_id_{0U};
    std::optional<SessionError> error_;

    friend class EditorSession;
};

} // namespace marrow::editor
