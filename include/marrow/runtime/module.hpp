#pragma once

#include "marrow/allocator.hpp"

#include <string_view>

namespace marrow::runtime {

/// @brief Returns the runtime module name used by bootstrap and sample diagnostics.
std::string_view component_name();

} // namespace marrow::runtime
