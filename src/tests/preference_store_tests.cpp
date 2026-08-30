#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#endif

#include "marrow/editor/authoring.hpp"
#include "marrow/editor/preferences.hpp"
#include "marrow/editor/recent_projects.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/editor/session.hpp"
#include "../editor/preferences_internal.hpp"

namespace {

namespace fs = std::filesystem;
namespace json = marrow::runtime::json;

using marrow::editor::CurvePreset;
using marrow::editor::EditorPreferences;
using marrow::editor::PreferenceLoadStatus;
using marrow::editor::PreferenceStore;
using marrow::editor::curve_preset_definition;
using marrow::editor::curve_preset_from_token;
using marrow::editor::curve_preset_interpolation;
using marrow::editor::curve_preset_of;
using marrow::editor::kCurvePresets;
using AnimationScalar = marrow::runtime::AnimationScalar;
using InterpolationKind = marrow::runtime::InterpolationKind;

class TestSuite {
public:
    template <typename Function>
    void run(std::string name, Function&& function) {
        current_case_ = std::move(name);
        const int failures_before = failures_;
        try {
            std::forward<Function>(function)();
        } catch (const std::exception& exception) {
            fail(std::string("unexpected exception: ") + exception.what());
        } catch (...) {
            fail("unexpected non-standard exception");
        }

        if (failures_ == failures_before) {
            std::cout << "PASS: " << current_case_ << '\n';
        } else {
            std::cout << "FAIL: " << current_case_ << '\n';
        }
        ++case_count_;
    }

    bool expect(bool condition, std::string_view message) {
        if (!condition) {
            fail(message);
        }
        return condition;
    }

    int finish() const {
        if (failures_ == 0) {
            std::cout << "PreferenceStore: " << case_count_
                      << " cases passed\n";
            return 0;
        }
        std::cerr << "PreferenceStore: " << failures_ << " failure(s) across "
                  << case_count_ << " cases\n";
        return 1;
    }

private:
    void fail(std::string_view message) {
        ++failures_;
        std::cerr << current_case_ << ": " << message << '\n';
    }

    std::string current_case_;
    int failures_{0};
    int case_count_{0};
};

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::string_view purpose) {
        const auto now = std::chrono::high_resolution_clock::now()
                             .time_since_epoch()
                             .count();
        const fs::path parent = fs::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            path_ = parent /
                ("marrow-preference-" + std::string(purpose) + "-" +
                 std::to_string(now) + "-" + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(path_, error)) {
                return;
            }
            if (error && error != std::errc::file_exists) {
                throw std::runtime_error(
                    "failed to create test directory: " + error.message());
            }
        }
        throw std::runtime_error("could not allocate a unique test directory");
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

std::string read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open " + path.string() + " for reading");
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error("failed to read " + path.string());
    }
    return buffer.str();
}

void write_text(const fs::path& path, std::string_view text) {
    const fs::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        fs::create_directories(parent, error);
        if (error) {
            throw std::runtime_error(
                "failed to create " + parent.string() + ": " + error.message());
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to open " + path.string() + " for writing");
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output) {
        throw std::runtime_error("failed to write " + path.string());
    }
}

std::optional<std::string> environment_value(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::nullopt : std::optional<std::string>(value);
}

void assign_environment_value(
    const char* name,
    const std::optional<std::string>& value) {
#if defined(_WIN32)
    const int result = ::_putenv_s(name, value.has_value() ? value->c_str() : "");
#else
    const int result = value.has_value()
        ? ::setenv(name, value->c_str(), 1)
        : ::unsetenv(name);
#endif
    if (result != 0) {
        throw std::runtime_error(std::string("failed to update environment variable ") + name);
    }
}

class ScopedPreferenceEnvironment {
public:
    ScopedPreferenceEnvironment()
        : marrow_config_home_(environment_value("MARROW_CONFIG_HOME")),
          home_(environment_value("HOME")),
          xdg_config_home_(environment_value("XDG_CONFIG_HOME")) {}

    ~ScopedPreferenceEnvironment() {
        try {
            assign_environment_value("MARROW_CONFIG_HOME", marrow_config_home_);
            assign_environment_value("HOME", home_);
            assign_environment_value("XDG_CONFIG_HOME", xdg_config_home_);
        } catch (...) {
            // Environment restoration is asserted explicitly by the test. A
            // destructor must not mask the original failure with an exception.
        }
    }

    void set(const char* name, std::optional<std::string> value) {
        assign_environment_value(name, value);
    }

private:
    std::optional<std::string> marrow_config_home_;
    std::optional<std::string> home_;
    std::optional<std::string> xdg_config_home_;
};

class ScopedRenameCallback {
public:
    explicit ScopedRenameCallback(marrow::editor::detail::RenameCallback callback) {
        marrow::editor::detail::set_preference_rename_callback_for_testing(
            std::move(callback));
    }

    ~ScopedRenameCallback() {
        marrow::editor::detail::set_preference_rename_callback_for_testing({});
    }

    ScopedRenameCallback(const ScopedRenameCallback&) = delete;
    ScopedRenameCallback& operator=(const ScopedRenameCallback&) = delete;
};

void expect_default_preferences(
    TestSuite& suite,
    const EditorPreferences& preferences,
    std::string_view context) {
    suite.expect(
        preferences.default_curve == CurvePreset::Linear,
        std::string(context) + " should use the linear curve default");
    suite.expect(
        preferences.recent_projects.empty(),
        std::string(context) + " should use an empty recent-project list");
}

void test_first_run(TestSuite& suite) {
    TemporaryDirectory temporary("first-run");
    const fs::path settings_path = temporary.path() / "nested" / "editor-settings.json";
    PreferenceStore store(settings_path);

    const auto result = store.load();
    suite.expect(result.status == PreferenceLoadStatus::FirstRun,
                 "missing settings should report FirstRun");
    suite.expect(result.path == settings_path,
                 "load result should report the resolved settings path");
    suite.expect(result.diagnostic.empty(),
                 "first-run defaults should not report an error diagnostic");
    expect_default_preferences(suite, result.preferences, "first run");
    suite.expect(!fs::exists(settings_path),
                 "loading first-run defaults must not create the settings file");
    suite.expect(!fs::exists(settings_path.parent_path()),
                 "loading first-run defaults must not create the settings directory");
}

void test_curve_tokens_and_raw_recent_paths(TestSuite& suite) {
    TemporaryDirectory temporary("roundtrip");
    const fs::path settings_path = temporary.path() / "editor-settings.json";
    PreferenceStore store(settings_path);

    const std::vector<std::pair<CurvePreset, std::string>> presets{
        {CurvePreset::Linear, "linear"},
        {CurvePreset::Stepped, "stepped"},
        {CurvePreset::Ease, "ease"},
        {CurvePreset::EaseIn, "ease_in"},
        {CurvePreset::EaseOut, "ease_out"},
        {CurvePreset::EaseInOut, "ease_in_out"},
    };
    const std::vector<fs::path> recent_projects{
        "relative/project.marrow",
        "../same-spelling.marrow",
        "relative/project.marrow",
        "/does/not/need/to/exist.marrow",
        "path with spaces/project.marrow",
        "one.marrow",
        "two.marrow",
        "three.marrow",
        "four.marrow",
        "five.marrow",
        "six.marrow",
        "seven.marrow",
    };

    for (const auto& [preset, token] : presets) {
        EditorPreferences preferences;
        preferences.default_curve = preset;
        preferences.recent_projects = recent_projects;

        const auto save_result = store.save(preferences);
        suite.expect(static_cast<bool>(save_result),
                     "saving the " + token + " preset should succeed");
        suite.expect(save_result.path == settings_path,
                     "save result should report the settings path");
        if (!save_result) {
            continue;
        }

        const auto parsed = json::load_document(settings_path);
        suite.expect(static_cast<bool>(parsed),
                     "saved preferences should be valid JSON");
        if (parsed) {
            const json::Value* curve =
                json::find_member(parsed.document->root, "default_curve");
            suite.expect(curve != nullptr && curve->is_string() &&
                             curve->as_string() == token,
                         "curve enum should serialize to token " + token);
        }

        const auto load_result = store.load();
        suite.expect(load_result.status == PreferenceLoadStatus::Loaded,
                     "a complete saved document should load without defaults");
        suite.expect(load_result.preferences.default_curve == preset,
                     "curve token should round-trip to its enum");
        suite.expect(load_result.preferences.recent_projects == recent_projects,
                     "recent paths should retain spelling, order, duplicates, and count");
    }
}

