# MAR-174 Transient Preview Playback Speed — Implementation Plan

Design spec: `docs/superpowers/specs/2026-08-30-mar-174-transient-preview-playback-speed-design.md`

- **Story**: MAR-174, depends on MAR-173.
- **Branch**: `feat/mar-168`.
- **Registry effect**: **none**. No Agent operation, no MCP tool, no count change.
- **Size**: small. Six production lines of logic, one setter, one UI row, one smoke
  scenario. The scenario is the bulk of the work and is where the rigor goes.

## Global Constraints

1. **MAR-173 lands first and is being implemented concurrently.** Task 0 reconciles
   with as-built code before any edit. Do not start Task 1 until MAR-173 is merged
   into the working branch, or the registry guard in Task 2 will be written against a
   stale count.
2. **The registry count is measured, never assumed.** Task 0 measures it. It is 59 on
   the tree this plan was written against; MAR-173 takes it to 60. Whatever Task 0
   measures is `N`, and Task 2's guard uses `N`. **MAR-174 does not change `N` and
   does not edit any of the hand-maintained count sites** — not
   `agent_dispatch_smoke.cpp`, not the guards in `shell_smoke_graph.cpp`, not
   `tools/mcp/test_client.py`, not the count prose in `AGENTS.md` or
   `docs/root1/*.md`. If a diff of those files is non-empty at the end, something
   went wrong.
3. **No `include/marrow/**` header changes. No CMake changes.** Both smoke TUs are
   already in the `marrow_editor_shell` target.
4. **Do not touch `~/Library/Application Support/Marrow`.** Every scenario that
   constructs a `ShellState` installs `ScopedPreferenceIsolation` first.
5. **Zero-byte diff gates** (design §13), checked in Task 5: `src/runtime/**`,
   `include/marrow/runtime/**`, `src/c_api/**`, `include/marrow/c_api/**`,
   `src/editor/preferences.cpp`, `include/marrow/editor/preferences.hpp`,
   `include/marrow/editor/session.hpp`, `src/editor/session.cpp`,
   `include/marrow/editor/project.hpp`, `src/editor/project.cpp`,
   `src/editor/agent_dispatch*.cpp`, `tools/mcp/**`.

## File and Responsibility Map

| File | Change |
|---|---|
| `src/editor/shell_state.hpp` | four constants, one `ShellState` field, one inline `preview_playback_speed()` accessor |
| `src/editor/timeline_controller.hpp` | one declaration: `set_preview_playback_speed()` |
| `src/editor/timeline_controller.cpp` | the setter; the multiply in `advance_timeline_playback(ShellState*, double)` |
| `src/editor/shell_core.cpp` | one line in `reload_project()` |
| `src/editor/shell_timeline.cpp` | the transport-row Speed drag + four preset buttons |
| `src/editor/shell_smoke_timeline.cpp` | `validate_preview_playback_speed_shell_smoke()` |
| `src/editor/shell_smoke_scenarios.hpp` | one declaration |
| `src/editor/shell_smoke.cpp` | one call |
| `AGENTS.md`, `docs/root1/{discription,editing-gap-analysis,platform-validation,quick-start}.md` | closure prose (Task 6) |

Nothing else.

---

## Task 0 (mandatory): Reconcile with as-built MAR-172/MAR-173 and measure

No edits in this task. Read and record.

- [ ] Confirm MAR-173 is merged into the working tree:
      ```bash
      git log --oneline -12
      rg -n 'scale_key_times' src/editor/agent_dispatch.cpp
      ```
      If `timeline.scale_key_times` is absent, **stop** and wait. Everything below
      assumes MAR-173's as-built state.

- [ ] **Measure the registry count. Do not assume it.**
      ```bash
      python3 - <<'EOF'
      import re
      src = open('src/editor/agent_dispatch.cpp').read()
      i = src.find('constexpr OperationSpec kOperationSpecs[] = {')
      body = src[i:src.find('};', i)]
      names = re.findall(r'\{"([a-z0-9_.]+)"', body)
      print("N =", len(names))
      print("timeline ops:", [n for n in names if n.startswith('timeline.')])
      EOF
      ```
      Record `N`. Expected 60 after MAR-173 (59 before). **Task 2's guard uses this
      exact number.**

