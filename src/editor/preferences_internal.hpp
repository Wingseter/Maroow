#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>

namespace marrow::editor::detail {

enum class PreferencePlatform {
    MacOS,
    Linux,
};

struct PreferenceEnvironment {
    std::optional<std::string> marrow_config_home;
    std::optional<std::string> home;
    std::optional<std::string> xdg_config_home;
};

struct PreferencePathResult {
    std::filesystem::path settings_path;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

/** @brief Pure production-path resolver used by the cross-platform tests. */
PreferencePathResult resolve_preference_settings_path(
    PreferencePlatform platform,
    const PreferenceEnvironment& environment);

using RenameCallback = std::function<std::error_code(
    const std::filesystem::path& source,
    const std::filesystem::path& destination)>;

/**
 * @brief Installs a process-local atomic-rename failure seam for focused tests.
 *
 * Passing an empty callback restores the production POSIX rename operation.
 * This hook intentionally lives in a private source header.
 */
void set_preference_rename_callback_for_testing(RenameCallback callback);

} // namespace marrow::editor::detail