void test_optional_fallbacks_and_unknown_fields(TestSuite& suite) {
    TemporaryDirectory temporary("fallbacks");
    const fs::path settings_path = temporary.path() / "editor-settings.json";
    PreferenceStore store(settings_path);

    write_text(
        settings_path,
        R"({"version":1,"recent_projects":["kept.marrow"]})");
    auto result = store.load();
    suite.expect(result.status == PreferenceLoadStatus::LoadedWithDefaults,
                 "a missing curve should use a field-local default");
    suite.expect(result.preferences.default_curve == CurvePreset::Linear,
                 "a missing curve should default to linear");
    suite.expect(result.preferences.recent_projects ==
                     std::vector<fs::path>{"kept.marrow"},
                 "a valid recent list should survive a missing curve");

    write_text(
        settings_path,
        R"({"version":1,"default_curve":"ease","recent_projects":false})");
    result = store.load();
    suite.expect(result.status == PreferenceLoadStatus::LoadedWithDefaults,
                 "a wrongly typed recent list should use a field-local default");
    suite.expect(result.preferences.default_curve == CurvePreset::Ease,
                 "a valid curve should survive a bad recent list");
    suite.expect(result.preferences.recent_projects.empty(),
                 "a bad recent list should default to empty");

    write_text(
        settings_path,
        R"({"version":1,"default_curve":"stepped"})");
    result = store.load();
    suite.expect(result.status == PreferenceLoadStatus::LoadedWithDefaults,
                 "a missing recent list should use a field-local default");
    suite.expect(result.preferences.default_curve == CurvePreset::Stepped,
                 "a valid curve should survive a missing recent list");
    suite.expect(result.preferences.recent_projects.empty(),
                 "a missing recent list should default to empty");

    write_text(
        settings_path,
        R"({"version":1,"default_curve":"unknown","recent_projects":["kept.marrow"]})");
    result = store.load();
    suite.expect(result.status == PreferenceLoadStatus::LoadedWithDefaults,
                 "an unknown curve token should report defaults");
    suite.expect(result.preferences.default_curve == CurvePreset::Linear,
                 "an unknown curve token should default to linear");
    suite.expect(result.preferences.recent_projects ==
                     std::vector<fs::path>{"kept.marrow"},
                 "a valid recent list should survive an unknown curve token");

    write_text(
        settings_path,
        R"({
  "version": 1,
  "default_curve": "ease_in",
  "recent_projects": ["first.marrow", 7, "", null, "second.marrow"],
  "future": {"answer": 42, "precise": 9007199254740991, "items": [true, "opaque"]},
  "future_flag": true
})");
    result = store.load();
    suite.expect(result.status == PreferenceLoadStatus::LoadedWithDefaults,
                 "invalid recent entries should be skipped with a defaults status");
    suite.expect(result.preferences.default_curve == CurvePreset::EaseIn,
                 "valid curve should survive invalid recent entries");
    suite.expect(result.preferences.recent_projects ==
                     std::vector<fs::path>{"first.marrow", "second.marrow"},
                 "non-string and empty recent entries should be skipped independently");

    result.preferences.default_curve = CurvePreset::EaseOut;
    result.preferences.recent_projects = {"saved.marrow"};
    const auto save_result = store.save(result.preferences);
    suite.expect(static_cast<bool>(save_result),
                 "saving a supported document with additive fields should succeed");

    const auto saved = json::load_document(settings_path);
    suite.expect(static_cast<bool>(saved),
                 "the saved additive-field document should remain valid JSON");
    if (!saved) {
        return;
    }
    const json::Value& root = saved.document->root;
    const json::Value* future = json::find_member(root, "future");
    const json::Value* future_flag = json::find_member(root, "future_flag");
    suite.expect(future != nullptr && future->is_object(),
                 "unknown object fields should survive load/save");
    suite.expect(future_flag != nullptr && future_flag->is_boolean() &&
                     future_flag->as_boolean(),
                 "unknown scalar fields should survive load/save");
    if (future != nullptr && future->is_object()) {
        const json::Value* answer = json::find_member(*future, "answer");
        const json::Value* precise = json::find_member(*future, "precise");
        const json::Value* items = json::find_member(*future, "items");
        suite.expect(answer != nullptr && answer->is_number() &&
                         answer->as_number() == 42.0,
                     "unknown nested numeric data should be preserved");
        suite.expect(precise != nullptr && precise->is_number() &&
                         precise->as_number() == 9007199254740991.0,
                     "unknown high-precision numeric data should be preserved exactly");
        suite.expect(items != nullptr && items->is_array() &&
                         items->as_array().size() == 2U,
                     "unknown nested array data should be preserved");
    }
    const json::Value* curve = json::find_member(root, "default_curve");
    const json::Value* recent = json::find_member(root, "recent_projects");
    suite.expect(curve != nullptr && curve->is_string() &&
                     curve->as_string() == "ease_out",
                 "known curve data should overlay its preserved field");
    suite.expect(recent != nullptr && recent->is_array() &&
                     recent->as_array().size() == 1U &&
                     recent->as_array().front().is_string() &&
                     recent->as_array().front().as_string() == "saved.marrow",
                 "known recent data should overlay its preserved field");

    result.preferences.recent_projects = {fs::path{}, "non-empty.marrow"};
    suite.expect(static_cast<bool>(store.save(result.preferences)),
                 "saving typed preferences should skip an empty path");
    const auto empty_filtered = store.load();
    suite.expect(empty_filtered.status == PreferenceLoadStatus::Loaded &&
                     empty_filtered.preferences.recent_projects ==
                         std::vector<fs::path>{"non-empty.marrow"},
                 "empty typed recent paths should not be written to the document");
}

