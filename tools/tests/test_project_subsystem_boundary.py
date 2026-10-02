"""Narrow source/build ownership guards, not a C++ semantic parser.

Behavior is exercised by project_subsystem_tests and the existing project,
parameter, C ABI and authoring smokes. These guards catch accidental re-merges,
.cpp inclusion, UI coupling and filesystem orchestration in the parse layer.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
EDITOR = ROOT / "src" / "editor"
MODULES = (
    "project_json", "project_validation", "project_serialize", "project_overlay",
    "project_runtime", "project_parse", "project_io",
)


def code_only(text):
    # Retain string literals (include paths and runtime names are useful here).
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)


class ProjectSubsystemBoundaryTests(unittest.TestCase):
    def test_separate_translation_units_are_built_once(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        library = cmake.split("add_library(marrow_editor STATIC", 1)[1].split(")", 1)[0]
        for module in MODULES:
            with self.subTest(module=module):
                self.assertTrue((EDITOR / (module + ".cpp")).is_file(),
                                "missing responsibility boundary: " + module)
                self.assertEqual(library.count("src/editor/" + module + ".cpp"), 1)

    def test_private_contracts_are_not_public_api(self):
        for name in ("project_json.hpp", "project_internal.hpp"):
            with self.subTest(header=name):
                self.assertTrue((EDITOR / name).is_file(), "missing private contract " + name)
                self.assertFalse((ROOT / "include/marrow/editor" / name).exists())

    def test_no_cpp_includes_or_ui_dependencies(self):
        for module in ("project",) + MODULES:
            path = EDITOR / (module + ".cpp")
            with self.subTest(module=module):
                self.assertTrue(path.is_file(), "missing module " + module)
                code = code_only(path.read_text(encoding="utf-8"))
                self.assertNotRegex(code, r'#\s*include\s*[<"][^>"\n]*\.cpp[>"]')
                self.assertNotRegex(code, r'#\s*include\s*[<"][^>"\n]*(?:shell_|imgui|SDL|sokol)')

    def test_filesystem_orchestration_stays_out_of_pure_layers(self):
        for module in ("project_parse", "project_serialize", "project_validation", "project_overlay", "project_runtime", "project_json"):
            path = EDITOR / (module + ".cpp")
            with self.subTest(module=module):
                self.assertTrue(path.is_file(), "missing module " + module)
                code = code_only(path.read_text(encoding="utf-8"))
                self.assertNotRegex(code, r'\b(?:ifstream|ofstream|fstream|write_file_atomically|load_skeleton_document|load_document|export_runtime_assets)\b')
                self.assertNotIn("AtlasLoader::load", code)
                self.assertNotIn('"atlas_packer.hpp"', code)

    def test_model_no_longer_owns_io_parse_or_materialization(self):
        code = code_only((EDITOR / "project.cpp").read_text(encoding="utf-8"))
        for symbol in ("parse_runtime_assets", "build_project_value", "build_runtime_document", "write_text_file"):
            with self.subTest(symbol=symbol):
                self.assertFalse(symbol + "(" in code, "model still owns " + symbol)


if __name__ == "__main__":
    unittest.main()