- [ ] Count the existing hand-maintained guard sites so Task 5 can prove MAR-174 did
      not disturb them:
      ```bash
      grep -n "operation_count_before != " src/editor/shell_smoke_graph.cpp
      grep -rn "len(registry_names) ==\|len(mcp_names) ==" tools/mcp/test_client.py
      ```
      Record the count and the number they assert. There were six guards in
      `shell_smoke_graph.cpp` after MAR-172; MAR-173 may add a seventh.

- [ ] Re-read the four load-bearing as-built sites and confirm the design's facts
      still hold:
      - `advance_timeline_playback()` — `src/editor/timeline_controller.cpp:762`.
        Confirm it is still the only progression site:
        `rg -n 'advance_timeline_playback' src/`
        (expect: two declarations, two definitions, and exactly two call sites —
        `shell_main.cpp` and `shell_smoke_frames.cpp`).
      - `PreviewImpl::advance()` — `src/editor/session.cpp:668`. Confirm the
        `delta_seconds <= 0.0` guard, the `std::fmod` loop wrap, the non-loop
        clamp-and-stop, and that `animation_state_->update()` receives the same delta.
      - `capture_history_snapshot()` / `history_snapshots_equal()` —
        `src/editor/shell_core.cpp:122` and `:163`. Confirm `PreviewState` is captured
        and restored but is **not** part of the equality test. This is why speed must
        stay off `PreviewState`.
      - `reload_project()` — `src/editor/shell_core.cpp:531`. Confirm it is still the
        single open/reload/replace entry point and that it still branches on
        `reload_current_project`.

- [ ] Confirm the runtime still has no speed concept:
      ```bash
      rg -n 'time_scale|timeScale|playback_speed' src/runtime include/marrow/runtime
      ```
      Expect no output. If MAR-173 introduced one, re-open design §4 before proceeding.

- [ ] Confirm `idle`'s inferred duration is still exactly `1.0` s — every numeric
      expectation in Task 2 depends on it:
      ```bash
      python3 -c "
      import json; a=json.load(open('assets/fixtures/player_idle.mskl'))['animations']['idle']
      mx=0.0
      def w(o):
          global mx
          if isinstance(o,dict):
              if isinstance(o.get('time'),(int,float)): mx=max(mx,o['time'])
              for v in o.values(): w(v)
          elif isinstance(o,list):
              for v in o: w(v)
      w(a); print('idle duration =', mx)"
      ```

- [ ] Confirm MAR-173 put its scenario in `shell_smoke_graph.cpp` and not in
      `shell_smoke_timeline.cpp`:
      `rg -n 'validate_timeline_scale_shell_smoke' src/editor/`
      MAR-174's scenario goes in `shell_smoke_timeline.cpp` specifically to avoid that
      file. If MAR-173 landed in the timeline TU instead, place MAR-174's scenario at
      the end of that file and re-check for conflicts before editing.

**Deliverable**: `N`, the guard-site list, and a confirmation that all six facts hold.
If any differs from the design spec, update the spec's §3 before writing code.

---

## Task 1: The domain, the setter, and the multiply

### TDD step (RED)

The repo's shell smokes are compiled assert binaries, so a plain compile error is a
weak RED. Use the two-stage RED that the rest of the milestone uses:

- [ ] **Stage 1 — declarations plus an inert stub.** Add to
      `src/editor/shell_state.hpp` the four constants and the `preview_speed` field,
      and add to `timeline_controller.{hpp,cpp}` a `set_preview_playback_speed()` that
      **returns `true` and does nothing else**. Do **not** add the accessor's clamp
      logic and do **not** touch `advance_timeline_playback()` yet.
- [ ] Write Task 2's scenario (below) in full and wire it in.
- [ ] Build and run. It **must fail**, and must fail with a specific message, not a
      crash:
      ```bash
      cmake --build build
      ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
      ```
      Expected first failure: the §11.2 bounds case — `set_preview_playback_speed(0.0)`
      left `preview_speed` at `1.0` instead of clamping to `0.05`. Record the exact
      stderr line. If it passes, the test is not testing anything; fix the test first.

### Implementation step (GREEN)

- [ ] `src/editor/shell_state.hpp`, beside the existing `kOnionSkinFrameRate` group:
      ```cpp
      // MAR-174: transient preview transport speed. Strictly positive; direction
      // is preview_reverse's job (a negative delta is a silent no-op inside
      // PreviewImpl::advance() and drops every event in AnimationState).
      constexpr double kPreviewSpeedMinimum = 0.05;
      constexpr double kPreviewSpeedMaximum = 8.0;
      constexpr double kDefaultPreviewSpeed = 1.0;
      constexpr double kPreviewSpeedPresets[] = {0.25, 0.5, 1.0, 2.0};
      ```