void test_malformed_and_version_statuses(TestSuite& suite) {
    TemporaryDirectory temporary("status");
    const fs::path settings_path = temporary.path() / "editor-settings.json";
    PreferenceStore store(settings_path);

    struct Case {
        std::string_view name;
        std::string_view text;
        PreferenceLoadStatus status;
    };
    const std::vector<Case> cases{
        {"missing version",
         R"({"default_curve":"linear","recent_projects":[]})",
         PreferenceLoadStatus::Malformed},
        {"fractional version",
         R"({"version":1.5,"default_curve":"linear","recent_projects":[]})",
         PreferenceLoadStatus::Malformed},
        {"future version",
         R"({"version":2,"default_curve":"stepped","recent_projects":["future.marrow"]})",
         PreferenceLoadStatus::UnsupportedVersion},
        {"large future version",
         R"({"version":1e100,"default_curve":"linear","recent_projects":[]})",
         PreferenceLoadStatus::UnsupportedVersion},
        {"large negative version",
         R"({"version":-1e100,"default_curve":"linear","recent_projects":[]})",
         PreferenceLoadStatus::UnsupportedVersion},
        {"malformed JSON", R"({"version":1,)", PreferenceLoadStatus::Malformed},
        {"non-object root", R"([1,2,3])", PreferenceLoadStatus::Malformed},
    };

    for (const Case& test_case : cases) {
        write_text(settings_path, test_case.text);
        const auto result = store.load();
        suite.expect(result.status == test_case.status,
                     std::string(test_case.name) + " should report its expected status");
        suite.expect(result.path == settings_path,
                     std::string(test_case.name) + " should report the source path");
        suite.expect(!result.diagnostic.empty(),
                     std::string(test_case.name) + " should include a diagnostic");
        expect_default_preferences(suite, result.preferences, test_case.name);
    }

    const std::string future_bytes =
        R"({"version":37,"default_curve":"future","recent_projects":[],"payload":"keep"})";
    write_text(settings_path, future_bytes);
    EditorPreferences replacement;
    replacement.default_curve = CurvePreset::Stepped;
    const auto refused = store.save(replacement);
    suite.expect(!static_cast<bool>(refused),
                 "ordinary save must refuse to overwrite a future version");
    suite.expect(!refused.error.empty(),
                 "future-version save refusal should include an error");
    suite.expect(read_text(settings_path) == future_bytes,
                 "future-version bytes must remain exactly unchanged");

    const std::string malformed_bytes = R"({"version":1,)";
    write_text(settings_path, malformed_bytes);
    EditorPreferences recovery;
    recovery.default_curve = CurvePreset::EaseInOut;
    recovery.recent_projects = {"recovered.marrow"};
    const auto recovered = store.save(recovery);
    suite.expect(static_cast<bool>(recovered),
                 "an explicit save should recover a readable malformed document");
    const auto recovered_load = store.load();
    suite.expect(recovered_load.status == PreferenceLoadStatus::Loaded,
                 "the recovered document should be a complete supported v1 document");
    suite.expect(recovered_load.preferences.default_curve == CurvePreset::EaseInOut &&
                     recovered_load.preferences.recent_projects ==
                         std::vector<fs::path>{"recovered.marrow"},
                 "the recovered document should contain explicitly saved preferences");

    PreferenceStore invalid_path(fs::path{});
    const auto invalid_load = invalid_path.load();
    suite.expect(invalid_load.status == PreferenceLoadStatus::IoError,
                 "an unresolvable settings path should report IoError on load");
    suite.expect(!invalid_load.diagnostic.empty(),
                 "an unresolvable load path should include a diagnostic");
    expect_default_preferences(suite, invalid_load.preferences, "path error");
    const auto invalid_save = invalid_path.save(EditorPreferences{});
    suite.expect(!static_cast<bool>(invalid_save) && !invalid_save.error.empty(),
                 "an unresolvable settings path should fail save with an error");
}

void test_pure_path_resolution(TestSuite& suite) {
    using marrow::editor::detail::PreferenceEnvironment;
    using marrow::editor::detail::PreferencePlatform;
    using marrow::editor::detail::resolve_preference_settings_path;

    // Use paths that are absolute according to the host filesystem while
    // exercising each target platform's pure path policy.
#if defined(_WIN32)
    const fs::path test_root = R"(C:\MarrowPreferenceTest)";
#else
    const fs::path test_root = "/marrow-preference-test";
#endif
    const fs::path override_home = test_root / "override" / "config";
    const fs::path home = test_root / "users" / "test";
    const fs::path xdg = test_root / "xdg" / "config";
    const fs::path xdg_without_home = test_root / "xdg" / "without-home";

    PreferenceEnvironment environment;
    environment.marrow_config_home = override_home.string();
    environment.home = home.string();
    environment.xdg_config_home = xdg.string();

    auto result = resolve_preference_settings_path(PreferencePlatform::MacOS, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path == override_home / "editor-settings.json",
                 "MARROW_CONFIG_HOME should take priority on macOS");
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path == override_home / "editor-settings.json",
                 "MARROW_CONFIG_HOME should take priority on Linux");

    environment.marrow_config_home.reset();
    result = resolve_preference_settings_path(PreferencePlatform::MacOS, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         home / "Library" / "Application Support" / "Marrow" /
                             "editor-settings.json",
                 "macOS should resolve settings below HOME Application Support");
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path == xdg / "marrow" / "editor-settings.json",
                 "Linux should prefer an absolute XDG_CONFIG_HOME");

    environment.xdg_config_home.reset();
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path == home / ".config" / "marrow" /
                         "editor-settings.json",
                 "Linux should fall back to HOME/.config");

    environment.marrow_config_home = "";
    environment.xdg_config_home = "";
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path == home / ".config" / "marrow" /
                         "editor-settings.json",
                 "empty override and XDG values should be treated as unset");

    environment.marrow_config_home = "relative/override";
    result = resolve_preference_settings_path(PreferencePlatform::MacOS, environment);
    suite.expect(!static_cast<bool>(result) && !result.error.empty() &&
                     result.settings_path.empty(),
                 "a relative MARROW_CONFIG_HOME should be rejected without fallback");

    environment.marrow_config_home.reset();
    environment.xdg_config_home = "relative/xdg";
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(!static_cast<bool>(result) && !result.error.empty() &&
                     result.settings_path.empty(),
                 "a relative XDG_CONFIG_HOME should be rejected without fallback");

    environment.xdg_config_home = xdg_without_home.string();
    environment.home.reset();
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         xdg_without_home / "marrow" / "editor-settings.json",
                 "an absolute Linux XDG path should not require HOME");

    environment.xdg_config_home.reset();
    result = resolve_preference_settings_path(PreferencePlatform::MacOS, environment);
    suite.expect(!static_cast<bool>(result) && !result.error.empty() &&
                     result.settings_path.empty(),
                 "macOS should reject a missing HOME");
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(!static_cast<bool>(result) && !result.error.empty() &&
                     result.settings_path.empty(),
                 "Linux fallback should reject a missing HOME");

    environment.home = "relative/home";
    result = resolve_preference_settings_path(PreferencePlatform::MacOS, environment);
    suite.expect(!static_cast<bool>(result) && !result.error.empty(),
                 "a relative HOME should be rejected rather than resolved from cwd");

    environment = {};
#if defined(_WIN32)
    const fs::path roaming_app_data = "C:\\Users\\Marrow\\AppData\\Roaming";
#else
    const fs::path roaming_app_data = "/windows/AppData/Roaming";
#endif
    environment.roaming_app_data = roaming_app_data;
    result = resolve_preference_settings_path(PreferencePlatform::Windows, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         roaming_app_data / "Marrow" / "editor-settings.json",
                 "Windows should resolve below Roaming AppData/Marrow");

    environment.roaming_app_data = fs::path("relative-appdata");
    result = resolve_preference_settings_path(PreferencePlatform::Windows, environment);
    suite.expect(!static_cast<bool>(result) && !result.error.empty(),
                 "a relative Roaming AppData path should be rejected");

    environment.roaming_app_data.reset();
    result = resolve_preference_settings_path(PreferencePlatform::Windows, environment);
    suite.expect(!static_cast<bool>(result) && !result.error.empty(),
                 "Windows should reject missing Roaming AppData");

#if defined(_WIN32)
    const fs::path windows_override = "D:\\MarrowConfig";
#else
    const fs::path windows_override = "/windows-override";
#endif
    environment.marrow_config_home = windows_override.string();
    environment.roaming_app_data = fs::path("relative-appdata");
    result = resolve_preference_settings_path(PreferencePlatform::Windows, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         windows_override / "editor-settings.json",
                 "MARROW_CONFIG_HOME should take priority on Windows");

#if defined(_WIN32)
    const std::string unicode_override_utf8 = u8"D:\\Marrow-설정";
#else
    const std::string unicode_override_utf8 = u8"/windows/Marrow-설정";
#endif
    const fs::path unicode_override = fs::u8path(unicode_override_utf8);
    environment.marrow_config_home = unicode_override_utf8;
    result = resolve_preference_settings_path(PreferencePlatform::Windows, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         unicode_override / "editor-settings.json",
                 "Windows UTF-8 config overrides should become native paths without loss");
}

