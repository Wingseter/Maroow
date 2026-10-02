#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/selection.hpp"
#include "marrow/editor/session.hpp"

namespace marrow::editor {

/** @brief Which lifecycle verb a constraint catalog edit applies. */
enum class ConstraintCatalogEditKind {
    Rename,
    Delete,
};

/** @brief One lifecycle edit, identified by `(family, source)` and never by index. */
struct ConstraintCatalogEdit {
    ConstraintCatalogEditKind kind{ConstraintCatalogEditKind::Rename};
    ConstraintKind family{ConstraintKind::Ik};
    std::string source;       ///< Current name. Required for both verbs.
    std::string destination;  ///< New name for `Rename`; must be empty for `Delete`.
};

/**
 * @brief Outcome of one catalog edit, including everything a surface must report.
 *
 * `affected_skins` is captured from the pre-mutation runtime, because the skin
 * arrays are already rewritten by the time the edit commits.
 */
struct ConstraintCatalogResult {
    bool ok{false};
    bool changed{false};
    std::string message;                      ///< The primitive's message on rejection; empty on success.
    bool used_operation{false};               ///< An ordered `.marrow` record was appended (base-backed).
    bool changed_upsert{false};               ///< A `*_constraint_edits` entry was rewritten or erased.
    std::vector<std::string> affected_skins;  ///< Skins whose `<family>` array named the constraint.
    bool selection_changed{false};            ///< The supplied SelectionSet was remapped or pruned.
};

/**
 * @brief Applies one constraint rename or delete as a single undoable transaction.
 *
 * Wraps `rename_constraint()` / `delete_constraint()` in one `EditorSession`
 * transaction, so the runtime root arrays, every `skins[*].<family>` reference,
 * the project upserts and the ordered `constraint_edits.operations` records all
 * move together or not at all.
 *
 * The selection cascade runs only after a successful commit: a rejected or
 * cancelled edit must leave the caller's selection exactly as it was.
 *
 * @param session Open editor session; its project and base document are used.
 * @param edit The verb and its `(family, source)` identity.
 * @param selection Optional transient selection to cascade. Rename follows the
 *        constraint, delete removes it. Pass `nullptr` from surfaces that own
 *        no selection, such as the agent dispatcher.
 * @param descriptor History descriptor; the caller composes the human label.
 * @return The outcome. On any rejection nothing changed: the project's
 *         serialization is byte-identical, the history is unchanged, and
 *         `selection` is untouched.
 */
ConstraintCatalogResult apply_constraint_catalog_edit(
    EditorSession& session,
    const ConstraintCatalogEdit& edit,
    SelectionSet* selection,
    EditDescriptor descriptor);

/**
 * @brief Lists the skins whose `<family>` array names one constraint.
 *
 * Read from live parsed data via `SkinData::<family>_constraint_indices`, so it
 * cannot disagree with what materialization rewrites. The list is identical for
 * both verbs -- a rename rewrites exactly the skins a delete prunes -- so the
 * surfaces choose the wording rather than the membership.
 */
std::vector<std::string> constraint_affected_skins(
    const runtime::SkeletonData& skeleton,
    ConstraintKind family,
    std::string_view name);

/** @brief The `.marrow`/wire spelling of a family: `"ik"`, `"path"`, `"transform"`, `"physics"`. */
std::string_view constraint_family_key(ConstraintKind family) noexcept;
/** @brief Parses a wire family spelling; rejects anything else. */
std::optional<ConstraintKind> parse_constraint_family(std::string_view key) noexcept;

} // namespace marrow::editor
