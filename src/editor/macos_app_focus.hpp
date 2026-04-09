#pragma once

namespace marrow::editor::platform {

#if defined(__APPLE__)
bool activate_editor_application();
bool uses_regular_activation_policy();
#else
inline bool activate_editor_application() {
    return true;
}

inline bool uses_regular_activation_policy() {
    return true;
}
#endif

} // namespace marrow::editor::platform