void test_process_environment_resolution_and_restoration(TestSuite& suite) {
    TemporaryDirectory temporary("environment");
    const auto original_override = environment_value("MARROW_CONFIG_HOME");
    const auto original_home = environment_value("HOME");
    const auto original_xdg = environment_value("XDG_CONFIG_HOME");

    {
        ScopedPreferenceEnvironment environment;
        const fs::path override_home = temporary.path() / "override";
        const fs::path home = temporary.path() / "home";
        const fs::path xdg = temporary.path() / "xdg";
        environment.set("HOME", home.string());
        environment.set("XDG_CONFIG_HOME", xdg.string());
        environment.set("MARROW_CONFIG_HOME", override_home.string());

        PreferenceStore overridden;
        suite.expect(overridden.settings_path() ==
                         override_home / "editor-settings.json",
                     "default construction should honor MARROW_CONFIG_HOME first");
        suite.expect(overridden.load().status == PreferenceLoadStatus::FirstRun,
                     "the override path should begin isolated from real preferences");
        EditorPreferences isolated_preferences;
        isolated_preferences.default_curve = CurvePreset::Ease;
        suite.expect(static_cast<bool>(overridden.save(isolated_preferences)) &&
                         fs::exists(override_home / "editor-settings.json"),
                     "default-store writes should stay below MARROW_CONFIG_HOME");

        environment.set("MARROW_CONFIG_HOME", std::string{});
        PreferenceStore empty_override;
        environment.set("MARROW_CONFIG_HOME", std::nullopt);
        PreferenceStore unset_override;
        suite.expect(empty_override.settings_path() == unset_override.settings_path(),
                     "empty MARROW_CONFIG_HOME should behave exactly like unset");

#if defined(__APPLE__)
        const fs::path expected =
            home / "Library" / "Application Support" / "Marrow" /
            "editor-settings.json";
#elif defined(__linux__)
        const fs::path expected = xdg / "marrow" / "editor-settings.json";
#elif defined(_WIN32)
        PWSTR roaming_path = nullptr;
        const HRESULT roaming_result = SHGetKnownFolderPath(
            FOLDERID_RoamingAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &roaming_path);
        const fs::path expected = SUCCEEDED(roaming_result) && roaming_path != nullptr
            ? fs::path(roaming_path) / "Marrow" / "editor-settings.json"
            : fs::path{};
        if (roaming_path != nullptr) {
            CoTaskMemFree(roaming_path);
        }
        suite.expect(!expected.empty(),
                     "Windows should expose the production Roaming AppData path");
#else
#error "PreferenceStore environment tests do not support this platform."
#endif
        suite.expect(unset_override.settings_path() == expected,
                     "default construction should use the platform production path");
    }

    suite.expect(environment_value("MARROW_CONFIG_HOME") == original_override,
                 "MARROW_CONFIG_HOME should be restored after the test");
    suite.expect(environment_value("HOME") == original_home,
                 "HOME should be restored after the test");
    suite.expect(environment_value("XDG_CONFIG_HOME") == original_xdg,
                 "XDG_CONFIG_HOME should be restored after the test");
}

void test_atomic_rename_failure(TestSuite& suite) {
    TemporaryDirectory temporary("rename");
    const fs::path settings_path = temporary.path() / "editor-settings.json";
    const std::string old_bytes =
        "{\n  \"version\": 1,\n  \"default_curve\": \"linear\",\n"
        "  \"recent_projects\": [\"old.marrow\"],\n"
        "  \"opaque\": {\"keep\": true}\n}\n";
    write_text(settings_path, old_bytes);

    PreferenceStore store(settings_path);
    auto loaded = store.load();
    suite.expect(loaded.status == PreferenceLoadStatus::Loaded,
                 "rename-failure setup document should load");
    loaded.preferences.default_curve = CurvePreset::Stepped;
    loaded.preferences.recent_projects = {"new.marrow"};

    int rename_calls = 0;
    fs::path observed_source;
    fs::path observed_destination;
    {
        ScopedRenameCallback rename_failure(
            [&](const fs::path& source, const fs::path& destination) {
                ++rename_calls;
                observed_source = source;
                observed_destination = destination;
                return std::make_error_code(std::errc::permission_denied);
            });
        const auto save_result = store.save(loaded.preferences);
        suite.expect(!static_cast<bool>(save_result),
                     "an injected rename failure should fail the save");
        suite.expect(save_result.path == settings_path && !save_result.error.empty(),
                     "rename failure should report the target path and cause");
    }

    suite.expect(rename_calls == 1,
                 "atomic save should attempt exactly one final rename");
    suite.expect(observed_destination == settings_path,
                 "atomic rename destination should be the settings path");
    suite.expect(observed_source.parent_path() == settings_path.parent_path() &&
                     observed_source != settings_path,
                 "the temporary file should be unique and in the destination directory");
    suite.expect(read_text(settings_path) == old_bytes,
                 "rename failure must preserve existing settings bytes exactly");
    suite.expect(!observed_source.empty() && !fs::exists(observed_source),
                 "rename failure should remove the exact temporary file");

    std::vector<fs::path> remaining_entries;
    for (const auto& entry : fs::directory_iterator(temporary.path())) {
        remaining_entries.push_back(entry.path().filename());
    }
    suite.expect(remaining_entries ==
                     std::vector<fs::path>{settings_path.filename()},
                 "rename failure should not leave any other temporary file behind");
}

struct SessionSnapshot {
    std::string serialized_project;
    bool dirty{false};
    bool can_undo{false};
    bool can_redo{false};
    std::size_t undo_count{0};
    std::size_t redo_count{0};
    std::string undo_label;
    std::string redo_label;
    std::uint64_t project_revision{0};
    std::uint64_t runtime_revision{0};
    std::uint64_t preview_revision{0};
};

SessionSnapshot snapshot_session(const marrow::editor::EditorSession& session) {
    SessionSnapshot snapshot;
    snapshot.serialized_project =
        marrow::editor::serialize_project(*session.project());
    snapshot.dirty = session.dirty();
    snapshot.can_undo = session.can_undo();
    snapshot.can_redo = session.can_redo();
    snapshot.undo_count = session.undo_count();
    snapshot.redo_count = session.redo_count();
    snapshot.undo_label = session.undo_label();
    snapshot.redo_label = session.redo_label();
    snapshot.project_revision = session.project_revision();
    snapshot.runtime_revision = session.runtime_revision();
    snapshot.preview_revision = session.preview_revision();
    return snapshot;
}

void expect_session_equal(
    TestSuite& suite,
    const SessionSnapshot& expected,
    const marrow::editor::EditorSession& session) {
    suite.expect(session.project() != nullptr,
                 "preference activity should not close the project");
    if (session.project() == nullptr) {
        return;
    }
    suite.expect(marrow::editor::serialize_project(*session.project()) ==
                     expected.serialized_project,
                 "preference activity should not alter serialized project state");
    suite.expect(session.dirty() == expected.dirty,
                 "preference activity should not alter dirty state");
    suite.expect(session.can_undo() == expected.can_undo &&
                     session.undo_count() == expected.undo_count &&
                     session.undo_label() == expected.undo_label,
                 "preference activity should not alter undo history");
    suite.expect(session.can_redo() == expected.can_redo &&
                     session.redo_count() == expected.redo_count &&
                     session.redo_label() == expected.redo_label,
                 "preference activity should not alter redo history");
    suite.expect(session.project_revision() == expected.project_revision,
                 "preference activity should not alter the project revision");
    suite.expect(session.runtime_revision() == expected.runtime_revision,
                 "preference activity should not alter the runtime revision");
    suite.expect(session.preview_revision() == expected.preview_revision,
                 "preference activity should not alter the preview revision");
}

