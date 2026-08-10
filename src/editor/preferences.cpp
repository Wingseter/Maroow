#include "marrow/editor/preferences.hpp"

#include "preferences_internal.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif

namespace marrow::editor {

namespace {

using runtime::json::Value;

struct FileReadResult {
    bool exists{false};
    std::string text;
    std::string error;
};

std::mutex g_rename_callback_mutex;
detail::RenameCallback g_rename_callback;

#if defined(_WIN32)
std::optional<std::string> environment_value(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0U);
    if (required == 0U) {
        return std::nullopt;
    }
    std::vector<wchar_t> wide_value(required, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        name,
        wide_value.data(),
        static_cast<DWORD>(wide_value.size()));
    if (length == 0U || length >= wide_value.size()) {
        return std::nullopt;
    }
    const int utf8_length = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wide_value.data(),
        static_cast<int>(length),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (utf8_length <= 0) {
        return std::nullopt;
    }
    std::string utf8_value(static_cast<std::size_t>(utf8_length), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide_value.data(),
            static_cast<int>(length),
            utf8_value.data(),
            utf8_length,
            nullptr,
            nullptr) != utf8_length) {
        return std::nullopt;
    }
    return utf8_value;
}
#else
std::optional<std::string> environment_value(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}
#endif

detail::PreferenceEnvironment process_environment() {
    detail::PreferenceEnvironment environment;
#if defined(_WIN32)
    environment.marrow_config_home = environment_value(L"MARROW_CONFIG_HOME");
#else
    environment.marrow_config_home = environment_value("MARROW_CONFIG_HOME");
    environment.home = environment_value("HOME");
    environment.xdg_config_home = environment_value("XDG_CONFIG_HOME");
#endif
#if defined(_WIN32)
    PWSTR roaming_path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_RoamingAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &roaming_path)) && roaming_path != nullptr) {
        environment.roaming_app_data = std::filesystem::path(roaming_path);
    }
    if (roaming_path != nullptr) {
        CoTaskMemFree(roaming_path);
    }
#endif
    return environment;
}

detail::PreferencePlatform production_platform() {
#if defined(__APPLE__)
    return detail::PreferencePlatform::MacOS;
#elif defined(__linux__)
    return detail::PreferencePlatform::Linux;
#elif defined(_WIN32)
    return detail::PreferencePlatform::Windows;
#else
#error "PreferenceStore production path is not defined for this platform."
#endif
}

std::string_view curve_preset_token(CurvePreset preset) {
    switch (preset) {
    case CurvePreset::Linear:
        return "linear";
    case CurvePreset::Stepped:
        return "stepped";
    case CurvePreset::Ease:
        return "ease";
    case CurvePreset::EaseIn:
        return "ease_in";
    case CurvePreset::EaseOut:
        return "ease_out";
    case CurvePreset::EaseInOut:
        return "ease_in_out";
    }
    return "linear";
}

std::optional<CurvePreset> parse_curve_preset(std::string_view token) {
    if (token == "linear") {
        return CurvePreset::Linear;
    }
    if (token == "stepped") {
        return CurvePreset::Stepped;
    }
    if (token == "ease") {
        return CurvePreset::Ease;
    }
    if (token == "ease_in") {
        return CurvePreset::EaseIn;
    }
    if (token == "ease_out") {
        return CurvePreset::EaseOut;
    }
    if (token == "ease_in_out") {
        return CurvePreset::EaseInOut;
    }
    return std::nullopt;
}

bool is_exact_integer(double value) {
    return std::isfinite(value) && std::floor(value) == value;
}

std::string format_number(double value) {
    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return stream.str();
}

FileReadResult read_existing_file(const std::filesystem::path& path) {
    FileReadResult result;
    std::error_code filesystem_error;
    result.exists = std::filesystem::exists(path, filesystem_error);
    if (filesystem_error) {
        result.error = "failed to inspect settings file: " + filesystem_error.message();
        return result;
    }
    if (!result.exists) {
        return result;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.error = "failed to open settings file for reading";
        return result;
    }

    std::array<char, 8192U> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        result.text.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
    }
    if (input.bad() || (!input.eof() && input.fail())) {
        result.error = "failed to read settings file";
        return result;
    }
    return result;
}

void append_diagnostic(std::string* diagnostic, std::string message) {
    if (!diagnostic->empty()) {
        *diagnostic += "; ";
    }
    *diagnostic += std::move(message);
}

