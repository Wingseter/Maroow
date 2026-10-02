"""Mutation tests for the build-time shared shell frame wiring guard.

Each case runs the real CMake guard against an isolated source fixture. These
prove missing/duplicated delegation cannot be hidden by comments, strings, or
later single-panel smoke scenarios; they are not a second window-name oracle.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = "src/editor/shell_main.cpp"
SMOKE = "src/editor/shell_smoke_frames.cpp"
FRAME = "src/editor/shell_frame.cpp"
CALLS = {
    APP: "draw_shell_frame(*shell_state, delta_time);",
    SMOKE: "draw_shell_frame(shell_state, io.DeltaTime);",
}


class ShellFrameBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="marrow-frame-guard-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for relative in (APP, SMOKE, FRAME, "cmake/CheckFrameBodies.cmake"):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)

    def replace(self, relative, old, new):
        path = self.root / relative
        text = path.read_text(encoding="utf-8")
        self.assertEqual(text.count(old), 1, f"fixture anchor must be unique: {old}")
        path.write_text(text.replace(old, new), encoding="utf-8")

    def guard(self):
        result = subprocess.run(
            ["cmake", "-P", str(self.root / "cmake/CheckFrameBodies.cmake")],
            cwd=self.root, capture_output=True, text=True, timeout=15,
        )
        return result.returncode, result.stdout + result.stderr

    def test_current_hosts_delegate_to_production(self):
        code, output = self.guard()
        self.assertEqual(code, 0, output)
        self.assertIn("shared frame wiring", output)

    def test_missing_delegation_in_either_host_is_rejected(self):
        for relative, call in CALLS.items():
            with self.subTest(host=relative):
                self.replace(relative, call, "/* removed delegation */")
                code, output = self.guard()
                self.assertNotEqual(code, 0, output)
                self.assertIn("exactly one draw_shell_frame", output)
                self.replace(relative, "/* removed delegation */", call)

    def test_duplicate_delegation_is_rejected(self):
        for relative, call in CALLS.items():
            with self.subTest(host=relative):
                self.replace(relative, call, call + "\n" + call)
                code, output = self.guard()
                self.assertNotEqual(code, 0, output)
                self.assertIn("exactly one draw_shell_frame", output)
                self.replace(relative, call + "\n" + call, call)

    def test_comments_and_strings_cannot_supply_delegation(self):
        for fake in ("// {call}", "/* {call} */", 'const char* unused = "{call}";'):
            with self.subTest(fake=fake):
                call = CALLS[APP]
                replacement = fake.format(call=call)
                self.replace(APP, call, replacement)
                code, output = self.guard()
                self.assertNotEqual(code, 0, output)
                self.assertIn("exactly one draw_shell_frame", output)
                self.replace(APP, replacement, call)

    def test_direct_window_composition_in_either_host_is_rejected(self):
        for relative, call in CALLS.items():
            with self.subTest(host=relative):
                replacement = call + "\ndraw_problems_window(nullptr);"
                self.replace(relative, call, replacement)
                code, output = self.guard()
                self.assertNotEqual(code, 0, output)
                self.assertIn("direct composition", output)
                self.replace(relative, replacement, call)

    def test_later_scenario_cannot_supply_shared_smoke_delegation(self):
        self.replace(SMOKE, CALLS[SMOKE], "/* missing shared call */")
        self.replace(SMOKE, "if (!validated_dock_layout) {",
                     "if (!validated_dock_layout) {\n" + CALLS[SMOKE])
        code, output = self.guard()
        self.assertNotEqual(code, 0, output)
        self.assertIn("exactly one draw_shell_frame", output)

    def test_empty_coordinator_is_rejected(self):
        (self.root / FRAME).write_text(
            "void draw_shell_frame(ShellState&, double) {}\n", encoding="utf-8")
        code, output = self.guard()
        self.assertNotEqual(code, 0, output)
        self.assertIn("no window composition", output)

    def test_frame_lifecycle_stays_in_hosts(self):
        for illegal in ("ImGui::NewFrame();", "ImGui::Render();", "sg_begin_pass(nullptr);"):
            with self.subTest(illegal=illegal):
                anchor = "void draw_shell_frame(ShellState& state, double elapsed_seconds) {"
                self.replace(FRAME, anchor, anchor + "\n" + illegal)
                code, output = self.guard()
                self.assertNotEqual(code, 0, output)
                self.assertIn("host lifecycle", output)
                self.replace(FRAME, anchor + "\n" + illegal, anchor)


if __name__ == "__main__":
    unittest.main()