void test_editor_session_isolation(TestSuite& suite) {
    marrow::editor::EditorSession session;
    const auto opened = session.open("assets/fixtures/player_idle.marrow");
    if (!suite.expect(static_cast<bool>(opened),
                      "the editor-session fixture should open")) {
        return;
    }

    {
        auto first = session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "Preference isolation one",
            "preference-isolation-one",
            false,
        });
        if (!suite.expect(static_cast<bool>(first),
                          "the first isolation transaction should begin")) {
            return;
        }
        first.project()->editor_metadata.notes += " preference-isolation-one";
        if (!suite.expect(static_cast<bool>(first.commit()),
                          "the first isolation transaction should commit")) {
            return;
        }
    }
    {
        auto second = session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "Preference isolation two",
            "preference-isolation-two",
            false,
        });
        if (!suite.expect(static_cast<bool>(second),
                          "the second isolation transaction should begin")) {
            return;
        }
        second.project()->editor_metadata.notes += " preference-isolation-two";
        if (!suite.expect(static_cast<bool>(second.commit()),
                          "the second isolation transaction should commit")) {
            return;
        }
    }
    if (!suite.expect(static_cast<bool>(session.undo()),
                      "the second isolation edit should undo")) {
        return;
    }
    suite.expect(session.dirty(),
                 "isolation setup should retain a dirty first edit");
    suite.expect(session.can_undo() && session.can_redo(),
                 "isolation setup should contain both undo and redo history");

    const SessionSnapshot before = snapshot_session(session);
    TemporaryDirectory temporary("session");
    PreferenceStore store(temporary.path() / "editor-settings.json");
    const auto first_run = store.load();
    suite.expect(first_run.status == PreferenceLoadStatus::FirstRun,
                 "session-isolation preference store should start empty");
    EditorPreferences preferences = first_run.preferences;
    preferences.default_curve = CurvePreset::Ease;
    preferences.recent_projects = {
        "assets/fixtures/player_idle.marrow",
        "another-project.marrow",
    };
    const auto saved = store.save(preferences);
    suite.expect(static_cast<bool>(saved),
                 "session-isolation preference save should succeed");
    const auto reloaded = store.load();
    suite.expect(reloaded.status == PreferenceLoadStatus::Loaded &&
                     reloaded.preferences.default_curve == CurvePreset::Ease,
                 "session-isolation preferences should reload normally");

    expect_session_equal(suite, before, session);
}


// MAR-170: the six fixed presets. Every number is spelled out literally here
// rather than read from `kCurvePresets`, because a test that reads the constant
// it checks proves nothing.
struct ExpectedPreset {
    CurvePreset preset;
    const char* token;
    const char* display;
    InterpolationKind kind;
    double cx1;
    double cy1;
    double cx2;
    double cy2;
};

const std::vector<ExpectedPreset>& expected_presets() {
    static const std::vector<ExpectedPreset> presets{
        {CurvePreset::Linear, "linear", "Linear",
         InterpolationKind::Linear, 0.0, 0.0, 0.0, 0.0},
        {CurvePreset::Stepped, "stepped", "Stepped",
         InterpolationKind::Stepped, 0.0, 0.0, 0.0, 0.0},
        {CurvePreset::Ease, "ease", "Ease",
         InterpolationKind::CubicBezier, 0.25, 0.1, 0.25, 1.0},
        {CurvePreset::EaseIn, "ease_in", "Ease-In",
         InterpolationKind::CubicBezier, 0.42, 0.0, 1.0, 1.0},
        {CurvePreset::EaseOut, "ease_out", "Ease-Out",
         InterpolationKind::CubicBezier, 0.0, 0.0, 0.58, 1.0},
        {CurvePreset::EaseInOut, "ease_in_out", "Ease-In-Out",
         InterpolationKind::CubicBezier, 0.42, 0.0, 0.58, 1.0},
    };
    return presets;
}

// MAR-170: the exact call sequence `load_shell_preferences()` and
// `set_shell_default_curve()` perform. Those two functions take a `ShellState`,
// which lives in the `marrow_editor_shell` target rather than in
// `marrow_editor`, so this case drives a default-constructed `PreferenceStore`
// through `MARROW_CONFIG_HOME` — the same resolution the shell gets — and the
// `ShellState`-level wiring is covered by the shell smoke. Do not "fix" this
// split by linking the shell into a unit test.
void test_shell_preference_session(TestSuite& suite) {
    ScopedPreferenceEnvironment environment;
    TemporaryDirectory temporary("shell-session");
    environment.set("MARROW_CONFIG_HOME", temporary.path().string());
    const fs::path settings_path = temporary.path() / "editor-settings.json";

    {
        const PreferenceStore store;
        suite.expect(store.settings_path() == settings_path,
                     "an isolated config home should resolve the shell settings path");
        const auto first = store.load();
        suite.expect(first.status == PreferenceLoadStatus::FirstRun,
                     "a fresh config home should report a first run");
        expect_default_preferences(suite, first.preferences, "a first-run shell load");
        suite.expect(!fs::exists(settings_path),
                     "loading preferences must never create a settings file");
    }

    // set_shell_default_curve(): mutate the LOADED preferences and save them, so
    // recent_projects and unknown additive fields survive.
    {
        const PreferenceStore store;
        EditorPreferences preferences = store.load().preferences;
        preferences.default_curve = CurvePreset::EaseOut;
        suite.expect(static_cast<bool>(store.save(preferences)),
                     "storing a new default curve should succeed");
        const auto reloaded = store.load();
        suite.expect(reloaded.status == PreferenceLoadStatus::Loaded &&
                         reloaded.preferences.default_curve == CurvePreset::EaseOut,
                     "a stored default curve should reload as itself");
    }

    // Preservation: an unknown additive field and a non-empty recent list must
    // survive a default-only change.
    {
        write_text(
            settings_path,
            "{\n  \"version\": 1,\n  \"default_curve\": \"ease\",\n"
            "  \"recent_projects\": [\"one.marrow\", \"two.marrow\"],\n"
            "  \"future_field\": {\"kept\": 7}\n}\n");
        const PreferenceStore store;
        const auto loaded = store.load();
        suite.expect(loaded.preferences.default_curve == CurvePreset::Ease,
                     "the preserved-field case should start from the ease preset");
        EditorPreferences preferences = loaded.preferences;
        preferences.default_curve = CurvePreset::Stepped;
        suite.expect(static_cast<bool>(store.save(preferences)),
                     "saving a default-only change should succeed");
        const auto parsed = json::load_document(settings_path);
        suite.expect(static_cast<bool>(parsed), "the rewritten settings should be JSON");
        if (parsed) {
            const json::Value* future =
                json::find_member(parsed.document->root, "future_field");
            const json::Value* kept =
                future != nullptr ? json::find_member(*future, "kept") : nullptr;
            suite.expect(kept != nullptr && kept->is_number() && kept->as_number() == 7.0,
                         "an unknown additive field must survive a default change");
        }
        const auto after = store.load();
        suite.expect(after.preferences.default_curve == CurvePreset::Stepped,
                     "the new default should be the one that was stored");
        suite.expect(after.preferences.recent_projects ==
                         std::vector<fs::path>{"one.marrow", "two.marrow"},
                     "recent projects must survive a default-only change");
    }

    // Malformed bytes fall back to Linear and are left exactly as found.
    {
        const std::string malformed = "{ this is not json";
        write_text(settings_path, malformed);
        const PreferenceStore store;
        const auto loaded = store.load();
        suite.expect(loaded.status == PreferenceLoadStatus::Malformed,
                     "malformed settings should report Malformed");
        expect_default_preferences(suite, loaded.preferences, "a malformed shell load");
        suite.expect(read_text(settings_path) == malformed,
                     "loading must never repair a malformed settings file");
    }

    // A future version is refused by save() and survives byte-for-byte.
    {
        const std::string future =
            "{\n  \"version\": 2,\n  \"default_curve\": \"ease_in\"\n}\n";
        write_text(settings_path, future);
        const PreferenceStore store;
        const auto loaded = store.load();
        suite.expect(loaded.status == PreferenceLoadStatus::UnsupportedVersion,
                     "a newer settings version should report UnsupportedVersion");
        expect_default_preferences(
            suite, loaded.preferences, "an unsupported-version shell load");
        EditorPreferences preferences = loaded.preferences;
        preferences.default_curve = CurvePreset::EaseInOut;
        const auto saved = store.save(preferences);
        suite.expect(!saved && !saved.error.empty(),
                     "saving over an unsupported future version must be refused");
        suite.expect(read_text(settings_path) == future,
                     "a refused save must preserve the future file byte-for-byte");
    }
}

