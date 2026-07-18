#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "marrow/runtime/json.hpp"

namespace marrow::editor {

/** @brief Current wire version of `editor-settings.json`. */
inline constexpr int kEditorSettingsVersion = 1;

/** @brief Stable tokens stored as the user's default curve preset. */
enum class CurvePreset {
    Linear,
    Stepped,
    Ease,
    EaseIn,
    EaseOut,
    EaseInOut,
};

/**
 * @brief UI-independent user preferences shared by editor features.
 *
 * Recent paths remain in their authored order and spelling. Canonicalization,
 * de-duplication, existence checks, MRU promotion, and list bounds belong to
 * the Recent Projects feature rather than this storage layer.
 */
struct EditorPreferences {
    CurvePreset default_curve{CurvePreset::Linear};
    std::vector<std::filesystem::path> recent_projects;

    // Supported-version root used to preserve unknown additive fields.
    runtime::json::Value preserved_root{runtime::json::Value::Object{}, {}};
};

enum class PreferenceLoadStatus {
    FirstRun,
    Loaded,
    LoadedWithDefaults,
    Malformed,
    UnsupportedVersion,
    IoError,
};

/** @brief Preference load outcome; `preferences` always contains usable values. */
struct PreferenceLoadResult {
    EditorPreferences preferences;
    PreferenceLoadStatus status{PreferenceLoadStatus::FirstRun};
    std::filesystem::path path;
    std::string diagnostic;
};

/** @brief Preference save outcome with the attempted path and failure cause. */
struct PreferenceSaveResult {
    std::filesystem::path path;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

/**
 * @brief Loads and atomically saves the user-local `editor-settings.json`.
 *
 * This service does not reference ProjectData, EditorSession, shell state, or
 * operation registries. The default constructor resolves the production path
 * from the process environment. The explicit constructor uses exactly the
 * supplied settings-file path and is intended for isolated tests and tools.
 */
class PreferenceStore {
public:
    PreferenceStore();
    explicit PreferenceStore(std::filesystem::path settings_path);

    const std::filesystem::path& settings_path() const noexcept;
    PreferenceLoadResult load() const;
    PreferenceSaveResult save(const EditorPreferences& preferences) const;

private:
    std::filesystem::path settings_path_;
    std::string path_error_;
};

} // namespace marrow::editor
