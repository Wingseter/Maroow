#pragma once

namespace marrow::editor::shell {

struct ShellState;

// Execute one production shell frame inside an already-begun ImGui frame.
// The host owns frame begin/end, input, surface acquisition and presentation.
// elapsed_seconds is host wall time for asset watching; playback uses ImGui's
// DeltaTime, including the backend's clamping. Call exactly once per frame.
void draw_shell_frame(ShellState& state, double elapsed_seconds);

} // namespace marrow::editor::shell
