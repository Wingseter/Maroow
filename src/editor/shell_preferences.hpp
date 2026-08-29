#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "shell_state.hpp"
#include "marrow/editor/preferences.hpp"

namespace marrow::editor::shell {

/**
 * @brief Loads the user-local editor settings once, at shell startup.
 *
 * Never fails: every missing, malformed, unsupported, or unreadable case leaves
 * `state->preferences.default_curve` at `CurvePreset::Linear`. Nothing is
 * written, so a first run creates no file and a settings file the user is
 * mid-way through hand-editing is preserved until they explicitly change the
 * default. This function does not reference EditorSession, ProjectData, or the
 * runtime, which is what makes "never rewrites existing curves" structural.
 */
void load_shell_preferences(ShellState* state);

/**
 * @brief Records a new default curve and atomically persists the settings file.
 *
 * Preserves `recent_projects` and every unknown additive field by saving the
 * `EditorPreferences` that were loaded rather than a fresh value. Never opens a
 * transaction and never dirties the project. Selecting the value already stored
 * performs no write. Returns false and sets `state->error_message` when the
 * write fails, leaving the in-memory default changed for this session.
 */
bool set_shell_default_curve(ShellState* state, marrow::editor::CurvePreset preset);

/**
 * @brief Points `MARROW_CONFIG_HOME` at a private temporary directory.
 *
 * `PreferenceStore`'s default constructor resolves the real user settings path,
 * so any smoke process that loads shell preferences would otherwise read — and
 * a save-path scenario would write — the developer's own
 * `editor-settings.json`. Every headless smoke installs one of these before it
 * constructs a `ShellState`, and restores the previous value and removes the
 * directory on every return path.
 *
 * `resolve_preference_settings_path()` honours `MARROW_CONFIG_HOME` first on
 * every platform, so this reuses the existing, already-tested override rather
 * than adding a second injection seam to `PreferenceStore`.
 */
class ScopedPreferenceIsolation {
public:
    explicit ScopedPreferenceIsolation(std::string_view purpose);
    ~ScopedPreferenceIsolation();

    ScopedPreferenceIsolation(const ScopedPreferenceIsolation&) = delete;
    ScopedPreferenceIsolation& operator=(const ScopedPreferenceIsolation&) = delete;
    ScopedPreferenceIsolation(ScopedPreferenceIsolation&&) = delete;
    ScopedPreferenceIsolation& operator=(ScopedPreferenceIsolation&&) = delete;

    /** @brief The isolated config home, or an empty path when setup failed. */
    const std::filesystem::path& path() const noexcept { return path_; }

    /** @brief The settings file this isolation would resolve to, if any. */
    std::filesystem::path settings_path() const;

    bool installed() const noexcept { return installed_; }

private:
    std::filesystem::path path_;
    std::optional<std::string> previous_;
    bool installed_{false};
};

} // namespace marrow::editor::shell
