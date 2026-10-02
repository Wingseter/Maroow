#include "marrow/editor/recent_projects.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace marrow::editor {

namespace fs = std::filesystem;

fs::path canonical_recent_path(const fs::path& path) {
    if (path.empty()) {
        // Not the CWD. An empty entry is a malformed entry, and `absolute("")`
        // would silently turn it into a real directory the user never named.
        return {};
    }
    std::error_code error;
    fs::path resolved = fs::weakly_canonical(path, error);
    if (!error && !resolved.empty()) {
        return resolved;
    }
    std::error_code fallback_error;
    resolved = fs::absolute(path, fallback_error);
    if (!fallback_error && !resolved.empty()) {
        return resolved.lexically_normal();
    }
    // Degraded, never rejected: an unresolvable entry still names something the
    // user chose, and AC4's whole point is that the list outlives the files.
    return path;
}

bool recent_project_exists(const fs::path& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    // `is_regular_file`, not `exists`: a directory is not a project, and the
    // error overload reports false for a dead mount rather than throwing.
    return fs::is_regular_file(path, error) && !error;
}

bool promote_recent_project(
    std::vector<fs::path>* list, const fs::path& path) {
    if (list == nullptr) {
        return false;
    }
    const fs::path canonical = canonical_recent_path(path);
    if (canonical.empty()) {
        return false;
    }

    const std::vector<fs::path> before = *list;

    // Erase every equal entry first, so the insert below cannot create a
    // duplicate of the head it is about to become.
    list->erase(
        std::remove(list->begin(), list->end(), canonical), list->end());
    list->insert(list->begin(), canonical);
    // INSERT BEFORE TRUNCATE. Truncating first would evict the tail to make room
    // and then push the list back over the bound, and at exactly the bound it
    // would drop the entry the user just opened.
    if (list->size() > kRecentProjectLimit) {
        list->resize(kRecentProjectLimit);
    }

    return *list != before;
}

bool forget_recent_path(std::vector<fs::path>* list, const fs::path& path) {
    if (list == nullptr) {
        return false;
    }
    const fs::path canonical = canonical_recent_path(path);
    if (canonical.empty()) {
        return false;
    }
    const auto removed =
        std::remove(list->begin(), list->end(), canonical);
    if (removed == list->end()) {
        return false;
    }
    list->erase(removed, list->end());
    return true;
}

bool drop_missing_recent_paths(std::vector<fs::path>* list) {
    if (list == nullptr) {
        return false;
    }
    const auto removed = std::remove_if(
        list->begin(), list->end(), [](const fs::path& entry) {
            return !recent_project_exists(entry);
        });
    if (removed == list->end()) {
        return false;
    }
    list->erase(removed, list->end());
    return true;
}

bool normalize_recent_paths(std::vector<fs::path>* list) {
    if (list == nullptr) {
        return false;
    }
    const std::vector<fs::path> before = *list;

    std::vector<fs::path> normalized;
    normalized.reserve(std::min(list->size(), kRecentProjectLimit));
    for (const fs::path& entry : *list) {
        const fs::path canonical = canonical_recent_path(entry);
        if (canonical.empty()) {
            continue;
        }
        // Keep the FIRST occurrence, at its original position. Keeping the last
        // would reorder a list the user has already seen, for no gain.
        if (std::find(normalized.begin(), normalized.end(), canonical) !=
            normalized.end()) {
            continue;
        }
        normalized.push_back(canonical);
        if (normalized.size() == kRecentProjectLimit) {
            break;
        }
    }

    *list = std::move(normalized);
    return *list != before;
}

} // namespace marrow::editor