void test_curve_preset_constants(TestSuite& suite) {
    const std::vector<ExpectedPreset>& expected = expected_presets();
    suite.expect(kCurvePresets.size() == expected.size(),
                 "there must be exactly six fixed curve presets");
    if (kCurvePresets.size() != expected.size()) return;

    for (std::size_t index = 0U; index < expected.size(); ++index) {
        const ExpectedPreset& want = expected[index];
        const auto& got = kCurvePresets[index];
        suite.expect(got.preset == want.preset && got.token == want.token &&
                         got.display_name == want.display && got.kind == want.kind &&
                         got.control_points[0] == want.cx1 &&
                         got.control_points[1] == want.cy1 &&
                         got.control_points[2] == want.cx2 &&
                         got.control_points[3] == want.cy2,
                     std::string("preset ") + want.token +
                         " must match its fixed definition");
        suite.expect(&curve_preset_definition(want.preset) == &got,
                     std::string("curve_preset_definition must index ") + want.token);
        suite.expect(curve_preset_from_token(want.token) ==
                         std::optional<CurvePreset>(want.preset),
                     std::string("token ") + want.token + " must parse to its preset");

        // The format invariant both loaders enforce, as double and after the
        // float32 narrowing CubicBezierControlPoints performs on store.
        if (want.kind == InterpolationKind::CubicBezier) {
            const double nx1 = static_cast<double>(static_cast<AnimationScalar>(want.cx1));
            const double nx2 = static_cast<double>(static_cast<AnimationScalar>(want.cx2));
            suite.expect(want.cx1 >= 0.0 && want.cx1 <= 1.0 && want.cx2 >= 0.0 &&
                             want.cx2 <= 1.0 && nx1 >= 0.0 && nx1 <= 1.0 &&
                             nx2 >= 0.0 && nx2 <= 1.0,
                         std::string("preset ") + want.token +
                             " must keep cx inside [0, 1] before and after narrowing");
            // No preset may overshoot: 0 <= cy1 <= cy2 <= 1.
            suite.expect(want.cy1 >= 0.0 && want.cy1 <= want.cy2 && want.cy2 <= 1.0,
                         std::string("preset ") + want.token +
                             " must keep cy monotone inside [0, 1]");
            // X monotonicity needs cx2 >= cx1 for the runtime inverse solve.
            suite.expect(want.cx2 >= want.cx1,
                         std::string("preset ") + want.token +
                             " must keep cx2 >= cx1 so X(t) is non-decreasing");
        }

        const auto interpolation = curve_preset_interpolation(want.preset);
        suite.expect(interpolation.kind() == want.kind,
                     std::string("preset ") + want.token +
                         " must build its declared interpolation kind");
        if (want.kind == InterpolationKind::CubicBezier) {
            const auto& points = interpolation.cubic_bezier();
            suite.expect(points.cx1 == static_cast<AnimationScalar>(want.cx1) &&
                             points.cy1 == static_cast<AnimationScalar>(want.cy1) &&
                             points.cx2 == static_cast<AnimationScalar>(want.cx2) &&
                             points.cy2 == static_cast<AnimationScalar>(want.cy2),
                         std::string("preset ") + want.token +
                             " must store the narrowed literal control points");
        }
        suite.expect(curve_preset_of(interpolation) ==
                         std::optional<CurvePreset>(want.preset),
                     std::string("preset ") + want.token + " must read back as itself");
    }

    // Custom curves and MAR-169's conversion seed are not presets.
    suite.expect(!curve_preset_of(marrow::runtime::Interpolation::cubic_bezier(
                                      0.2, -0.4, 0.8, 1.6))
                      .has_value(),
                 "an overshoot curve must not be reported as a preset");
    suite.expect(!curve_preset_of(marrow::runtime::Interpolation::cubic_bezier(
                                      1.0 / 3.0, 1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0))
                      .has_value(),
                 "MAR-169's linear-equivalent conversion seed is not a preset");
    // One ULP away from Ease must not read as Ease: the comparison is exact.
    {
        const auto ease = curve_preset_interpolation(CurvePreset::Ease);
        const AnimationScalar nudged =
            std::nextafter(ease.cubic_bezier().cy1, static_cast<AnimationScalar>(1.0));
        suite.expect(!curve_preset_of(marrow::runtime::Interpolation::cubic_bezier(
                                          static_cast<double>(ease.cubic_bezier().cx1),
                                          static_cast<double>(nudged),
                                          static_cast<double>(ease.cubic_bezier().cx2),
                                          static_cast<double>(ease.cubic_bezier().cy2)))
                          .has_value(),
                     "a curve one ULP away from Ease must read as Custom");
    }
    suite.expect(!curve_preset_from_token("ease-in").has_value() &&
                     !curve_preset_from_token("easeIn").has_value() &&
                     !curve_preset_from_token("bounce").has_value() &&
                     !curve_preset_from_token("").has_value(),
                 "only the six snake_case tokens are accepted");

    // Runtime well-posedness of every cubic preset, including Ease-In's
    // X'(1) = 0 degenerate right endpoint.
    for (const ExpectedPreset& want : expected) {
        if (want.kind != InterpolationKind::CubicBezier) continue;
        const auto curve = curve_preset_interpolation(want.preset);
        double previous = curve.transform(0.0);
        bool ok = previous == 0.0;
        for (int step = 1; step <= 100; ++step) {
            const double alpha = static_cast<double>(step) / 100.0;
            const double value = curve.transform(alpha);
            ok = ok && std::isfinite(value) && value >= previous - 1e-6 &&
                value >= -1e-6 && value <= 1.0 + 1e-6;
            previous = value;
        }
        ok = ok && std::abs(curve.transform(1.0) - 1.0) < 1e-6;
        suite.expect(ok,
                     std::string("preset ") + want.token +
                         " must evaluate finite, monotone, and overshoot-free on [0, 1]");
    }
}

// Keeps `authoring.hpp`'s table and `preferences.cpp`'s private token list in
// agreement without refactoring either: the settings file is the only shared
// contract, so a file written with a table token must load back to that preset.
void test_curve_preset_tokens_match_settings_tokens(TestSuite& suite) {
    TemporaryDirectory temporary("preset-tokens");
    const fs::path settings_path = temporary.path() / "editor-settings.json";
    PreferenceStore store(settings_path);

    for (const auto& entry : kCurvePresets) {
        const std::string token(entry.token);
        write_text(
            settings_path,
            "{\n  \"version\": 1,\n  \"default_curve\": \"" + token +
                "\",\n  \"recent_projects\": []\n}\n");
        const auto loaded = store.load();
        suite.expect(loaded.status == PreferenceLoadStatus::Loaded,
                     "a settings file carrying token " + token + " should load cleanly");
        suite.expect(
            std::optional<CurvePreset>(loaded.preferences.default_curve) ==
                curve_preset_from_token(token),
            "settings token " + token + " must resolve to the same preset the table names");
        suite.expect(loaded.preferences.default_curve == entry.preset,
                     "settings token " + token + " must resolve to its table entry");
    }
}