- [ ] Add the field to `ShellState`, immediately after `preview_reverse`, with the
      comment from design §6.1:
      ```cpp
      double preview_speed{kDefaultPreviewSpeed};
      ```
- [ ] Add the inline accessor beside `authoring_gesture_active()`, exactly as design
      §6.1 specifies. It clamps and substitutes `kDefaultPreviewSpeed` for a non-finite
      field, so no corrupted value can reach `EditorSession::advance()`.
- [ ] Implement `set_preview_playback_speed()` in `timeline_controller.cpp` to design
      §6.2's contract table:
      - `!std::isfinite(speed)` → return `false`, write nothing, set no status.
      - otherwise `state->preview_speed = std::clamp(speed, kPreviewSpeedMinimum, kPreviewSpeedMaximum);`
        set `status_message` when `update_status_message` (e.g.
        `"Preview speed 2.00x via Timeline"`, matching the `scrub_timeline_time()`
        source-suffix style), return `true`.
      - **It must call nothing on `EditorSession`.** Any `session.set_*()` here would
        bump `preview_revision` and fail Task 2's §11.6 gate.
- [ ] Add the multiply to `advance_timeline_playback(ShellState*, double)`, verbatim
      from design §8.1, including the `std::isfinite(scaled_delta)` guard. Leave the
      `float` overload alone.
- [ ] Add the reset line to `reload_project()` (`shell_core.cpp`), immediately after
      the existing `state->timeline_playing = false;`:
      ```cpp
      state->preview_speed = kDefaultPreviewSpeed;   // MAR-174
      ```
      Unconditional and above the `reload_current_project` branch, so it covers open,
      reload, and replace with one statement.

### Verification

```bash
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

- [ ] The scenario passes.
- [ ] `git diff --stat -- include/marrow/` is empty.
- [ ] `git diff -- src/runtime src/c_api include/marrow/runtime include/marrow/c_api` is empty.

---

## Task 2: The shell smoke scenario

Written during Task 1's RED stage; this task is where its content is specified.

### File and wiring

- [ ] `bool validate_preview_playback_speed_shell_smoke(const std::filesystem::path& project_path);`
      in `src/editor/shell_smoke_scenarios.hpp`, after
      `validate_timeline_loop_sync_shell_smoke` (and after MAR-173's declaration if it
      landed there).
- [ ] Definition at the end of `src/editor/shell_smoke_timeline.cpp`, inside
      `namespace marrow::editor::shell`, with a `// MAR-174 …` banner comment matching
      the style at `shell_smoke_graph.cpp:3930`.
- [ ] One call in `src/editor/shell_smoke.cpp`, after MAR-173's, following the
      established `if (!…) { ImGui::DestroyContext(); return 1; }` shape.

### Preamble

- [ ] `const ScopedPreferenceIsolation isolation("preview-speed");` first statement;
      fail with a clear message when `!isolation.installed()`.
- [ ] `ShellState state; state.project_path = project_path;` then `reload_project(&state)`
      and `set_selected_animation(&state, "idle", "Preview speed smoke", false, true)`.
- [ ] `state.session.clear_history();`
- [ ] Local helpers: `near(a, b)` with `1e-9`, a `seek(t)` wrapping
      `scrub_timeline_time(&state, t, "Preview speed smoke", false)`, and a
      `spine_world()` reading the preview skeleton's `spine` bone world transform for
      the pose comparisons.

### Cases — every one from design §11

- [ ] **§11.1 Registry guard.** `agent_operation_descriptor_count() == N` (Task 0's
      measurement), with the message `"Preview speed shell smoke requires the exact
      N-operation registry."`. **Do not add this constant anywhere else.**
- [ ] **§11.2 Domain.** Default `1.0`; `0.0`→`0.05`; `-3.0`→`0.05`; `100.0`→`8.0`;
      `0.05` and `8.0` exact; each of `{0.25, 0.5, 1.0, 2.0}` exact and in range;
      `NaN`/`+inf`/`-inf` each return `false` and leave the field **bit-identical**
      (compare with `std::memcmp` over the two `double`s — `==` is false for `NaN` and
      would let a `NaN` write through).
