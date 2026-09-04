#pragma once

#include <algorithm>
#include <string_view>
#include <vector>

namespace marrow::editor {

/**
 * @brief The first constraint in @p constraints named @p name, or nullptr.
 *
 * Lives here rather than in `shell_constraints.hpp` because both the UI-free
 * `marrow_editor` library and the shell need it. `agent_handlers_constraints.cpp`
 * was the ONE `#include "shell_*"` in the whole library -- taken for exactly this
 * template -- and this header is what removed it; `docs/root1/refector.md` makes
 * the UI-free boundary a contract, so do not put it back.
 *
 * Not in the PUBLIC `include/marrow/editor/constraint_catalog.hpp`: a generic
 * vector scan used by nothing outside `src/editor` does not belong on the
 * installed authoring surface.
 *
 * Shell callers stay unqualified: they sit in `marrow::editor::shell`, so
 * enclosing-namespace lookup finds this without a `using`.
 */
template <typename ConstraintType>
const ConstraintType* find_named_constraint(
    const std::vector<ConstraintType>& constraints,
    std::string_view name) {
    const auto iterator = std::find_if(
        constraints.begin(),
        constraints.end(),
        [&](const ConstraintType& constraint) {
            return constraint.name == name;
        });
    return iterator == constraints.end() ? nullptr : &(*iterator);
}

} // namespace marrow::editor
