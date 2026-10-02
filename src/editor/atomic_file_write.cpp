#include "atomic_file_write.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace marrow::editor::detail {

namespace {

std::mutex g_rename_callback_mutex;
RenameCallback g_rename_callback;

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

RenameCallback rename_callback() {
    std::lock_guard<std::mutex> lock(g_rename_callback_mutex);
    return g_rename_callback;
}

} // namespace

std::string write_file_atomically(
    const std::filesystem::path& destination,
    std::string_view text,
    std::string_view subject) {
    std::string error;
    const std::string subject_text(subject);

    const std::filesystem::path parent = destination.parent_path().empty()
        ? std::filesystem::path(".")
        : destination.parent_path();
    std::error_code filesystem_error;
    std::filesystem::create_directories(parent, filesystem_error);
    if (filesystem_error) {
        error = "failed to create " + subject_text + " directory: " +
            filesystem_error.message();
        return error;
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
        error = "failed to create temporary " + subject_text + " file: " +
            std::error_code(
                static_cast<int>(GetLastError()),
                std::system_category()).message();
        return error;
    }
#else
    std::string temporary_template =
        (parent / (destination.filename().string() + ".tmp.XXXXXX")).string();
    std::vector<char> temporary_buffer(temporary_template.begin(), temporary_template.end());
    temporary_buffer.push_back('\0');

    const int descriptor = ::mkstemp(temporary_buffer.data());
    if (descriptor < 0) {
        error = "failed to create temporary " + subject_text + " file: " +
            std::error_code(errno, std::generic_category()).message();
        return error;
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
            error = "failed to write temporary " + subject_text + " file: " +
                write_error.message();
            return error;
        }
        write_offset += static_cast<std::size_t>(written);
    }
    if (FlushFileBuffers(output) == 0) {
        const std::error_code flush_error(
            static_cast<int>(GetLastError()),
            std::system_category());
        CloseHandle(output);
        cleanup_temporary();
        error = "failed to flush temporary " + subject_text + " file: " +
            flush_error.message();
        return error;
    }
    if (CloseHandle(output) == 0) {
        const std::error_code close_error(
            static_cast<int>(GetLastError()),
            std::system_category());
        cleanup_temporary();
        error = "failed to close temporary " + subject_text + " file: " +
            close_error.message();
        return error;
    }
#else
    std::FILE* output = ::fdopen(descriptor, "wb");
    if (output == nullptr) {
        const std::error_code open_error(errno, std::generic_category());
        ::close(descriptor);
        cleanup_temporary();
        error = "failed to open temporary " + subject_text + " stream: " +
            open_error.message();
        return error;
    }

    const std::size_t written =
        text.empty() ? 0U : std::fwrite(text.data(), 1U, text.size(), output);
    if (written != text.size() || std::ferror(output) != 0) {
        const int write_errno = errno;
        std::fclose(output);
        cleanup_temporary();
        error = "failed to write temporary " + subject_text + " file";
        if (write_errno != 0) {
            error += ": " +
                std::error_code(write_errno, std::generic_category()).message();
        }
        return error;
    }
    if (std::fflush(output) != 0) {
        const std::error_code flush_error(errno, std::generic_category());
        std::fclose(output);
        cleanup_temporary();
        error = "failed to flush temporary " + subject_text + " file: " +
            flush_error.message();
        return error;
    }
    if (std::fclose(output) != 0) {
        const std::error_code close_error(errno, std::generic_category());
        cleanup_temporary();
        error = "failed to close temporary " + subject_text + " file: " +
            close_error.message();
        return error;
    }
#endif

    const RenameCallback callback = rename_callback();
    const std::error_code rename_error = callback
        ? callback(temporary_path, destination)
        : production_rename(temporary_path, destination);
    if (rename_error) {
        cleanup_temporary();
        error = "failed to atomically replace " + subject_text + " file: " +
            rename_error.message();
        return error;
    }

    return error;
}

void set_preference_rename_callback_for_testing(RenameCallback callback) {
    std::lock_guard<std::mutex> lock(g_rename_callback_mutex);
    g_rename_callback = std::move(callback);
}

} // namespace marrow::editor::detail