- [ ] **§11.3 Forward progression.** The six-row table, plus the equivalence law:
      3 advances at `(s, 0.25)` equal 3 advances at `(1.0, 0.25*s)` for
      `s ∈ {0.25, 2.0}`.
- [ ] **§11.4 Reverse.** `preview_reverse = true`, seek 0, speed 2.0, delta 0.25 →
      `t == 0.5` (same as forward); sampled `spine` pose differs from the forward pose
      at the same `t` and matches the forward pose at `duration - t` within `1e-6`.
- [ ] **§11.5 Loop / clamp / scrub / pause.** All four cases, with the exact numbers
      in the design. The loop-wrap case must also compare the `spine` world transform
      against the speed-1.0 walk to `0.9` within `1e-6` — that is the no-drift proof.
- [ ] **§11.6 Non-effect gate.** All seven captured values, `session.dirty()`, and
      `state.project_dirty` after `update_project_dirty_state(&state)`. Then the
      export gate: `save_project` + `materialize_temp_project_runtime_assets` +
      `export_runtime_skeleton` (and the binary output) into
      `isolation.path()`, once before and once after the speed batch, byte-compared
      with `std::ifstream` + `std::istreambuf_iterator<char>`. Both pairs identical.
      Follow the existing pattern at `shell_smoke_timeline.cpp:1165-1178`.
- [ ] **§11.7 Mid-transaction inertness.** Open an `EditTransaction` with
      `state.session.begin_edit({...})` (pattern at `shell_smoke_graph.cpp:3996`),
      change speed, assert `undo_count()` unchanged and the transaction still valid,
      assert `advance_timeline_playback` moves no time, roll back, assert the
      serialized project is byte-identical.
- [ ] **§11.8 Reset.** Both `reload_project()` branches: same `project_path`
      (`session.reload()`), then a copy saved into `isolation.path()`
      (`session.open()`). Each must land on `preview_speed == 1.0`.
- [ ] Re-assert `agent_operation_descriptor_count() == N` as the last statement.

Every failure path prints a specific message to `std::cerr` and returns `false`, in
the style of the surrounding scenarios.

### Verification

```bash
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

- [ ] Passes.
- [ ] `ls ~/Library/Application\ Support/ | grep -i marrow` prints nothing — the
      isolation held.

---

## Task 3: The transport UI

### TDD step (RED)

The transport row is ImGui code and the smoke is ImGui-free by design, so the RED here
is not a new assertion — Task 2 already covers every behaviour the buttons can
produce, because they call nothing but `set_preview_playback_speed()`.

- [ ] Before writing the widgets, confirm the smoke would catch a wiring mistake: the
      only way the UI can misbehave without failing Task 2 is by calling something
      *other* than the setter. Task 5's diff review is the gate for that.

### Implementation step (GREEN)

- [ ] In `draw_timeline_window()` (`src/editor/shell_timeline.cpp`), immediately after
      the `Icon::Loop` `icon_button` block and before the `slider_time` block, add the
      row from design §7:
      - `ImGui::SameLine();`
      - `ImGui::SetNextItemWidth(90.0f);` +
        `ImGui::DragScalar("Speed", ImGuiDataType_Double, &speed, 0.01f,
         &kPreviewSpeedMinimum, &kPreviewSpeedMaximum, "%.2fx", ImGuiSliderFlags_AlwaysClamp)`
        into a local `double speed = state->preview_speed;`, then on change call
        `set_preview_playback_speed(state, speed, "Timeline", true);`.
      - Four `ImGui::SmallButton` presets on the same line, driven by a loop over
        `kPreviewSpeedPresets` with `ImGui::PushID(index)`, each calling the same setter.
      - Wrap the whole row in `ImGui::BeginDisabled(animation == nullptr)` /
        `EndDisabled()` to match the rest of the transport.
- [ ] **Do not** route any of this through the `apply_state_preview_change` lambda —
      it sets `timeline_playing = false` and would stop playback on every speed change
      (design §7). The new code must not touch `timeline_playing` at all.

### Verification

```bash
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2
```

- [ ] Both pass. The second proves the new row does not break Parameter Modeling mode,
      where the transport is disabled.
- [ ] `git diff -- src/editor/shell_timeline.cpp | rg 'timeline_playing'` is empty.
- [ ] Ask the user to confirm visually on an interactive macOS session that the Speed
      drag and the four presets appear on the transport row, that changing speed while
      playing does **not** stop playback, and that the value reads e.g. `2.00x`. The
      headless smoke cannot see layout.

---

## Task 4: Cross-milestone interaction sweep

No new production code. Prove MAR-174 did not disturb its neighbours.

### TDD step (RED)

- [ ] Extend Task 2's scenario with a MAR-172 interaction case: author an explicit
      duration of `1.5` on `idle` through the real gesture
      (`begin_/apply_/finish_animation_duration_gesture`, pattern at
      `shell_smoke_graph.cpp:3987-3995`), enable loop sync on the `spine` rotate lane,
      then run a multi-period advance at speed `8.0` and assert:
      - the managed boundary key's time is still exactly `1.5`,
      - its value components are still a bit-for-bit copy of the time-zero key,
      - `serialize_project(...)` is byte-identical before and after the advance batch.
      This must fail if anyone later makes speed reach into the project.
- [ ] Run and confirm it passes with the Task 1 implementation in place. If it fails,
      the multiply escaped its choke point.

### Implementation step (GREEN)

- [ ] None expected. If the case fails, the fault is in Task 1's `advance_timeline_playback`
      or in the setter making a session call; fix there, not in the test.

### Verification

```bash
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_agent_dispatch_smoke
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