// MAR-183: the recent-project list algebra, plus one isolated store round trip.
//
// The algebra lives in `marrow_editor` (`recent_projects.cpp`) precisely so this
// binary can reach it without linking the shell. See the note above
// `test_shell_preference_session`: do not "fix" that split.
//
// The store layer itself is NOT retested here -- MAR-156 shipped
// `recent_projects` inside settings version 1, and the four cases above already
// cover its parse, its fallbacks and its additive preservation. This case covers
// only what MAR-183 adds: canonicalization, MRU ordering, de-duplication,
// eviction at the bound, and the changed-bool every persist decision is keyed on.
void test_recent_project_list_algebra(TestSuite& suite) {
    using marrow::editor::canonical_recent_path;
    using marrow::editor::drop_missing_recent_paths;
    using marrow::editor::forget_recent_path;
    using marrow::editor::kRecentProjectLimit;
    using marrow::editor::normalize_recent_paths;
    using marrow::editor::promote_recent_project;
    using marrow::editor::recent_project_exists;

    // (1) M4 gate. Printed EVERY run: design 2.3 specifies a fallback chain that
    // only runs if `weakly_canonical` errors on a missing path, and which branch
    // ran is a platform fact, not an assumption.
    {
        const fs::path missing =
            fs::temp_directory_path() / "mar183-does-not-exist" / "x.marrow";
        const fs::path canonical_missing = canonical_recent_path(missing);
        std::cout << "  MAR-183 M4 measured: canonical_recent_path(missing) -> '"
                  << canonical_missing.string() << "' absolute="
                  << canonical_missing.is_absolute() << '\n';
        suite.expect(!canonical_missing.empty(),
                     "canonicalizing a missing path must return something");
        suite.expect(canonical_missing.is_absolute(),
                     "canonicalizing a missing path must still absolutize it -- an "
                     "entry must outlive its file (AC4)");
        suite.expect(canonical_recent_path(fs::path{}).empty(),
                     "canonicalizing an empty path must return empty, not the CWD");
    }

    // (2) Canonicalization of a relative path, and agreement with its own
    // absolute spelling. ShellState::project_path defaults to a RELATIVE path,
    // so this is load-bearing rather than cosmetic.
    const fs::path relative = "assets/fixtures/player_idle.marrow";
    const fs::path canonical_relative = canonical_recent_path(relative);
    std::cout << "  MAR-183 M4 measured: canonical_recent_path(relative existing) -> '"
              << canonical_relative.string() << "'\n";
    suite.expect(canonical_relative.is_absolute(),
                 "a relative recent path must canonicalize to an absolute one");
    suite.expect(canonical_recent_path(fs::absolute(relative)) == canonical_relative,
                 "the relative and absolute spellings of one file must canonicalize equal");

    // (3) Dedup across spellings. (I6)
    {
        std::vector<fs::path> list;
        suite.expect(promote_recent_project(&list, relative),
                     "promoting into an empty list must report a change");
        suite.expect(promote_recent_project(&list, fs::absolute(relative)) == false,
                     "re-promoting the head under a different spelling must be a no-op");
        suite.expect(list.size() == 1,
                     "two spellings of one file must collapse to exactly one entry, got " +
                         std::to_string(list.size()));
        suite.expect(list.front() == canonical_relative,
                     "the surviving entry must be the canonical form");
    }

    // (4)(5) MRU ordering, and promotion rather than duplication.
    const fs::path a = canonical_recent_path("/tmp/mar183/a.marrow");
    const fs::path b = canonical_recent_path("/tmp/mar183/b.marrow");
    const fs::path c = canonical_recent_path("/tmp/mar183/c.marrow");
    {
        std::vector<fs::path> list;
        (void)promote_recent_project(&list, a);
        (void)promote_recent_project(&list, b);
        (void)promote_recent_project(&list, c);
        suite.expect(list == std::vector<fs::path>{c, b, a},
                     "promotion must order most-recent-first");
        suite.expect(promote_recent_project(&list, a),
                     "promoting a non-head existing entry must report a change");
        suite.expect(list == std::vector<fs::path>{a, c, b},
                     "re-promoting an existing entry must move it to the head, not duplicate it");
        suite.expect(list.size() == 3,
                     "re-promotion must not grow the list, got " +
                         std::to_string(list.size()));
    }

    // (6) Eviction at exactly the bound, insert-before-truncate. (I7)
    {
        std::vector<fs::path> list;
        std::vector<fs::path> ordered;
        for (int index = 1; index <= 12; ++index) {
            const fs::path entry = canonical_recent_path(
                fs::path("/tmp/mar183/p") / (std::to_string(index) + ".marrow"));
            ordered.push_back(entry);
            (void)promote_recent_project(&list, entry);
            // The bound is an INVARIANT, checked after EVERY promotion. A cap
            // comparison that is off by one leaves the list one over the bound
            // on odd promotions and corrects itself on even ones, so sampling
            // only the final size tests the single parity that hides the bug.
            suite.expect(list.size() <= kRecentProjectLimit,
                         "the list must NEVER exceed kRecentProjectLimit -- after "
                         "promotion " + std::to_string(index) + " it held " +
                             std::to_string(list.size()));
        }
        suite.expect(list.size() == kRecentProjectLimit,
                     "twelve promotions must leave exactly kRecentProjectLimit entries, got " +
                         std::to_string(list.size()));
        suite.expect(list.front() == ordered[11],
                     "the newest promotion must be at the head, never the evicted one");
        suite.expect(list.back() == ordered[2],
                     "eviction must drop from the TAIL: the oldest survivor must be p3");
        suite.expect(std::find(list.begin(), list.end(), ordered[0]) == list.end(),
                     "p1 must have been evicted");
        suite.expect(std::find(list.begin(), list.end(), ordered[1]) == list.end(),
                     "p2 must have been evicted");
    }

    // (7) The changed-bool in both polarities. This is the precondition for the
    // no-op skip in `persist_recent_projects` (I12): a false that is really true
    // would suppress a needed write, and a true that is really false would
    // rewrite the settings file on every frame.
    {
        std::vector<fs::path> list;
        suite.expect(promote_recent_project(&list, a),
                     "a new promotion must return true");
        const std::vector<fs::path> before = list;
        suite.expect(promote_recent_project(&list, a) == false,
                     "re-promoting the CURRENT HEAD must return false");
        suite.expect(list == before,
                     "a no-op promotion must leave the list element-wise equal");
        suite.expect(forget_recent_path(&list, b) == false,
                     "forgetting an absent path must return false");
        suite.expect(promote_recent_project(&list, fs::path{}) == false,
                     "promoting an empty path must return false and store nothing");
        suite.expect(list.size() == 1,
                     "an empty promotion must not grow the list");
        suite.expect(forget_recent_path(&list, a),
                     "forgetting a present path must return true");
        suite.expect(list.empty(), "forgetting the only entry must empty the list");
    }

    // (8) normalize_recent_paths: canonicalize, drop empties, keep the FIRST of
    // each duplicate group, cap -- and be idempotent.
    {
        // The duplicate pair is deliberately NON-ADJACENT, with two unrelated
        // entries between the two spellings. Adjacent -- or adjacent at the
        // front -- keep-first and keep-last collapse to the same index and a
        // keep-the-last dedup passes every assertion below.
        std::vector<fs::path> list{fs::path{}, relative, "/tmp/mar183/c.marrow",
                                   "/tmp/mar183/d.marrow", fs::absolute(relative)};
        for (int index = 1; index <= 15; ++index) {
            list.emplace_back(fs::path("/tmp/mar183/n") /
                              (std::to_string(index) + ".marrow"));
        }
        suite.expect(normalize_recent_paths(&list),
                     "normalizing a list with an empty entry and a duplicate must report a change");
        suite.expect(list.size() == kRecentProjectLimit,
                     "normalize must cap at kRecentProjectLimit, got " +
                         std::to_string(list.size()));
        suite.expect(std::find(list.begin(), list.end(), fs::path{}) == list.end(),
                     "normalize must drop empty entries");
        suite.expect(std::count(list.begin(), list.end(), canonical_relative) == 1,
                     "the two spellings must have collapsed to one entry");
        // INDEX 0, not merely "present". Keeping the LAST occurrence would put
        // the survivor at index 2, after the two entries that separated the
        // pair, silently reordering a list the user has already seen.
        suite.expect(list.front() == canonical_relative,
                     "dedup must keep the FIRST occurrence AT ITS ORIGINAL INDEX 0");
        suite.expect(list[1] == c && list[2] == canonical_recent_path("/tmp/mar183/d.marrow"),
                     "the entries that separated the duplicate pair must keep their "
                     "positions behind the survivor");
        const std::vector<fs::path> normalized = list;
        suite.expect(normalize_recent_paths(&list) == false,
                     "normalize must be idempotent: a second call must report no change");
        suite.expect(list == normalized,
                     "a second normalize must leave the list element-wise equal");
    }

    // (9) A missing path survives normalize. This is the load half of I5: an
    // entry on an unmounted volume must never be destroyed by merely loading.
    {
        const fs::path gone = fs::temp_directory_path() / "mar183-gone" / "gone.marrow";
        std::vector<fs::path> list{gone};
        (void)normalize_recent_paths(&list);
        suite.expect(list.size() == 1 && list.front() == canonical_recent_path(gone),
                     "normalize must KEEP an entry whose file does not exist");
        suite.expect(recent_project_exists(gone) == false,
                     "recent_project_exists must be false for a missing file");
    }

    // (10) drop_missing_recent_paths over a real directory.
    {
        TemporaryDirectory temporary("recent-missing");
        const fs::path present_one = temporary.path() / "one.marrow";
        const fs::path present_two = temporary.path() / "two.marrow";
        write_text(present_one, "{}\n");
        write_text(present_two, "{}\n");
        const fs::path gone_one = temporary.path() / "gone-one.marrow";
        const fs::path gone_two = temporary.path() / "gone-two.marrow";
        suite.expect(recent_project_exists(present_one),
                     "recent_project_exists must be true for a real file");
        suite.expect(recent_project_exists(temporary.path()) == false,
                     "a DIRECTORY is not a recent project: is_regular_file, not exists");

        std::vector<fs::path> list{present_one, gone_one, present_two, gone_two};
        (void)normalize_recent_paths(&list);
        suite.expect(drop_missing_recent_paths(&list),
                     "dropping missing entries from a list containing two must return true");
        suite.expect(list == std::vector<fs::path>{canonical_recent_path(present_one),
                                                   canonical_recent_path(present_two)},
                     "exactly the missing entries must be dropped, and the present pair "
                     "must keep its relative order");
        suite.expect(drop_missing_recent_paths(&list) == false,
                     "a second drop over an all-present list must return false");
    }

    // (11) The macOS case-identity boundary, MEASURED rather than assumed.
    //
    // Design 2.2 and 10.1 state that `/x/A.marrow` and `/x/a.marrow` produce TWO
    // entries on a case-insensitive volume. That is only half true, and the half
    // it gets wrong is the common one: `weakly_canonical` resolves its longest
    // EXISTING prefix through the filesystem, so on macOS both spellings of a
    // file that EXISTS canonicalize to the on-disk spelling and collapse into one
    // entry. Only when the file is MISSING does the lexical remainder survive
    // verbatim and leave two. Both branches are asserted, and printed, so the
    // behaviour cannot change under us silently.
    {
        TemporaryDirectory temporary("recent-case");
        const fs::path upper_present = temporary.path() / "A.marrow";
        write_text(upper_present, "{}\n");
        const fs::path lower_present = temporary.path() / "a.marrow";
        const bool present_collapse =
            canonical_recent_path(upper_present) == canonical_recent_path(lower_present);

        const fs::path upper_missing = temporary.path() / "GONE.marrow";
        const fs::path lower_missing = temporary.path() / "gone.marrow";
        const bool missing_collapse =
            canonical_recent_path(upper_missing) == canonical_recent_path(lower_missing);

        std::cout << "  MAR-183 case identity measured: existing-file spellings collapse="
                  << present_collapse << " missing-file spellings collapse="
                  << missing_collapse << '\n';
        suite.expect(missing_collapse == false,
                     "two case-variant spellings of a MISSING file must stay distinct: "
                     "the identity rule folds no case of its own");
        std::vector<fs::path> list{upper_missing, lower_missing};
        (void)normalize_recent_paths(&list);
        suite.expect(list.size() == 2,
                     "the two missing spellings must survive normalize as two entries");
    }

    // (12) Store round trip, isolated. The list the shell will write must survive
    // the real serializer, and `default_curve` must survive alongside it.
    {
        ScopedPreferenceEnvironment environment;
        TemporaryDirectory temporary("recent-round-trip");
        environment.set("MARROW_CONFIG_HOME", temporary.path().string());

        std::vector<fs::path> list;
        for (int index = 1; index <= 12; ++index) {
            (void)promote_recent_project(
                &list,
                canonical_recent_path(temporary.path() /
                                      ("r" + std::to_string(index) + ".marrow")));
        }
        suite.expect(list.size() == kRecentProjectLimit,
                     "the round-trip fixture must be exactly at the bound");

        EditorPreferences preferences;
        preferences.default_curve = CurvePreset::EaseOut;
        preferences.recent_projects = list;
        const PreferenceStore store;
        const auto saved = store.save(preferences);
        suite.expect(static_cast<bool>(saved),
                     "saving a normalized recent list must succeed");

        const auto loaded = store.load();
        suite.expect(loaded.status == PreferenceLoadStatus::Loaded,
                     "a settings file this feature wrote must reload as Loaded");
        suite.expect(loaded.preferences.recent_projects == list,
                     "the recent list must round-trip element-wise equal");
        suite.expect(loaded.preferences.default_curve == CurvePreset::EaseOut,
                     "default_curve must survive a recent-list write");

        std::vector<fs::path> reloaded = loaded.preferences.recent_projects;
        suite.expect(normalize_recent_paths(&reloaded) == false,
                     "a list that was normalized before the write must reload already normalized");
    }
}

} // namespace

