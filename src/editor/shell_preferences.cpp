#include "shell_preferences.hpp"

#include "marrow/editor/recent_projects.hpp"

#include <chrono>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>

#include "marrow/editor/authoring.hpp"

#if defined(_WIN32)
#include <stdlib.h>
#endif

namespace marrow::editor::shell {
namespace {

namespace fs = std::filesystem;

std::optional<std::string> environment_value(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::nullopt : std::optional<std::string>(value);
}

bool assign_environment_value(
    const char* name,
    const std::optional<std::string>& value) {
#if defined(_WIN32)
    return ::_putenv_s(name, value.has_value() ? value->c_str() : "") == 0;
#else
    return value.has_value() ? ::setenv(name, value->c_str(), 1) == 0
                             : ::unsetenv(name) == 0;
#endif
}

} // namespace

ScopedPreferenceIsolation::ScopedPreferenceIsolation(std::string_view purpose)
    : previous_(environment_value("MARROW_CONFIG_HOME")) {
    const auto stamp =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path parent = fs::temp_directory_path();
    for (int attempt = 0; attempt < 100; ++attempt) {
        const fs::path candidate = parent /
            ("marrow-shell-config-" + std::string(purpose) + "-" +
             std::to_string(stamp) + "-" + std::to_string(attempt));
        std::error_code error;
        if (fs::create_directory(candidate, error)) {
            path_ = candidate;
            break;
        }
    }
    if (path_.empty()) return;
    installed_ = assign_environment_value("MARROW_CONFIG_HOME", path_.string());
}

ScopedPreferenceIsolation::~ScopedPreferenceIsolation() {
    if (installed_) {
        // Best effort: a failed restore must not throw out of a destructor.
        (void)assign_environment_value("MARROW_CONFIG_HOME", previous_);
    }
    if (!path_.empty()) {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
}

fs::path ScopedPreferenceIsolation::settings_path() const {
    return path_.empty() ? fs::path{} : path_ / "editor-settings.json";
}


void load_shell_preferences(ShellState* state) {
    if (state == nullptr) return;
    // Constructed here rather than held by ShellState so a smoke process can
    // install its MARROW_CONFIG_HOME isolation before any path is resolved.
    const marrow::editor::PreferenceStore store;
    const marrow::editor::PreferenceLoadResult result = store.load();
    state->preferences = result.preferences;
    // MAR-183: normalize IN MEMORY only -- canonicalize, drop empties, dedup
    // keeping the first occurrence, cap at the bound. LOADING NEVER WRITES: a
    // settings file the user is mid-way through hand-editing survives untouched,
    // and an entry whose volume is merely unmounted is never destroyed. The
    // returned bool is discarded deliberately, because there is no write to
    // skip here. An oversized on-disk list stays oversized until the next real
    // mutation. Design 2.5.
    (void)marrow::editor::normalize_recent_paths(
        &state->preferences.recent_projects);
    state->preference_status = result.status;
    state->preference_path = result.path;
    state->preference_diagnostic = result.diagnostic;
    // A first run is normal and silent; anything else is reported once and the
    // file is left exactly as found. The shell never repairs it on load.
    if (result.status != marrow::editor::PreferenceLoadStatus::Loaded &&
        result.status != marrow::editor::PreferenceLoadStatus::FirstRun) {
        state->status_message =
            "Editor settings could not be read; using the Linear default curve";
    }
}

bool set_shell_default_curve(ShellState* state, marrow::editor::CurvePreset preset) {
    if (state == nullptr) return false;
    if (state->preferences.default_curve == preset) return true;
    // Mutating the loaded value rather than constructing a fresh one is what
    // preserves recent_projects and the preserved on-disk root.
    state->preferences.default_curve = preset;
    const marrow::editor::PreferenceStore store;
    const marrow::editor::PreferenceSaveResult saved = store.save(state->preferences);
    if (!saved) {
        state->error_message = "Failed to store the default curve: " + saved.error;
        return false;
    }
    state->preference_path = saved.path;
    state->status_message = "Default curve set to " +
        std::string(marrow::editor::curve_preset_definition(preset).display_name);
    return true;
}

} // namespace marrow::editor::shell
