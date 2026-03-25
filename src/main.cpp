#include <iostream>

#include "marrow/editor/module.hpp"
#include "marrow/runtime/module.hpp"

int main() {
    std::cout << "Bootstrapped " << marrow::runtime::component_name()
              << " and " << marrow::editor::component_name() << ".\n";
    return 0;
}
