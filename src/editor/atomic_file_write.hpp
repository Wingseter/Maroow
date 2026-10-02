#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>

namespace marrow::editor::detail {

using RenameCallback = std::function<std::error_code(
    const std::filesystem::path& source,
    const std::filesystem::path& destination)>;

/**
 * @brief Installs a process-local atomic-rename failure seam for focused tests.
 *
 * Passing an empty callback restores the production platform replacement
 * operation (`rename` on POSIX and `MoveFileExW` on Windows).
 * This hook intentionally lives in a private source header.
 *
 * HAZARD: the seam is process-global and is now shared by the settings writer and
 * the project writer. `marrow_editor_shell` exercises both. Scope every
 * installation with RAII and perform no unrelated atomic write inside that scope.
 */
void set_preference_rename_callback_for_testing(RenameCallback callback);

/**
 * @brief Writes @p text to @p destination through a temporary file and an atomic rename.
 *
 * Creates the temporary in the destination's own directory so the rename never
 * crosses a filesystem boundary. Every write, flush and close error is checked
 * explicitly. Every handled failure removes the temporary and leaves
 * @p destination byte-for-byte unchanged.
 *
 * Does NOT fsync the file or its parent directory: durability across power loss
 * is a deliberate non-goal (MAR-180 design section 3.2). A crash between the
 * temporary's creation and the rename can leave one orphan `*.tmp.*` file beside
 * the destination; only handled failures clean up.
 *
 * @param subject Noun used in the error messages ("settings", "project").
 * @return Empty on success; otherwise a message naming the failed step and cause.
 */
std::string write_file_atomically(
    const std::filesystem::path& destination,
    std::string_view text,
    std::string_view subject);

} // namespace marrow::editor::detail