PreferenceLoadResult parse_preferences(
    std::string_view text,
    const std::filesystem::path& path) {
    PreferenceLoadResult result;
    result.path = path;

    const auto parsed = runtime::json::parse_document(text, path);
    if (!parsed) {
        result.status = PreferenceLoadStatus::Malformed;
        result.diagnostic = parsed.error.has_value()
            ? parsed.error->format()
            : "settings file contains malformed JSON";
        return result;
    }

    const Value& root = parsed.document->root;
    if (!root.is_object()) {
        result.status = PreferenceLoadStatus::Malformed;
        result.diagnostic = "settings document root must be an object";
        return result;
    }

    const Value* version = runtime::json::find_member(root, "version");
    if (version == nullptr || !version->is_number() ||
        !is_exact_integer(version->as_number())) {
        result.status = PreferenceLoadStatus::Malformed;
        result.diagnostic = "settings version must be the required integer 1";
        return result;
    }
    if (version->as_number() != static_cast<double>(kEditorSettingsVersion)) {
        result.status = PreferenceLoadStatus::UnsupportedVersion;
        result.diagnostic =
            "settings version " + format_number(version->as_number()) +
            " is not supported by this editor";
        return result;
    }

    result.preferences.preserved_root = root;
    bool used_defaults = false;

    const Value* curve = runtime::json::find_member(root, "default_curve");
    if (curve == nullptr) {
        used_defaults = true;
        append_diagnostic(&result.diagnostic, "default_curve is missing; using linear");
    } else if (!curve->is_string()) {
        used_defaults = true;
        append_diagnostic(&result.diagnostic, "default_curve is not a string; using linear");
    } else if (const auto preset = parse_curve_preset(curve->as_string())) {
        result.preferences.default_curve = *preset;
    } else {
        used_defaults = true;
        append_diagnostic(&result.diagnostic, "default_curve token is invalid; using linear");
    }

    const Value* recent = runtime::json::find_member(root, "recent_projects");
    if (recent == nullptr) {
        used_defaults = true;
        append_diagnostic(&result.diagnostic, "recent_projects is missing; using an empty list");
    } else if (!recent->is_array()) {
        used_defaults = true;
        append_diagnostic(
            &result.diagnostic,
            "recent_projects is not an array; using an empty list");
    } else {
        bool skipped_invalid_entry = false;
        for (const Value& entry : recent->as_array()) {
            if (!entry.is_string() || entry.as_string().empty()) {
                used_defaults = true;
                skipped_invalid_entry = true;
                continue;
            }
            result.preferences.recent_projects.emplace_back(
                std::filesystem::u8path(entry.as_string()));
        }
        if (skipped_invalid_entry) {
            append_diagnostic(
                &result.diagnostic,
                "recent_projects contains invalid entries; skipped them");
        }
    }

    result.status = used_defaults
        ? PreferenceLoadStatus::LoadedWithDefaults
        : PreferenceLoadStatus::Loaded;
    return result;
}

Value build_preferences_root(
    const EditorPreferences& preferences,
    const Value* disk_root) {
    Value::Object root;
    if (disk_root != nullptr && disk_root->is_object()) {
        root = disk_root->as_object();
    } else if (preferences.preserved_root.is_object()) {
        root = preferences.preserved_root.as_object();
    }

    root["version"] = Value(static_cast<double>(kEditorSettingsVersion), {});
    root["default_curve"] = Value(std::string(curve_preset_token(preferences.default_curve)), {});

    Value::Array recent_projects;
    recent_projects.reserve(preferences.recent_projects.size());
    for (const std::filesystem::path& path : preferences.recent_projects) {
        if (path.empty()) {
            continue;
        }
        recent_projects.emplace_back(path.u8string(), runtime::json::SourceLocation{});
    }
    root["recent_projects"] = Value(std::move(recent_projects), {});
    return Value(std::move(root), {});
}