- [ ] All pass. `marrow_agent_dispatch_smoke` must report the **same** count as before
      MAR-174 — Task 0's `N`.

---

## Task 5: Compatibility and non-effect diff gates

No code. Prove the negatives from design §13 and §12.

- [ ] Zero-byte diffs:
      ```bash
      git diff --stat -- src/runtime include/marrow/runtime src/c_api include/marrow/c_api
      git diff --stat -- src/editor/preferences.cpp include/marrow/editor/preferences.hpp
      git diff --stat -- include/marrow/editor/session.hpp src/editor/session.cpp
      git diff --stat -- include/marrow/editor/project.hpp src/editor/project.cpp
      git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_dispatch_smoke.cpp
      git diff --stat -- src/editor/agent_handlers_*.cpp tools/mcp
      git diff --stat -- CMakeLists.txt
      ```
      **Every one must be empty.**
- [ ] The count sites are untouched:
      ```bash
      git diff -- src/editor/shell_smoke_graph.cpp tools/mcp/test_client.py
      ```
      Empty. MAR-173 owns those; MAR-174 must not have moved them.
- [ ] The only mention of speed outside the eight files in the map is the closure prose
      of Task 6:
      ```bash
      git diff --name-only
      ```
- [ ] `rg -n 'preview_speed' src/ include/` shows the field only in
      `shell_state.hpp`, `timeline_controller.cpp`, `shell_core.cpp`,
      `shell_timeline.cpp`, and `shell_smoke_timeline.cpp`. In particular **not** in
      `project.cpp`, `session.cpp`, or any `agent_*` file.

---

## Task 6: Documentation and closure

MAR-173 lands first and edits several of these lines. **Re-read each file
immediately before editing** and build on what MAR-173 wrote; do not restore an
earlier wording.

Prose rule, as established across this milestone: a sentence stating what the surface
**is** gets the current number; a sentence carrying a date, milestone, or checkpoint
stays historical. MAR-174 changes **no** count, so no count sentence should move at
all — if one does, revert it.

- [ ] `AGENTS.md:6` — advance the "next product milestone" sentence from MAR-173 to
      MAR-175, and add MAR-174 to the completed range. **Leave `AGENTS.md:161`,
      `:255`, `:273`, `:286`, `:288` alone** — those are registry-count and MAR-172/173
      rows.
- [ ] `AGENTS.md` shell-smoke bullet (the long
      `./build/marrow_editor_shell … --auto-close 2` line around `:180`) — add
      "transient preview playback speed" to the enumerated coverage.
- [ ] `AGENTS.md` `Current Validation` table — one MAR-174 row recording: the speed
      domain and presets, the single multiply site, the reverse/loop/scrub/pause
      composition, and the byte-identical project + export + zero-revision non-effect
      gate. Explicitly state **"registry unchanged at N; no agent operation added."**