int main() {
    TestSuite suite;
    suite.run("first run defaults without filesystem writes", [&] {
        test_first_run(suite);
    });
    suite.run("curve tokens and raw recent paths round-trip", [&] {
        test_curve_tokens_and_raw_recent_paths(suite);
    });
    suite.run("optional field fallbacks and additive preservation", [&] {
        test_optional_fallbacks_and_unknown_fields(suite);
    });
    suite.run("malformed, version, recovery, and path statuses", [&] {
        test_malformed_and_version_statuses(suite);
    });
    suite.run("pure macOS, Linux, and Windows path resolution", [&] {
        test_pure_path_resolution(suite);
    });
    suite.run("process environment priority and restoration", [&] {
        test_process_environment_resolution_and_restoration(suite);
    });
    suite.run("atomic rename failure preserves old bytes", [&] {
        test_atomic_rename_failure(suite);
    });
    suite.run("PreferenceStore remains isolated from EditorSession", [&] {
        test_editor_session_isolation(suite);
    });
    suite.run("fixed curve preset constants, identity, and well-posedness", [&] {
        test_curve_preset_constants(suite);
    });
    suite.run("preset tokens agree with the settings-file vocabulary", [&] {
        test_curve_preset_tokens_match_settings_tokens(suite);
    });
    suite.run("shell preference session load, save, fallback, and preservation", [&] {
        test_shell_preference_session(suite);
    });
    suite.run("recent project list algebra and isolated round trip", [&] {
        test_recent_project_list_algebra(suite);
    });
    return suite.finish();
}
