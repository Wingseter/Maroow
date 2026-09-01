#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "marrow/editor/project.hpp"
#include "marrow/editor/psd_import.hpp"

namespace marrow::editor {

/**
 * @brief What a reimport would do to one PSD layer.
 *
 * A layer is `Updated` when its identity appears in BOTH the project's stored
 * provenance and the candidate PSD, `Added` when it appears only in the candidate,
 * and `Missing` when it appears only in the provenance. Nothing else is inferred:
 * a renamed layer is an `Added` and a `Missing`, never an `Updated`.
 */
enum class PsdLayerChangeKind { Added, Updated, Missing };

/** @brief One layer's planned outcome. */
struct PsdPlannedLayer {
    std::vector<std::string> group_path;
    std::string layer_name;
    /// @brief Escaped and sortable. `\` becomes `\\` and `|` becomes `\|`.
    std::string identity;
    PsdLayerChangeKind change{PsdLayerChangeKind::Added};

    // What the project has today. Empty for `Added`.
    std::string current_slot_name;
    std::string current_attachment_name;
    std::string current_bone_name;
    std::string current_image_file;

    // What committing this plan would produce. Empty for `Missing`.
    std::string proposed_slot_name;
    std::string proposed_attachment_name;
    std::string proposed_bone_name;
    std::string proposed_image_file;

    /// @brief A `Missing` layer is preserved unless a later story opts out.
    bool preserve{true};
};

struct PsdReimportPlanError {
    std::filesystem::path path;
    std::string message;

    /// @brief Formats the planning error as a human-readable message.
    /// @return A formatted string naming the candidate path and the failure.
    std::string format() const;
};

/**
 * @brief A deterministic added/updated/missing diff for a candidate PSD.
 *
 * Every path under `staged_*` is inside `staging_root`, which the caller supplies
 * and the planner never escapes. Nothing outside `staging_root` is written.
 */
struct PsdReimportPlan {
    std::filesystem::path source_path;
    std::filesystem::path staging_root;
    std::filesystem::path staged_skeleton_path;
    std::filesystem::path staged_atlas_path;
    std::filesystem::path staged_layers_directory;
    /// @brief Lexicographic by `identity`, ascending, over the union.
    std::vector<PsdPlannedLayer> layers;
    std::size_t added_count{0};
    std::size_t updated_count{0};
    std::size_t missing_count{0};
    std::optional<PsdReimportPlanError> error;

    /// @brief Reports whether planning completed without an error.
    /// @return `true` when no error payload is present; otherwise `false`.
    explicit operator bool() const {
        return !error.has_value();
    }
};

/**
 * @brief Where a candidate PSD is, and where the planner may write.
 *
 * The caller supplies a staging ROOT and never an output path. This is
 * structural, not stylistic: the importer's `write_imported_layers` calls
 * `remove_all` on whatever layer directory it is handed, so an API that cannot be
 * handed a directory cannot be handed the wrong one. An empty `staging_root`, or
 * one that already exists and is non-empty, is refused -- a planner that inherits
 * somebody else's files cannot tell its own output from theirs.
 */
struct PsdReimportPlanOptions {
    std::filesystem::path psd_path;
    std::filesystem::path staging_root;
};

/**
 * @brief Plans what reimporting `options.psd_path` would do to `project`.
 *
 * Takes `const ProjectData&`, so it cannot modify project data, history or the
 * session -- that half of AC3 is true by signature. The half a signature cannot
 * prove is the file system, and every write is confined to `options.staging_root`.
 *
 * @param project Project whose stored provenance the candidate is compared against.
 * @param options Candidate PSD path and the staging root every write goes under.
 * @return The classified plan, or an error describing why planning failed.
 */
PsdReimportPlan plan_psd_reimport(
    const ProjectData& project,
    const PsdReimportPlanOptions& options);

/**
 * @brief Converts a completed import into storable provenance.
 *
 * `image_file` is taken as `filename()` of the extracted image, which is what
 * makes the bare-name invariant true by construction on every write path in this
 * story rather than merely checked on the way in.
 *
 * @param result A successful import whose layers carry their runtime targets.
 * @param project_path Project file the stored paths are made relative to.
 * @param psd_path Source PSD the provenance records.
 * @return Provenance ready to assign to `ProjectMetadata::import_sources`.
 */
PsdImportProvenance make_psd_provenance(
    const PsdImportResult& result,
    const std::filesystem::path& project_path,
    const std::filesystem::path& psd_path);

} // namespace marrow::editor