- [ ] `docs/root1/discription.md` — a MAR-174 paragraph after MAR-173's, in the
      established Korean prose style: transient shell-private `double`, 0.05x–8x with
      0.25/0.5/1/2 presets, one multiplication at `advance_timeline_playback()`,
      reverse stays the direction toggle and negative speed is never accepted, zero
      clamps rather than pausing, `fmod` makes multi-period steps exact, the MAR-172
      managed boundary key is never sampled during looped playback at any speed,
      `reload_project()` resets to `kDefaultPreviewSpeed`, not in `PreviewState` /
      history / preferences / export, registry unchanged, MAR-175가 다음 경계.
- [ ] `docs/root1/discription.md:53` — update the completion list and the
      "next milestone" sentence to MAR-175.
- [ ] `docs/root1/editing-gap-analysis.md:397` — mark the MAR-174 row done and expand
      it to match the shipped design.
- [ ] `docs/root1/editing-gap-analysis.md:93` — the sentence currently ending
      "선택 키 시간 스케일과 preview 속도는 MAR-173~174다." now describes shipped
      behaviour; rewrite accordingly.
- [ ] `docs/root1/platform-validation.md:20` and `docs/root1/quick-start.md:36` —
      "next MAR-173 product milestone" → MAR-175 (MAR-173 may already have moved these
      to MAR-174; move them one further).
- [ ] `.agents/tasks/prd-marrow-runtime.json` — set MAR-174 `"status": "done"` and
      `"completedAt": "2026-08-30"`. Change nothing else in the file.
- [ ] Verify no stray number moved:
      ```bash
      git diff -- AGENTS.md docs/ | rg '^[+-].*\b(59|60)\b'
      ```
      Every hit must be a MAR-174 row *stating* the unchanged count, never a
      historical MAR-172/173 sentence being rewritten.

---

## Full Verification Checklist

Run in order. Every command must pass before MAR-174 is closed.

### Build

- [ ] `cmake -S . -B build`
- [ ] `cmake --build build`
- [ ] `cmake --build build --target marrow_verify_third_party`
- [ ] `cmake --build build --target marrow_constraint_warning_check`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `ctest --test-dir build --output-on-failure -L editor`
- [ ] `ctest --test-dir build --output-on-failure -L runtime`

### Shell (the story's own gates)

- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
      — includes `validate_preview_playback_speed_shell_smoke`.
- [ ] `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2`
- [ ] `./build/marrow_editor_shell --verify-launch-focus`
- [ ] `ls ~/Library/Application\ Support/ | grep -i marrow` prints nothing.

### Agent and MCP (must be unaffected)

- [ ] `./build/marrow_agent_dispatch_smoke` — reports Task 0's `N`, unchanged.
- [ ] `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876`
      in one shell, then `tools/mcp/venv/bin/python tools/mcp/test_client.py` — `N/N`
      parity, unchanged.

### Project, export, and runtime (must be unaffected)

- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin`
- [ ] `./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl`
- [ ] `./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- [ ] `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow`
- [ ] `./build/marrow_c_smoke`

### Compatibility (Task 5)

- [ ] Every `git diff --stat` in Task 5 is empty.
- [ ] `git diff --name-only` lists only the eight files in the map plus Task 6's docs.

### Manual

- [ ] Interactive macOS session: the Speed drag and four preset buttons appear on the
      Timeline transport row; changing speed during playback changes the rate without
      stopping playback; `2x` visibly doubles the rate; `Reverse` combined with `2x`
      plays backwards at double rate; the value clamps at `0.05x` and `8.00x`;
      reloading the project returns the display to `1.00x`.

---

## Risks for the Implementer

1. **The tempting wrong home for the field is `marrow::editor::PreviewState`.** It
   already holds `reverse`, `loop`, and `playing`, so speed looks like it belongs
   there. It does not: `assign_history_snapshot()` writes `PreviewState` back on every
   undo, so speed would silently jump on Ctrl+Z while
   `history_snapshots_equal()` still reported "no change". Keep it on `ShellState`.
2. **Any `session.set_*()` inside the setter fails the §11.6 gate**, because it bumps
   `preview_revision`. The setter must touch only `preview_speed` and `status_message`.
3. **Routing the UI through `apply_state_preview_change`** would pause playback on
   every speed change and defeat the feature, while still passing every headless
   assertion. Task 3's `rg 'timeline_playing'` diff check is the only gate; do not skip it.
4. **MAR-173 concurrency.** Task 0 is a hard gate, and Task 6's docs must be rebased
   onto MAR-173's wording rather than written from this plan's line numbers.
