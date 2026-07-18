#include <chrono>
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

#include "marrow/editor/preferences.hpp"
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
    const int result = value.has_value()
        ? ::setenv(name, value->c_str(), 1)
        : ::unsetenv(name);
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

    PreferenceEnvironment environment;
    environment.marrow_config_home = "/override/config";
    environment.home = "/users/test";
    environment.xdg_config_home = "/xdg/config";

    auto result = resolve_preference_settings_path(PreferencePlatform::MacOS, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         fs::path("/override/config/editor-settings.json"),
                 "MARROW_CONFIG_HOME should take priority on macOS");
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         fs::path("/override/config/editor-settings.json"),
                 "MARROW_CONFIG_HOME should take priority on Linux");

    environment.marrow_config_home.reset();
    result = resolve_preference_settings_path(PreferencePlatform::MacOS, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         fs::path("/users/test/Library/Application Support/Marrow/editor-settings.json"),
                 "macOS should resolve settings below HOME Application Support");
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         fs::path("/xdg/config/marrow/editor-settings.json"),
                 "Linux should prefer an absolute XDG_CONFIG_HOME");

    environment.xdg_config_home.reset();
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         fs::path("/users/test/.config/marrow/editor-settings.json"),
                 "Linux should fall back to HOME/.config");

    environment.marrow_config_home = "";
    environment.xdg_config_home = "";
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         fs::path("/users/test/.config/marrow/editor-settings.json"),
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

    environment.xdg_config_home = "/xdg/without-home";
    environment.home.reset();
    result = resolve_preference_settings_path(PreferencePlatform::Linux, environment);
    suite.expect(static_cast<bool>(result) &&
                     result.settings_path ==
                         fs::path("/xdg/without-home/marrow/editor-settings.json"),
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
#else
#error "PreferenceStore environment tests support macOS and Linux only."
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
    suite.run("pure macOS and Linux path resolution", [&] {
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
    return suite.finish();
}
