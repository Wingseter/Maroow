#pragma once

#include "atomic_file_write.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>

namespace marrow::editor::detail {

enum class PreferencePlatform {
    MacOS,
    Linux,
    Windows,
};

struct PreferenceEnvironment {
    std::optional<std::string> marrow_config_home;
    std::optional<std::string> home;
    std::optional<std::string> xdg_config_home;
    std::optional<std::filesystem::path> roaming_app_data;
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

// `RenameCallback` and `set_preference_rename_callback_for_testing` now live in
// "atomic_file_write.hpp", which this header includes. The settings writer and
// the project writer share the same primitive and the same test seam.

} // namespace marrow::editor::detail