std::error_code production_rename(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
#if defined(_WIN32)
    if (MoveFileExW(
            source.c_str(),
            destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        return {};
    }
    return std::error_code(
        static_cast<int>(GetLastError()),
        std::system_category());
#else
    if (::rename(source.c_str(), destination.c_str()) == 0) {
        return {};
    }
    return std::error_code(errno, std::generic_category());
#endif
}

detail::RenameCallback rename_callback() {
    std::lock_guard<std::mutex> lock(g_rename_callback_mutex);
    return g_rename_callback;
}

PreferenceSaveResult write_atomically(
    const std::filesystem::path& destination,
    std::string_view text) {
    PreferenceSaveResult result;
    result.path = destination;

    const std::filesystem::path parent = destination.parent_path().empty()
        ? std::filesystem::path(".")
        : destination.parent_path();
    std::error_code filesystem_error;
    std::filesystem::create_directories(parent, filesystem_error);
    if (filesystem_error) {
        result.error = "failed to create settings directory: " + filesystem_error.message();
        return result;
    }

#if defined(_WIN32)
    static std::atomic<unsigned long> temporary_sequence{0UL};
    HANDLE output = INVALID_HANDLE_VALUE;
    std::filesystem::path temporary_path;
    for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
        const unsigned long sequence = temporary_sequence.fetch_add(1UL);
        temporary_path = parent /
            (destination.filename().wstring() + L".tmp." +
             std::to_wstring(GetCurrentProcessId()) + L"." +
             std::to_wstring(sequence));
        output = CreateFileW(
            temporary_path.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY,
            nullptr);
        if (output != INVALID_HANDLE_VALUE) {
            break;
        }
        const DWORD create_error = GetLastError();
        if (create_error != ERROR_FILE_EXISTS &&
            create_error != ERROR_ALREADY_EXISTS) {
            break;
        }
    }
    if (output == INVALID_HANDLE_VALUE) {
        result.error = "failed to create temporary settings file: " +
            std::error_code(
                static_cast<int>(GetLastError()),
                std::system_category()).message();
        return result;
    }
#else
    std::string temporary_template =
        (parent / (destination.filename().string() + ".tmp.XXXXXX")).string();
    std::vector<char> temporary_buffer(temporary_template.begin(), temporary_template.end());
    temporary_buffer.push_back('\0');

    const int descriptor = ::mkstemp(temporary_buffer.data());
    if (descriptor < 0) {
        result.error = "failed to create temporary settings file: " +
            std::error_code(errno, std::generic_category()).message();
        return result;
    }
    const std::filesystem::path temporary_path(temporary_buffer.data());
#endif

    const auto cleanup_temporary = [&temporary_path]() {
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
    };

#if defined(_WIN32)
    std::size_t write_offset = 0U;
    while (write_offset < text.size()) {
        const std::size_t remaining = text.size() - write_offset;
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0U;
        if (WriteFile(
                output,
                text.data() + write_offset,
                requested,
                &written,
                nullptr) == 0 ||
            written == 0U) {
            const std::error_code write_error(
                static_cast<int>(GetLastError()),
                std::system_category());
            CloseHandle(output);
            cleanup_temporary();
            result.error = "failed to write temporary settings file: " +
                write_error.message();
            return result;
        }
        write_offset += static_cast<std::size_t>(written);
    }
    if (FlushFileBuffers(output) == 0) {
        const std::error_code flush_error(
            static_cast<int>(GetLastError()),
            std::system_category());
        CloseHandle(output);
        cleanup_temporary();
        result.error = "failed to flush temporary settings file: " +
            flush_error.message();
        return result;
    }
    if (CloseHandle(output) == 0) {
        const std::error_code close_error(
            static_cast<int>(GetLastError()),
            std::system_category());
        cleanup_temporary();
        result.error = "failed to close temporary settings file: " +
            close_error.message();
        return result;
    }
#else
    std::FILE* output = ::fdopen(descriptor, "wb");
    if (output == nullptr) {
        const std::error_code open_error(errno, std::generic_category());
        ::close(descriptor);
        cleanup_temporary();
        result.error = "failed to open temporary settings stream: " + open_error.message();
        return result;
    }

    const std::size_t written =
        text.empty() ? 0U : std::fwrite(text.data(), 1U, text.size(), output);
    if (written != text.size() || std::ferror(output) != 0) {
        const int write_errno = errno;
        std::fclose(output);
        cleanup_temporary();
        result.error = "failed to write temporary settings file";
        if (write_errno != 0) {
            result.error += ": " +
                std::error_code(write_errno, std::generic_category()).message();
        }
        return result;
    }
    if (std::fflush(output) != 0) {
        const std::error_code flush_error(errno, std::generic_category());
        std::fclose(output);
        cleanup_temporary();
        result.error = "failed to flush temporary settings file: " + flush_error.message();
        return result;
    }
    if (std::fclose(output) != 0) {
        const std::error_code close_error(errno, std::generic_category());
        cleanup_temporary();
        result.error = "failed to close temporary settings file: " + close_error.message();
        return result;
    }
#endif

    const detail::RenameCallback callback = rename_callback();
    const std::error_code rename_error = callback
        ? callback(temporary_path, destination)
        : production_rename(temporary_path, destination);
    if (rename_error) {
        cleanup_temporary();
        result.error = "failed to atomically replace settings file: " + rename_error.message();
        return result;
    }

    return result;
}

} // namespace

