#include "marrow/editor/psd_reimport_plan.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <system_error>
#include <utility>

namespace marrow::editor {

namespace {

/**
 * @brief Escapes one identity token, matching MAR-186's scheme exactly.
 *
 * Escaping here is NOT defensive. `AGENTS.md` records that a collision needs a
 * token whose neighbours are unconstrained, and that MAR-186 could construct one
 * in exactly one family because everywhere else the arity was fixed or the tokens
 * were closed enums. Here EVERY token is a free Photoshop string and the arity is
 * variable -- a layer at depth 1 and a layer at depth 2 build 2-token and 3-token
 * identities from the same alphabet. Unescaped, group `["a|b"]` + layer `c` and
 * group `["a"]` + layer `b|c` are the same string, and a map-keyed comparison
 * would silently DELETE one of them.
 */
std::string escape_identity_token(const std::string& token) {
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

/** @brief `<esc(group[0])>|…|<esc(layer_name)>`. */
std::string build_identity(
    const std::vector<std::string>& group_path,
    const std::string& layer_name) {
    std::string identity;
    for (const std::string& segment : group_path) {
        identity += escape_identity_token(segment);
        identity.push_back('|');
    }
    identity += escape_identity_token(layer_name);
    return identity;
}

PsdReimportPlan make_error_plan(
    const PsdReimportPlanOptions& options,
    std::string message) {
    PsdReimportPlan plan;
    plan.source_path = options.psd_path;
    plan.staging_root = options.staging_root;
    plan.error = PsdReimportPlanError{options.psd_path, std::move(message)};
    return plan;
}

/** @brief True when `root` is an ancestor of `candidate`, or is it. */
bool is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    const std::filesystem::path normal_root = root.lexically_normal();
    const std::filesystem::path normal_candidate = candidate.lexically_normal();
    auto root_part = normal_root.begin();
    auto candidate_part = normal_candidate.begin();
    for (; root_part != normal_root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == normal_candidate.end() || *root_part != *candidate_part) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string PsdReimportPlanError::format() const {
    if (path.empty()) {
        return "PSD reimport planning failed: " + message;
    }
    return "PSD reimport planning failed for '" + path.string() + "': " + message;
}

PsdImportProvenance make_psd_provenance(
    const PsdImportResult& result,
    const std::filesystem::path& project_path,
    const std::filesystem::path& psd_path) {
    PsdImportProvenance provenance;
    provenance.source_path = project_relative_path(project_path, psd_path);
    provenance.layers_directory =
        project_relative_path(project_path, result.extracted_layers_directory);
    provenance.layers.reserve(result.layers.size());
    for (const PsdImportedLayer& layer : result.layers) {
        PsdLayerProvenance stored;
        stored.group_path = layer.group_path;
        stored.layer_name = layer.name;
        stored.slot_name = layer.slot_name;
        stored.attachment_name = layer.attachment_name;
        stored.bone_name = layer.bone_name;
        // `filename()` is what makes the bare-name invariant true BY CONSTRUCTION
        // on every write path in this story, rather than merely checked on the way
        // in: an extracted image is always a direct child of the layer directory.
        stored.image_file = layer.extracted_image_path.filename().generic_string();
        provenance.layers.push_back(std::move(stored));
    }
    return provenance;
}

PsdReimportPlan plan_psd_reimport(
    const ProjectData& project,
    const PsdReimportPlanOptions& options) {
    if (options.psd_path.empty()) {
        return make_error_plan(options, "candidate PSD path must not be empty");
    }
    if (options.staging_root.empty()) {
        return make_error_plan(options, "staging root must not be empty");
    }

    // A planner that inherits somebody else's files cannot tell its own output
    // from theirs, and the importer's `remove_all` would then delete them.
    std::error_code status_error;
    if (std::filesystem::exists(options.staging_root, status_error)) {
        if (!std::filesystem::is_directory(options.staging_root, status_error)) {
            return make_error_plan(options, "staging root must be a directory");
        }
        const std::filesystem::directory_iterator entries(options.staging_root, status_error);
        if (status_error) {
            return make_error_plan(options, "staging root could not be read");
        }
        if (entries != std::filesystem::directory_iterator{}) {
            return make_error_plan(options, "staging root must be empty or absent");
        }
    }
    std::filesystem::create_directories(options.staging_root, status_error);
    if (status_error) {
        return make_error_plan(options, "staging root could not be created");
    }

    PsdReimportPlan plan;
    plan.source_path = options.psd_path;
    plan.staging_root = options.staging_root;
    plan.staged_skeleton_path = options.staging_root / "staged.mskl";
    plan.staged_atlas_path = options.staging_root / "staged.matl";
    plan.staged_layers_directory = options.staging_root / "staged_layers";

    PsdImportOptions import_options;
    import_options.psd_path = options.psd_path;
    import_options.skeleton_output_path = plan.staged_skeleton_path;
    import_options.atlas_output_path = plan.staged_atlas_path;
    import_options.extracted_layers_directory = plan.staged_layers_directory;
    import_options.atlas_name = "staged";
    // Set EXPLICITLY. Left unset, `effective_existing_skeleton_path` falls back to
    // the staging skeleton -- which does not exist -- and the staged merge would
    // then differ from what a real reimport produces, silently changing every
    // proposed target.
    import_options.existing_skeleton_path = project.resolved_skeleton_path();

    const PsdImportResult imported = import_psd_to_runtime_bundle(import_options);
    if (!imported) {
        return make_error_plan(options, imported.error->message);
    }

    // Candidate identities, refused rather than paired when they collide. Given
    // two entries sharing an identity, ANY pairing is an inference -- positional
    // pairing most of all, since record order is exactly what a user reorders in
    // Photoshop without meaning anything by it. A story whose criterion is "never
    // infers renames" cannot resolve duplicates by guessing.
    std::map<std::string, const PsdImportedLayer*> candidates;
    for (const PsdImportedLayer& layer : imported.layers) {
        const std::string identity = build_identity(layer.group_path, layer.name);
        if (!candidates.emplace(identity, &layer).second) {
            return make_error_plan(
                options,
                "psd layers must have unique group and name identities (" + identity +
                    ")");
        }
    }

    std::map<std::string, const PsdLayerProvenance*> stored;
    if (project.editor_metadata.import_sources.has_value() &&
        project.editor_metadata.import_sources->psd.has_value()) {
        for (const PsdLayerProvenance& layer :
             project.editor_metadata.import_sources->psd->layers) {
            stored.emplace(build_identity(layer.group_path, layer.layer_name), &layer);
        }
    }

    // The union, in ascending identity order. `std::map` is already sorted, so
    // the ordering is a property of the container rather than of a sort call a
    // mutation could remove.
    std::set<std::string> identities;
    for (const auto& entry : candidates) {
        identities.insert(entry.first);
    }
    for (const auto& entry : stored) {
        identities.insert(entry.first);
    }

    plan.layers.reserve(identities.size());
    for (const std::string& identity : identities) {
        const auto candidate = candidates.find(identity);
        const auto existing = stored.find(identity);

        PsdPlannedLayer planned;
        planned.identity = identity;
        if (candidate != candidates.end()) {
            planned.group_path = candidate->second->group_path;
            planned.layer_name = candidate->second->name;
            planned.proposed_slot_name = candidate->second->slot_name;
            planned.proposed_attachment_name = candidate->second->attachment_name;
            planned.proposed_bone_name = candidate->second->bone_name;
            planned.proposed_image_file =
                candidate->second->extracted_image_path.filename().generic_string();
        } else {
            planned.group_path = existing->second->group_path;
            planned.layer_name = existing->second->layer_name;
        }
        if (existing != stored.end()) {
            planned.current_slot_name = existing->second->slot_name;
            planned.current_attachment_name = existing->second->attachment_name;
            planned.current_bone_name = existing->second->bone_name;
            planned.current_image_file = existing->second->image_file;
        }

        if (candidate != candidates.end() && existing != stored.end()) {
            planned.change = PsdLayerChangeKind::Updated;
        } else if (candidate != candidates.end()) {
            planned.change = PsdLayerChangeKind::Added;
        } else {
            planned.change = PsdLayerChangeKind::Missing;
        }
        plan.layers.push_back(std::move(planned));
    }

    // Counts are DERIVED from the vector, never accumulated in a parallel loop: a
    // count a mutation cannot change is not an assertion.
    for (const PsdPlannedLayer& layer : plan.layers) {
        switch (layer.change) {
            case PsdLayerChangeKind::Added:
                ++plan.added_count;
                break;
            case PsdLayerChangeKind::Updated:
                ++plan.updated_count;
                break;
            case PsdLayerChangeKind::Missing:
                ++plan.missing_count;
                break;
        }
    }

    // Every staged path is under the caller's root, checked rather than assumed.
    if (!is_within(plan.staging_root, plan.staged_skeleton_path) ||
        !is_within(plan.staging_root, plan.staged_atlas_path) ||
        !is_within(plan.staging_root, plan.staged_layers_directory)) {
        return make_error_plan(options, "staged outputs escaped the staging root");
    }
    return plan;
}

} // namespace marrow::editor
