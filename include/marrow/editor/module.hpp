#pragma once

#include <string_view>

namespace marrow::editor {

/// @brief Returns the editor module name used by bootstrap and sample diagnostics.
std::string_view component_name();

} // namespace marrow::editor