namespace detail {

PreferencePathResult resolve_preference_settings_path(
    PreferencePlatform platform,
    const PreferenceEnvironment& environment) {
    const auto non_empty = [platform](const std::optional<std::string>& value)
        -> std::optional<std::filesystem::path> {
        if (!value.has_value() || value->empty()) {
            return std::nullopt;
        }
        if (platform == PreferencePlatform::Windows) {
            return std::filesystem::u8path(*value);
        }
        return std::filesystem::path(*value);
    };

    if (const auto override_home = non_empty(environment.marrow_config_home)) {
        if (!override_home->is_absolute()) {
            return {{}, "MARROW_CONFIG_HOME must be an absolute path"};
        }
        return {*override_home / "editor-settings.json", {}};
    }

    if (platform == PreferencePlatform::Linux) {
        if (const auto xdg_home = non_empty(environment.xdg_config_home)) {
            if (!xdg_home->is_absolute()) {
                return {{}, "XDG_CONFIG_HOME must be an absolute path"};
            }
            return {*xdg_home / "marrow" / "editor-settings.json", {}};
        }
    }

    if (platform == PreferencePlatform::Windows) {
        if (!environment.roaming_app_data.has_value() ||
            environment.roaming_app_data->empty()) {
            return {{}, "Roaming AppData is required to resolve the editor settings path"};
        }
        if (!environment.roaming_app_data->is_absolute()) {
            return {{}, "Roaming AppData must be an absolute path"};
        }
        return {
            *environment.roaming_app_data / "Marrow" / "editor-settings.json",
            {}};
    }

    const auto home = non_empty(environment.home);
    if (!home.has_value()) {
        return {{}, "HOME is required to resolve the editor settings path"};
    }
    if (!home->is_absolute()) {
        return {{}, "HOME must be an absolute path"};
    }

    if (platform == PreferencePlatform::MacOS) {
        return {
            *home / "Library" / "Application Support" / "Marrow" /
                "editor-settings.json",
            {}};
    }
    return {*home / ".config" / "marrow" / "editor-settings.json", {}};
}

void set_preference_rename_callback_for_testing(RenameCallback callback) {
    std::lock_guard<std::mutex> lock(g_rename_callback_mutex);
    g_rename_callback = std::move(callback);
}

} // namespace detail

PreferenceStore::PreferenceStore() {
    const detail::PreferencePathResult resolved =
        detail::resolve_preference_settings_path(production_platform(), process_environment());
    settings_path_ = resolved.settings_path;
    path_error_ = resolved.error;
}

PreferenceStore::PreferenceStore(std::filesystem::path settings_path)
    : settings_path_(std::move(settings_path)) {
    if (settings_path_.empty()) {
        path_error_ = "settings path must not be empty";
    }
}

const std::filesystem::path& PreferenceStore::settings_path() const noexcept {
    return settings_path_;
}

PreferenceLoadResult PreferenceStore::load() const {
    PreferenceLoadResult result;
    result.path = settings_path_;
    if (!path_error_.empty()) {
        result.status = PreferenceLoadStatus::IoError;
        result.diagnostic = path_error_;
        return result;
    }

    const FileReadResult file = read_existing_file(settings_path_);
    if (!file.error.empty()) {
        result.status = PreferenceLoadStatus::IoError;
        result.diagnostic = file.error;
        return result;
    }
    if (!file.exists) {
        result.status = PreferenceLoadStatus::FirstRun;
        return result;
    }
    return parse_preferences(file.text, settings_path_);
}

PreferenceSaveResult PreferenceStore::save(const EditorPreferences& preferences) const {
    PreferenceSaveResult result;
    result.path = settings_path_;
    if (!path_error_.empty()) {
        result.error = path_error_;
        return result;
    }

    Value disk_root;
    const Value* preservation_root = nullptr;
    const FileReadResult file = read_existing_file(settings_path_);
    if (!file.error.empty()) {
        result.error = file.error;
        return result;
    }
    if (file.exists) {
        const auto parsed = runtime::json::parse_document(file.text, settings_path_);
        if (parsed && parsed.document->root.is_object()) {
            const Value* version =
                runtime::json::find_member(parsed.document->root, "version");
            if (version != nullptr && version->is_number() &&
                is_exact_integer(version->as_number()) &&
                version->as_number() != static_cast<double>(kEditorSettingsVersion)) {
                result.error = "refusing to overwrite unsupported settings version " +
                    format_number(version->as_number());
                return result;
            }
            if (version != nullptr && version->is_number() &&
                is_exact_integer(version->as_number()) &&
                version->as_number() == static_cast<double>(kEditorSettingsVersion)) {
                disk_root = parsed.document->root;
                preservation_root = &disk_root;
            }
        }
        // Readable malformed documents are intentionally recoverable by an
        // explicit user-triggered save. Only supported v1 roots preserve data.
    }

    const Value root = build_preferences_root(preferences, preservation_root);
    return write_atomically(
        settings_path_,
        runtime::json::serialize_pretty_round_trip(root));
}

} // namespace marrow::editor
