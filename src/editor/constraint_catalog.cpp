#include "marrow/editor/constraint_catalog.hpp"

#include <algorithm>
#include <utility>

#include "marrow/editor/project.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace marrow::editor {

namespace {

/** @brief The index of a named constraint inside its family's root array. */
std::optional<std::size_t> find_family_index(
    const runtime::SkeletonData& skeleton,
    ConstraintKind family,
    std::string_view name) {
    const auto locate = [&](const auto& constraints) -> std::optional<std::size_t> {
        for (std::size_t index = 0U; index < constraints.size(); ++index) {
            if (constraints[index].name == name) {
                return index;
            }
        }
        return std::nullopt;
    };
    switch (family) {
    case ConstraintKind::Ik:
        return locate(skeleton.ik_constraints());
    case ConstraintKind::Path:
        return locate(skeleton.path_constraints());
    case ConstraintKind::Transform:
        return locate(skeleton.transform_constraints());
    case ConstraintKind::Physics:
        return locate(skeleton.physics_constraints());
    }
    return std::nullopt;
}

/** @brief The skin's index array for one constraint family. */
const std::vector<std::size_t>& family_indices(
    const runtime::SkinData& skin,
    ConstraintKind family) {
    switch (family) {
    case ConstraintKind::Ik:
        return skin.ik_constraint_indices;
    case ConstraintKind::Path:
        return skin.path_constraint_indices;
    case ConstraintKind::Transform:
        return skin.transform_constraint_indices;
    case ConstraintKind::Physics:
        return skin.physics_constraint_indices;
    }
    return skin.ik_constraint_indices;
}

} // namespace

std::string_view constraint_family_key(ConstraintKind family) noexcept {
    switch (family) {
    case ConstraintKind::Ik:
        return "ik";
    case ConstraintKind::Path:
        return "path";
    case ConstraintKind::Transform:
        return "transform";
    case ConstraintKind::Physics:
        return "physics";
    }
    return "ik";
}

std::optional<ConstraintKind> parse_constraint_family(std::string_view key) noexcept {
    if (key == "ik") return ConstraintKind::Ik;
    if (key == "path") return ConstraintKind::Path;
    if (key == "transform") return ConstraintKind::Transform;
    if (key == "physics") return ConstraintKind::Physics;
    return std::nullopt;
}

std::vector<std::string> constraint_affected_skins(
    const runtime::SkeletonData& skeleton,
    ConstraintKind family,
    std::string_view name) {
    std::vector<std::string> affected;
    const auto index = find_family_index(skeleton, family, name);
    if (!index.has_value()) {
        return affected;
    }
    for (const runtime::SkinData& skin : skeleton.skins()) {
        const std::vector<std::size_t>& references = family_indices(skin, family);
        if (std::find(references.begin(), references.end(), *index) !=
            references.end()) {
            affected.push_back(skin.name);
        }
    }
    return affected;
}

ConstraintCatalogResult apply_constraint_catalog_edit(
    EditorSession& session,
    const ConstraintCatalogEdit& edit,
    SelectionSet* selection,
    EditDescriptor descriptor) {
    ConstraintCatalogResult out;

    // 1. Guard. No transaction is opened for a request that cannot be applied.
    if (!session.has_project() || session.project() == nullptr ||
        session.base_skeleton_document() == nullptr ||
        session.runtime_data() == nullptr) {
        out.message = "No editor project is open.";
        return out;
    }
    if (session.transaction_active()) {
        out.message = "Finish the active edit before editing constraints.";
        return out;
    }
    if (edit.source.empty()) {
        out.message = "Constraint name must not be empty.";
        return out;
    }
    if (edit.kind == ConstraintCatalogEditKind::Delete && !edit.destination.empty()) {
        out.message = "A constraint delete carries no destination name.";
        return out;
    }

    // 2. Summarise. The skin arrays no longer name the constraint once step 3
    //    runs, so the summary must be read from the pre-mutation runtime.
    out.affected_skins = constraint_affected_skins(
        *session.runtime_data(), edit.family, edit.source);

    // 3. Transact.
    EditorSession::EditTransaction transaction = session.begin_edit(std::move(descriptor));
    if (!transaction) {
        out.message = transaction.error().has_value()
            ? transaction.error()->format()
            : std::string("Could not begin a constraint catalog edit.");
        out.affected_skins.clear();
        return out;
    }

    const ConstraintLifecycleResult applied =
        edit.kind == ConstraintCatalogEditKind::Rename
        ? rename_constraint(
              transaction.project(),
              *session.base_skeleton_document(),
              edit.family,
              edit.source,
              edit.destination)
        : delete_constraint(
              transaction.project(),
              *session.base_skeleton_document(),
              edit.family,
              edit.source);
    if (!applied.ok) {
        // Cancel explicitly rather than through the destructor, so the failure
        // path is visible in the code.
        transaction.cancel();
        out.message = applied.message;
        out.affected_skins.clear();
        return out;
    }

    // 4. Commit.
    const SessionResult committed = transaction.commit();
    if (!committed) {
        out.message = committed.error.has_value()
            ? committed.error->format()
            : std::string("The constraint catalog edit failed to commit.");
        out.affected_skins.clear();
        return out;
    }
    if (!committed.changed) {
        out.message = "The constraint catalog edit did not change the project.";
        out.affected_skins.clear();
        return out;
    }

    out.ok = true;
    out.changed = true;
    out.used_operation = applied.used_operation;
    out.changed_upsert = applied.changed_upsert;

    // 5. Cascade. Only a committed edit may move the caller's selection.
    if (selection != nullptr) {
        const SelectionItem from = ConstraintSelection{edit.family, edit.source};
        out.selection_changed = edit.kind == ConstraintCatalogEditKind::Rename
            ? selection->remap(
                  from,
                  SelectionItem{ConstraintSelection{edit.family, edit.destination}})
            : selection->remap(from, std::nullopt);
    }

    return out;
}

} // namespace marrow::editor
