# MAR-170 Fixed Curve Presets and Remembered Defaults Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add six fixed curve presets — Linear, Stepped, Ease `[0.25, 0.1, 0.25, 1]`,
Ease-In `[0.42, 0, 1, 1]`, Ease-Out `[0, 0, 0.58, 1]`, Ease-In-Out
`[0.42, 0, 0.58, 1]` — applicable to every compatible selected key as one
previewed, undoable transaction from the GUI, the C++ Agent, and the Python MCP
facade, all through MAR-169's single `set_keyframe_interpolation()` primitive;
and wire MAR-156's already-built, never-consumed `editor-settings.json`
`default_curve` field so the remembered default seeds newly authored Transform,
Deform, and Slot Color keys — while the Agent/MCP registry stays at exactly
**57** operations and `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, and
`editor-settings.json` v1 are all unchanged.

**Architecture:** One `constexpr` preset table plus three accessors appended to
`include/marrow/editor/authoring.hpp`. One ImGui-free
`apply_timeline_curve_preset()` in `timeline_controller`, called from the Graph
and Dopesheet toolbars. One new shell-only unit `shell_preferences.hpp/.cpp`
that loads the settings file once at startup and saves it atomically when the
user changes the default. Three `Interpolation::linear()` literals in
`timeline_controller.cpp` become `curve_preset_interpolation(default)`. The
Agent's existing `interpolation_request_arg()` gains four string tokens; no
registry row is added.

**Tech Stack:** C++17, Dear ImGui, existing Marrow runtime/editor session APIs,
CMake/CTest, Python MCP SDK, JSON fixtures.

**Spec:** `docs/superpowers/specs/2026-08-30-mar-170-curve-presets-and-defaults-design.md`

## Global Constraints

- Read `docs/root1/discription.md` and the MAR-170 PRD story before each
  implementation task; current source and tests override stale plan assumptions.
- Follow strict RED-GREEN-REFACTOR: add one focused failing test, observe the
  expected failure, write the minimum production code, then rerun focused and
  affected regression tests.
- Do not reset, discard, stash, commit, push, or create a PR unless the user
  separately requests it. Each task ends with a read-only
  `git diff --check` / `git status --short` checkpoint.
- **Never modify `src/editor/preferences.cpp` or
  `include/marrow/editor/preferences.hpp`.** MAR-156 built exactly what MAR-170
  needs. `kEditorSettingsVersion` stays `1`; no field is added, removed, or
  reinterpreted. If a task appears to require a change there, stop and report —
  it means the design was misread.
- **Never add a `.marrow` field.** MAR-171 owns project-local curve metadata.
  The only project bytes MAR-170 may change are values in the existing `curve`
  field.
- **Never add an Agent operation.** `std::size(kOperationSpecs)` must still be
  57 after every task. Any change that grows the registry is out of scope.
- The preset numbers exist in exactly one place — `kCurvePresets` in
  `include/marrow/editor/authoring.hpp`. No literal `0.42`, `0.58`, `0.25`, or
  `0.1` curve constant may appear in the shell, the agent handler, the MCP
  server, or any production `.cpp`. Tests may and should spell them out
  literally, because a test that reads the constant it is checking proves
  nothing.
- Every mutation goes through `set_keyframe_interpolation()` inside an
  `EditorSession::EditTransaction`. Never introduce a parallel easing-writing
  path, and never give that primitive a component parameter.
- Never change MAR-168's point drag or MAR-169's handle drag, including
  MAR-169's `[1/3, 1/3, 2/3, 2/3]` Linear/Stepped conversion seed and the
  `interpolation_arg()` helper used by `set_transform`,
  `set_deform_keyframe`, and `set_slot_color_keyframe`.
- **Preference isolation is mandatory.** Any test process that loads shell
  preferences must first point `MARROW_CONFIG_HOME` at a fresh temporary
  directory and restore it afterwards (design spec §16). A test run must never
  read or write the developer's real `editor-settings.json`.
- Preserve `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json`
  v1, `SelectionSet`, and GPU ownership. The only public header change is the
  additive block in `include/marrow/editor/authoring.hpp`.
- Automated display gates do not establish manual-visible-UI, Windows 11, or
  physical-input qualification. Keep MAR-192 through MAR-210 open.

## File and Responsibility Map

**Create**

- `src/editor/shell_preferences.hpp` — `load_shell_preferences()` and
  `set_shell_default_curve()` declarations.
- `src/editor/shell_preferences.cpp` — their implementations.

**Modify**

- `include/marrow/editor/authoring.hpp` — additive `CurvePresetDefinition`,
  `kCurvePresets`, `curve_preset_definition()`, `curve_preset_interpolation()`,
  `curve_preset_of()`, `curve_preset_from_token()`.
- `src/editor/authoring.cpp` — their implementations and the `static_assert`
  block.
- `src/tests/preference_store_tests.cpp` — preset constants, token agreement,
  format invariant, runtime well-posedness, identity round trip, fallback
  matrix, preservation, `MARROW_CONFIG_HOME`.
- `src/samples/editor_project_smoke.cpp` — `validate_mar170_curve_presets()`.
- `src/editor/shell_state.hpp` — the four preference fields.
- `src/editor/timeline_controller.hpp/.cpp` — `TimelineCurvePresetResult`,
  `apply_timeline_curve_preset()`, `active_outgoing_curve_preset()`, and the
  three `Interpolation::linear()` seeding sites.
- `src/editor/shell_timeline_graph.hpp/.cpp` — the render-stat additions, the
  preset row, the `Default:` combo, and the `outgoing_kind_label()` rewrite.
- `src/editor/shell_timeline.cpp` — the Dopesheet preset row and `Default:`
  combo.
- `src/editor/shell_main.cpp` — the startup `load_shell_preferences()` call.
- `src/editor/shell_smoke.cpp` — `MARROW_CONFIG_HOME` isolation and the new
  scenario registration.
- `src/editor/shell_smoke_scenarios.hpp` — the new scenario declaration.
- `src/editor/shell_smoke_graph.cpp` — `validate_timeline_curve_preset_shell_smoke()`.
- `src/editor/shell_smoke_frames.cpp` — actual-frame preset coverage.
- `src/editor/agent_dispatch.cpp` — the four new tokens inside
  `interpolation_request_arg()`.
- `src/samples/agent_dispatch_smoke.cpp` — preset token behaviour cases.
- `tools/mcp/tools/editing.py` — `_bezier_interpolation_schema()`'s string enum.
- `tools/mcp/test_client.py` — preset behaviour coverage.
- `CMakeLists.txt` — `src/editor/shell_preferences.cpp` in the
  `marrow_editor_shell` source list.
- `AGENTS.md`, `docs/root1/*.md`, the design spec, and
  `.agents/tasks/prd-marrow-runtime.json` — record verified MAR-170 completion
  and make MAR-171 next.

---

### Task 0: Reconcile with the as-built MAR-169

MAR-169 was implemented concurrently with this plan. Its final code may differ
from its plan in naming, signatures, or small details. **Do this before writing
any MAR-170 code**, and prefer the as-built source over anything this plan or
the MAR-170 design spec asserts about MAR-169.

**Files:** read-only.

- [ ] **Step 1: Confirm MAR-169 is complete and green**

```bash
jq -r '.stories[] | select(.id == "MAR-169") | "\(.id) \(.status) \(.completedAt // "-")"' .agents/tasks/prd-marrow-runtime.json
cmake -S . -B build && cmake --build build -j4
./build/marrow_timeline_graph_model_tests
./build/marrow_preference_tests
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_agent_dispatch_smoke
```

Expected: MAR-169 is `done`, everything passes, and
`marrow_agent_dispatch_smoke` reports exactly **57** operations. If MAR-169 is
not `done`, stop and report; MAR-170 must not start on a half-built dependency.
If the registry is not 57, stop and report — every "57" in this plan is then
wrong and the plan must be reconciled before any code is written.

- [ ] **Step 2: Read the as-built MAR-169 surfaces and record the deltas**

```bash
git diff --stat HEAD~1 2>/dev/null || git log --oneline -5
grep -n 'set_keyframe_interpolation\|TimelineInterpolationResult' -A 25 include/marrow/editor/authoring.hpp
grep -n 'set_keyframe_interpolation' -A 40 src/editor/authoring.cpp | head -80
grep -n 'interpolation_request_arg' -A 45 src/editor/agent_dispatch.cpp
grep -n 'interpolation_request_arg' src/editor/agent_dispatch_internal.hpp
grep -n 'timeline.set_interpolation' -A 60 src/editor/agent_handlers_editing.cpp | head -90
grep -n 'outgoing_kind_label' -A 25 src/editor/shell_timeline_graph.cpp
grep -n 'TimelineGraphRenderStats' -A 60 src/editor/shell_timeline_graph.hpp
grep -n 'BeginDisabled\|EndDisabled\|SmallButton\|Checkbox\|Outgoing:' src/editor/shell_timeline_graph.cpp
grep -n '_bezier_interpolation_schema\|_timeline_interpolation_key_schema' -A 25 tools/mcp/tools/editing.py
grep -rn '\b57\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n 'Interpolation::linear\(\)' src/editor/timeline_controller.cpp
```

Write down, in your working notes (not in a file):

1. the exact signature and semantics of `set_keyframe_interpolation()` and
   `TimelineInterpolationResult`, including whether the result carries
   `key_count` / `changed_key_count` and what `changed` means for a no-op;
2. the exact signature of `interpolation_request_arg()` and where its string
   branch lives — MAR-170 edits exactly that branch;
3. whether `authoring.hpp` already includes `<array>` and `<optional>`;
4. what `outgoing_kind_label()` looks like as built and whether MAR-169 changed
   its signature or its call site;
5. every field MAR-169 added to `TimelineGraphRenderStats`, so the MAR-170
   additions append rather than collide;
6. the exact names of MAR-169's headless graph scenario(s) and where they are
   registered, so the MAR-170 scenario follows the same shape;
7. **every** file and line asserting `57`, and confirm each is a *current*
   claim. MAR-170 changes none of them; you must be able to re-verify all of
   them at Task 8;
8. the exact line numbers of the three `Interpolation::linear()` literals in
   `timeline_controller.cpp`. There must be exactly three
   (`sample_transform_keyframe`, `sample_deform_keyframe`, and the Slot Color
   branch of `add_timeline_key_at_playhead`). If `rg` finds a different number,
   stop and reconcile §3.2 of the design spec before proceeding.

- [ ] **Step 3: Reconcile this plan against those facts**

Where the as-built code differs, adapt the MAR-170 signatures below to match it.
Do not rename or reshape MAR-169's API to fit this plan. If a difference makes
a MAR-170 decision impossible — for example, if `interpolation_request_arg()`
turns out to construct a `runtime::Interpolation` before returning, leaving no
place to resolve a token to raw doubles — stop and report rather than
duplicating the parser.

---

### Task 1: The preset table and its accessors

**Files:**

- Modify: `include/marrow/editor/authoring.hpp`
- Modify: `src/editor/authoring.cpp`
- Modify: `src/tests/preference_store_tests.cpp`

**Interfaces:**

- Consumes: `CurvePreset` (`marrow/editor/preferences.hpp`),
  `runtime::InterpolationKind`, `runtime::Interpolation::cubic_bezier()`,
  `runtime::AnimationScalar`.
- Produces: `CurvePresetDefinition`, `kCurvePresets`,
  `curve_preset_definition()`, `curve_preset_interpolation()`,
  `curve_preset_of()`, `curve_preset_from_token()` (design spec §12.1).

- [ ] **Step 1: Add failing preset-constant tests**

In `src/tests/preference_store_tests.cpp` add
`test_curve_preset_constants(TestSuite&)` and register it in `main()`. Include
`marrow/editor/authoring.hpp`. Spell every number out literally — do not read it
from the table you are checking:

```cpp
using marrow::editor::CurvePreset;
using marrow::editor::kCurvePresets;
using marrow::editor::curve_preset_definition;
using marrow::editor::curve_preset_interpolation;
using marrow::editor::curve_preset_of;
using marrow::editor::curve_preset_from_token;
using Scalar = marrow::runtime::AnimationScalar;

struct Expected {
    CurvePreset preset;
    const char* token;
    const char* display;
    marrow::runtime::InterpolationKind kind;
    double cx1, cy1, cx2, cy2;
};
const std::vector<Expected> expected{
    {CurvePreset::Linear,    "linear",      "Linear",
     marrow::runtime::InterpolationKind::Linear,      0.0,  0.0, 0.0,  0.0},
    {CurvePreset::Stepped,   "stepped",     "Stepped",
     marrow::runtime::InterpolationKind::Stepped,     0.0,  0.0, 0.0,  0.0},
    {CurvePreset::Ease,      "ease",        "Ease",
     marrow::runtime::InterpolationKind::CubicBezier, 0.25, 0.1, 0.25, 1.0},
    {CurvePreset::EaseIn,    "ease_in",     "Ease-In",
     marrow::runtime::InterpolationKind::CubicBezier, 0.42, 0.0, 1.0,  1.0},
    {CurvePreset::EaseOut,   "ease_out",    "Ease-Out",
     marrow::runtime::InterpolationKind::CubicBezier, 0.0,  0.0, 0.58, 1.0},
    {CurvePreset::EaseInOut, "ease_in_out", "Ease-In-Out",
     marrow::runtime::InterpolationKind::CubicBezier, 0.42, 0.0, 0.58, 1.0},
};

suite.expect(kCurvePresets.size() == expected.size(),
             "there must be exactly six fixed curve presets");

for (std::size_t index = 0U; index < expected.size(); ++index) {
    const Expected& want = expected[index];
    const auto& got = kCurvePresets[index];
    suite.expect(got.preset == want.preset && got.token == want.token &&
                     got.display_name == want.display &&
                     got.kind == want.kind &&
                     got.control_points[0] == want.cx1 &&
                     got.control_points[1] == want.cy1 &&
                     got.control_points[2] == want.cx2 &&
                     got.control_points[3] == want.cy2,
                 std::string("preset ") + want.token + " must match its fixed definition");
    suite.expect(&curve_preset_definition(want.preset) == &got,
                 std::string("curve_preset_definition must index ") + want.token);
    suite.expect(curve_preset_from_token(want.token) == want.preset,
                 std::string("token ") + want.token + " must parse to its preset");

    // The format invariant, as double and after float32 narrowing.
    if (want.kind == marrow::runtime::InterpolationKind::CubicBezier) {
        const double nx1 = static_cast<double>(static_cast<Scalar>(want.cx1));
        const double nx2 = static_cast<double>(static_cast<Scalar>(want.cx2));
        suite.expect(want.cx1 >= 0.0 && want.cx1 <= 1.0 &&
                         want.cx2 >= 0.0 && want.cx2 <= 1.0 &&
                         nx1 >= 0.0 && nx1 <= 1.0 && nx2 >= 0.0 && nx2 <= 1.0,
                     std::string("preset ") + want.token +
                         " must keep cx inside [0, 1] before and after narrowing");
    }

    // Identity round trip.
    suite.expect(curve_preset_of(curve_preset_interpolation(want.preset)) == want.preset,
                 std::string("preset ") + want.token + " must read back as itself");
}

// Custom curves and MAR-169's conversion seed are not presets.
suite.expect(!curve_preset_of(
                 marrow::runtime::Interpolation::cubic_bezier(0.2, -0.4, 0.8, 1.6))
                 .has_value(),
             "an overshoot curve must not be reported as a preset");
suite.expect(!curve_preset_of(marrow::runtime::Interpolation::cubic_bezier(
                                  1.0 / 3.0, 1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0))
                 .has_value(),
             "MAR-169's linear-equivalent conversion seed is not a preset");
suite.expect(!curve_preset_from_token("ease-in").has_value() &&
                 !curve_preset_from_token("easeIn").has_value() &&
                 !curve_preset_from_token("").has_value(),
             "only the six snake_case tokens are accepted");

// Runtime well-posedness of every cubic preset, including Ease-In's X'(1) = 0.
for (const Expected& want : expected) {
    if (want.kind != marrow::runtime::InterpolationKind::CubicBezier) continue;
    const auto curve = curve_preset_interpolation(want.preset);
    double previous = curve.transform(0.0);
    bool ok = previous == 0.0;
    for (int step = 1; step <= 100; ++step) {
        const double alpha = static_cast<double>(step) / 100.0;
        const double value = curve.transform(alpha);
        ok = ok && std::isfinite(value) && value >= previous - 1e-6 &&
             value >= -1e-6 && value <= 1.0 + 1e-6;
        previous = value;
    }
    ok = ok && std::abs(curve.transform(1.0) - 1.0) < 1e-6;
    suite.expect(ok,
                 std::string("preset ") + want.token +
                     " must evaluate finite, monotone, and unclamped-free on [0, 1]");
}
```

Also add `test_curve_preset_tokens_match_settings_tokens(TestSuite&)`: for each
of the six, write a settings file containing that token, load it, and assert the
resulting `default_curve` equals `curve_preset_from_token(token)`. This is the
test that keeps `authoring.hpp`'s table and `preferences.cpp`'s private token
list in agreement without refactoring MAR-156 code.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_preference_tests -j4
```

Expected: compilation fails because `CurvePresetDefinition`, `kCurvePresets`,
`curve_preset_definition`, `curve_preset_interpolation`, `curve_preset_of`, and
`curve_preset_from_token` do not exist.

- [ ] **Step 3: Implement the table and accessors**

In `include/marrow/editor/authoring.hpp`, after MAR-169's
`set_keyframe_interpolation()` declaration, add `#include <array>`,
`#include <optional>`, and `#include "marrow/editor/preferences.hpp"` if not
already present, then the block from design spec §6.4 and §12.1.

In `src/editor/authoring.cpp` implement:

- `curve_preset_definition(preset)` — a `switch` over the closed enum returning
  `kCurvePresets[i]`, with the `default`-free form so a new enumerator is a
  compiler warning;
- `curve_preset_interpolation(preset)` — `Interpolation::linear()`,
  `Interpolation::stepped()`, or `Interpolation::cubic_bezier(p[0], p[1], p[2], p[3])`;
- `curve_preset_of(interpolation)` — design spec §7.2, comparing
  `interpolation.cubic_bezier().cx1` etc. against
  `static_cast<runtime::AnimationScalar>(entry.control_points[k])`,
  **bit-exactly, with no epsilon**;
- `curve_preset_from_token(token)` — a linear scan of `kCurvePresets` comparing
  `token`.

Add a file-local `constexpr` validation block:

```cpp
constexpr bool curve_presets_are_well_formed() {
    for (std::size_t index = 0U; index < kCurvePresets.size(); ++index) {
        const auto& entry = kCurvePresets[index];
        if (static_cast<std::size_t>(entry.preset) != index) return false;
        if (entry.token.empty() || entry.display_name.empty()) return false;
        if (entry.kind == runtime::InterpolationKind::CubicBezier) {
            if (!(entry.control_points[0] >= 0.0 && entry.control_points[0] <= 1.0)) return false;
            if (!(entry.control_points[2] >= 0.0 && entry.control_points[2] <= 1.0)) return false;
        }
    }
    return true;
}
static_assert(curve_presets_are_well_formed(),
              "curve presets must be in enum order and keep cx inside the [0, 1] "
              "invariant both .marrow and .mskl loaders enforce");
```

- [ ] **Step 4: Run focused and affected tests and observe GREEN**

```bash
cmake --build build --target marrow_preference_tests marrow_project_smoke marrow_editor_shell -j4
./build/marrow_preference_tests
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: every new case and every existing preference case passes, and nothing
else regresses.

- [ ] **Step 5: Record a no-commit checkpoint**

```bash
git diff --stat -- include/marrow/editor/preferences.hpp src/editor/preferences.cpp
git diff --check
git status --short
```

Expected: the preferences module diff is **empty**.

---

### Task 2: Preset application through the shared primitive

**Files:**

- Modify: `src/samples/editor_project_smoke.cpp`

**Interfaces:**

- Consumes: `set_keyframe_interpolation()`, `TimelineKeySelector`,
  `curve_preset_interpolation()`, `curve_preset_of()`.
- Produces: `validate_mar170_curve_presets()`.

This task adds no production code. It proves, at the project-domain layer, that
the presets compose correctly with MAR-169's primitive before any UI exists.

- [ ] **Step 1: Add failing project-smoke coverage**

In `src/samples/editor_project_smoke.cpp` add
`bool validate_mar170_curve_presets(const marrow::editor::ProjectLoadResult&)`
next to `validate_mar169_graph_interpolation_authoring()` and call it from
`main()` in the same place. It must cover design spec §19.2:

- each of the six presets written onto a Transform Translate key, a Deform key,
  and a Slot Color key, asserting `interpolation.kind()` and, for cubics, the
  four stored `float` values equal
  `static_cast<AnimationScalar>(<literal>)`, and that `time`, `angle`, `x`,
  `y`, `color`, and `vertex_offsets` are byte-identical to the snapshot;
- **segment-wide identity**: after a preset on a Translate key, exactly one
  `interpolation` field in the serialized project differs;
- **multi-key determinism**: build a 12-selector list, apply `Ease-In-Out`,
  snapshot `serialize_project()`; undo the effect by reloading, apply the same
  preset with the selector list reversed, and assert the two serializations are
  byte-identical;
- **no-change**: applying the same preset twice yields `changed == false` and a
  byte-identical project;
- **skip/reject boundary**: `DrawOrder`, `Event`, and `SlotAttachment` selectors
  are rejected atomically by the primitive (this is MAR-169 behaviour that
  MAR-170's GUI filters *around*, and the test records that split);
- **save/reload preserves preset identity**: save to a temp path, reload, and
  assert `curve_preset_of()` names the same preset for every written key;
- **loader acceptance**: the reloaded project produces no validation error,
  proving no preset can trip `project.cpp:1639` or `skeleton_parse.cpp:1727`;
- **export**: run the existing export/compare helper over a project carrying all
  six presets.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_project_smoke -j4
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

Expected: the new validator fails on its first assertion, or the build fails if
`curve_preset_of` is not yet visible from the smoke's include set. Fix the
include, not the assertion.

- [ ] **Step 3: Make it pass**

No production change should be needed. If a case fails, the failure is real:
either the Task 1 table is wrong or `set_keyframe_interpolation()` does not
behave as MAR-169 documented. Diagnose before changing anything, and if the
primitive is at fault, stop and report rather than working around it in MAR-170.

- [ ] **Step 4: Run focused and affected tests and observe GREEN**

```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/player_idle.marrow
./build/marrow_preference_tests
```

- [ ] **Step 5: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 3: Shell preference session

**Files:**

- Create: `src/editor/shell_preferences.hpp`
- Create: `src/editor/shell_preferences.cpp`
- Modify: `src/editor/shell_state.hpp`
- Modify: `src/editor/shell_main.cpp`
- Modify: `src/editor/shell_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: `PreferenceStore`, `EditorPreferences`, `PreferenceLoadStatus`,
  `PreferenceSaveResult`.
- Produces: `load_shell_preferences()`, `set_shell_default_curve()`, and the
  four `ShellState` fields (design spec §12.3, §12.4).

- [ ] **Step 1: Install the test isolation FIRST**

Before any preference code exists, make `run_headless_smoke()` in
`src/editor/shell_smoke.cpp` set `MARROW_CONFIG_HOME` to a unique temporary
directory as its **first** statement, and restore the previous value plus remove
the directory on every return path (RAII, in the style of
`preference_store_tests.cpp`'s `TemporaryDirectory` and its environment guard —
copy that shape rather than inventing a new one). Use `setenv`/`unsetenv` on
POSIX and `_putenv_s` on Windows, guarded exactly as `preferences.cpp` guards
its platform code.

This step comes first because every later step makes the shell read that file,
and a smoke run must never touch the developer's real settings.

Verify it in isolation, before adding the load:

```bash
cmake --build build --target marrow_editor_shell -j4
ls -la "$HOME/Library/Application Support/Marrow/" 2>/dev/null | tee /tmp/mar170_settings_before.txt
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
ls -la "$HOME/Library/Application Support/Marrow/" 2>/dev/null | tee /tmp/mar170_settings_after.txt
diff /tmp/mar170_settings_before.txt /tmp/mar170_settings_after.txt
```

Expected: identical, on this and every later run of this plan.

- [ ] **Step 2: Add the failing shell preference test**

Extend `src/tests/preference_store_tests.cpp` with
`test_shell_preference_session(TestSuite&)`, registered in `main()`. It builds
an isolated `MARROW_CONFIG_HOME`, opens an `EditorSession` on
`assets/fixtures/player_idle.marrow`, and asserts, without ImGui:

- a fresh directory yields `PreferenceLoadStatus::FirstRun`, `default_curve ==
  CurvePreset::Linear`, and **no file created**;
- after a `save()` of `default_curve = CurvePreset::EaseOut`, a reload yields
  `Loaded` and `EaseOut`;
- a settings file carrying `recent_projects` and an unknown additive field,
  re-saved with only `default_curve` changed, retains both verbatim;
- a malformed file yields `Malformed`, `Linear`, and is **not** rewritten by the
  load;
- a `version: 2` file is refused by `save()` and survives byte-for-byte.

Because `load_shell_preferences()` and `set_shell_default_curve()` take a
`ShellState`, which lives in the shell target and not in `marrow_editor`, this
test exercises `PreferenceStore` directly with the same call sequence the shell
uses, and the `ShellState`-level behaviour is covered by the Task 6 shell smoke.
State that split in a comment so the next reader does not "fix" it.

- [ ] **Step 3: Observe RED, then implement**

```bash
cmake --build build --target marrow_preference_tests -j4
./build/marrow_preference_tests
```

Then add to `src/editor/shell_state.hpp` (design spec §12.4):

```cpp
    // MAR-170: user-local editor settings, loaded once at startup. Never
    // participates in project history, dirty state, or revisions.
    marrow::editor::EditorPreferences preferences{};
    marrow::editor::PreferenceLoadStatus preference_status{
        marrow::editor::PreferenceLoadStatus::FirstRun};
    std::filesystem::path preference_path;
    std::string preference_diagnostic;
```

with `#include "marrow/editor/preferences.hpp"`. Add **nothing** to
`authoring_gesture_active()` and **nothing** to `cancel_authoring_gestures()`.

Create `src/editor/shell_preferences.hpp/.cpp` implementing design spec §12.3:

```text
load_shell_preferences(state):
    PreferenceStore store;                     // resolves MARROW_CONFIG_HOME first
    const auto result = store.load();
    state->preferences           = result.preferences;
    state->preference_status     = result.status;
    state->preference_path       = result.path;
    state->preference_diagnostic = result.diagnostic;
    if (status is not Loaded and not FirstRun)
        state->status_message = "Editor settings could not be read; using the Linear default curve";
    // never writes

set_shell_default_curve(state, preset):
    if (state->preferences.default_curve == preset) return true;   // no write
    state->preferences.default_curve = preset;                     // preserves the rest
    PreferenceStore store;
    const auto saved = store.save(state->preferences);
    if (!saved) { state->error_message = "Failed to store the default curve: " + saved.error; return false; }
    state->status_message = "Default curve set to " + std::string(curve_preset_definition(preset).display_name);
    return true;
```

Neither function references `state->session`, `state->load_result`, or the
runtime. Add the `.cpp` to `marrow_editor_shell`'s source list in
`CMakeLists.txt`, next to `src/editor/shell_core.cpp`.

Call `load_shell_preferences(&shell_state);` in `shell_main.cpp` immediately
after `ShellState shell_state;` and **before** `reload_project(&shell_state);`,
and in `shell_smoke.cpp` immediately after `ShellState shell_state;` (which is
already inside the Step 1 isolation).

- [ ] **Step 4: Run focused and affected tests and observe GREEN**

```bash
cmake -S . -B build && cmake --build build -j4
./build/marrow_preference_tests
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
diff <(ls -la "$HOME/Library/Application Support/Marrow/" 2>/dev/null) /tmp/mar170_settings_before.txt
```

Expected: all green, and the real settings directory is still untouched.

- [ ] **Step 5: Record a no-commit checkpoint**

```bash
git diff --stat -- src/editor/preferences.cpp include/marrow/editor/preferences.hpp
git diff --check
git status --short
```

Expected: the preferences module diff is still empty.

---

### Task 4: `apply_timeline_curve_preset()` and default seeding

**Files:**

- Modify: `src/editor/timeline_controller.hpp`
- Modify: `src/editor/timeline_controller.cpp`

**Interfaces:**

- Consumes: `set_keyframe_interpolation()`, `timeline_key_selector()`,
  `timeline_key_index()`, `timeline_track_is_editable()`,
  `find_timeline_track()`, the `ensure_*_timeline_edit_index()` family,
  `EditorSession::EditTransaction`, `curve_preset_interpolation()`,
  `curve_preset_of()`.
- Produces: `TimelineCurvePresetResult`, `apply_timeline_curve_preset()`,
  `active_outgoing_curve_preset()` (design spec §12.2), and the three seeding
  sites (design spec §9.1).

- [ ] **Step 1: Add failing headless coverage**

This task's tests live in the Task 6 shell smoke, which needs the function to
exist to compile. To keep the RED-GREEN cycle honest without a circular
dependency, add the **narrowest** possible failing case first, in
`src/editor/shell_smoke_graph.cpp`, inside a new
`validate_timeline_curve_preset_shell_smoke()` that at this point asserts only:

- selecting two Transform keys and calling
  `apply_timeline_curve_preset(state, tracks, CurvePreset::EaseInOut)` returns
  `applied == true`, `changed_key_count == 2`, `skipped_key_count == 0`, adds
  exactly one history entry, and stores
  `[0.42f, 0.0f, 0.58f, 1.0f]` in both keys;
- with the default set to `CurvePreset::EaseOut`,
  `add_timeline_key_at_playhead()` on a Transform track produces a key whose
  easing is exactly `[0.0f, 0.0f, 0.58f, 1.0f]`.

Declare it in `shell_smoke_scenarios.hpp` and call it from `shell_smoke.cpp`
alongside the MAR-168/169 scenarios, inside its own `MARROW_CONFIG_HOME`
isolation. Task 6 fills the scenario out.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_editor_shell -j4
```

Expected: compilation fails because `apply_timeline_curve_preset` and
`TimelineCurvePresetResult` do not exist.

- [ ] **Step 3: Implement the preset application**

In `timeline_controller.hpp` add the declarations from design spec §12.2. In
`timeline_controller.cpp` implement `apply_timeline_curve_preset()` exactly per
design spec §10.1–§10.6:

```text
1. guard: null state, no project, no animation, authoring_gesture_active -> {} with error
2. build the selector list in selection order:
     resolve track (find_timeline_track) -> skip if missing or !timeline_track_is_editable
     resolve key index (timeline_key_index)  -> skip if lost
     timeline_key_selector()                 -> skip if nullopt
     skip DrawOrder / Event / SlotAttachment kinds, counting them in skipped_key_count
     skip a selector equal to one already collected   (full-field equality)
3. if the list is empty: set status_message; return {applied=false, ...}
4. begin_edit({EditKind::EditProperty,
               one or "N keys" label,
               "timeline:curve-preset",
               /*allow_merge=*/false,
               Project | Runtime | Preview})
5. materialize every selector's track through the ensure_*_timeline_edit_index family;
   on failure -> cancel(); error; return
6. definition = curve_preset_definition(preset)
   result = set_keyframe_interpolation(project, selectors, definition.kind,
                                       definition.control_points)
   on error          -> cancel(); error_message; return
   on !result.changed -> cancel(); "Selected keys already use <Display>"; return
7. refresh_runtime(); on failure -> cancel(); error; return
8. commit(); sync_shell_from_editor_session(state); status_message; return applied
```

Implement `active_outgoing_curve_preset()` as a read-only helper: resolve the
active key in the focused track, return `std::nullopt` when there is no
outgoing segment, otherwise `curve_preset_of(key.outgoing_easing)`.

Then replace the three seeding literals (design spec §9.1):

```cpp
// sample_transform_keyframe(), sample_deform_keyframe(), and the SlotColor
// branch of add_timeline_key_at_playhead():
keyframe.interpolation =
    marrow::editor::curve_preset_interpolation(state.preferences.default_curve);
```

Leave every `copied.interpolation = keyframe.interpolation;` line in the
clipboard paths **untouched**.

- [ ] **Step 4: Run focused and affected tests and observe GREEN**

```bash
cmake --build build --target marrow_editor_shell -j4
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

- [ ] **Step 5: Record a no-commit checkpoint**

```bash
rg -n 'Interpolation::linear\(\)' src/editor/timeline_controller.cpp
git diff --check
git status --short
```

Expected: the `rg` sweep now finds **zero** hits in
`sample_transform_keyframe`, `sample_deform_keyframe`, and
`add_timeline_key_at_playhead`. Any remaining hit must be justified in your
notes.

---

### Task 5: ImGui wiring in both timeline tabs

**Files:**

- Modify: `src/editor/shell_timeline_graph.hpp`
- Modify: `src/editor/shell_timeline_graph.cpp`
- Modify: `src/editor/shell_timeline.cpp`
- Modify: `src/editor/shell_smoke_frames.cpp`

**Interfaces:**

- Consumes: `apply_timeline_curve_preset()`, `active_outgoing_curve_preset()`,
  `set_shell_default_curve()`, `kCurvePresets`, `curve_preset_definition()`.
- Produces: the preset row, the `Default:` combo, the rewritten
  `outgoing_kind_label()`, and the `TimelineGraphRenderStats` additions (design
  spec §12.6, §13).

- [ ] **Step 1: Add failing actual-frame coverage**

In `src/editor/shell_smoke_frames.cpp`, extend the graph frame scenario per
design spec §19.4:

- assert `curve_preset_row_drawn`, finite `first_preset_min_x/y`, and that
  `plot_min_y < plot_max_y` and `plot_max_y` still lies inside the timeline
  window after the toolbar grew;
- assert MAR-167/168's `fit_*` and `first_component_*` rectangles are still
  reported non-degenerate and still hoverable, proving the appended row moved
  nothing;
- with keys selected, click the reported first preset button through real ImGui
  mouse events and assert one history entry and the expected stored curve;
- with nothing selected, assert `curve_preset_row_enabled == false` and a click
  changes nothing;
- assert the preset row and the `Default:` combo are inert while a MAR-169
  handle drag is live.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_editor_shell -j4
```

Expected: compilation fails on the missing render-stat fields.

- [ ] **Step 3: Implement the Graph tab wiring**

In `shell_timeline_graph.hpp`, append the fields from design spec §12.6 to
`TimelineGraphRenderStats` — **append**, never reorder, so MAR-167/168/169's
positional expectations are unaffected.

Rewrite `outgoing_kind_label()` (currently at
`src/editor/shell_timeline_graph.cpp:215`) to return the preset display name via
`curve_preset_of()`, or `"Custom Bezier"`, keeping `"No outgoing segment"` for a
missing active key or a last key and keeping its signature and call site
unchanged.

In `draw_timeline_graph_body()`, **after** the existing `Outgoing:` label and
the shared-easing notice, add the preset row and the `Default:` combo per design
spec §13, publishing the new render stats. The ImGui layer performs no
arithmetic and no mutation of its own: a button click calls
`apply_timeline_curve_preset()`, a combo change calls
`set_shell_default_curve()`.

- [ ] **Step 4: Implement the Dopesheet wiring**

In `draw_dopesheet_body()` (`src/editor/shell_timeline.cpp:1997`), after the
existing `Snap to Frames` checkbox, add the same preset row and `Default:`
combo, calling the same two functions. Factor the row into one file-local
helper if both tabs can share a translation unit; otherwise duplicate only the
ImGui calls, never the logic.

Do **not** touch the per-key `Interpolation` combo or the
`Bezier X1/Y1/X2/Y2` fields further down that file — MAR-169 deferred that
refactor and MAR-170 keeps the deferral.

- [ ] **Step 5: Run focused and affected tests and observe GREEN**

```bash
cmake --build build --target marrow_editor_shell -j4
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2
```

Expected: the actual-frame cases pass, and MAR-167/168/169's frame cases pass
unchanged.

- [ ] **Step 6: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 6: The full headless preset scenario

**Files:**

- Modify: `src/editor/shell_smoke_graph.cpp`
- Modify: `src/editor/shell_smoke_scenarios.hpp` (if not already done in Task 4)
- Modify: `src/editor/shell_smoke.cpp` (if not already done in Task 4)

**Interfaces:**

- Consumes: everything from Tasks 1–5.
- Produces: the complete `validate_timeline_curve_preset_shell_smoke()`.

- [ ] **Step 1: Fill out the scenario**

Expand the Task 4 stub to the full list in design spec §19.3. Every case runs in
an isolated session with its own `MARROW_CONFIG_HOME` directory. The cases that
must not be skipped, because each maps to an acceptance criterion:

| Case | Criterion |
| --- | --- |
| Three Transform keys, one preset, one history entry, exact stored floats | 2 |
| Mixed Transform + Slot Color + Event selection: Event skipped and counted | 2 |
| Event-only selection: no transaction, no history, status message | 2 |
| Re-apply: `applied == false`, no history entry, byte-identical project | 2 |
| Display component Y, apply preset, X segment changes identically | 5 |
| Undo then redo restores the curve, `selected_keys`/`active_key` bit-identical | 2 |
| Preset then MAR-169 handle drag: `undo_count()` grows by exactly 2 | 2 + MAR-169 |
| After that drag, `active_outgoing_curve_preset()` is `std::nullopt` | design §11 |
| Default `Ease-Out` seeds new Transform, Deform, and Slot Color keys | 3 |
| Default `Stepped` seeds the next new key as Stepped | 3 |
| Copy an `Ease-In` key, change the default, paste: still `Ease-In` | design §9.2 |
| Default change + save: project bytes/dirty/history/revision unchanged | 3 |
| Isolated `MARROW_CONFIG_HOME` holds a settings file only after an explicit change | 3 + 6 |
| Malformed settings file: default is Linear, no keyframe changed | 4 |
| `agent_operation_descriptor_count() == 57` before and after every case | 5 |

The rollback comparisons follow MAR-168/169's shape exactly: compare
`serialize_project()`, `undo_count()`, `redo_count()`, `project_revision()`,
`dirty()`, the rebuilt dopesheet `TrackRow::key_times`, every rebuilt graph
`Key::values`, every rebuilt `Segment::kind`, and every rebuilt
`Key::outgoing_easing` control point.

- [ ] **Step 2: Build, observe RED per case, then GREEN**

```bash
cmake --build build --target marrow_editor_shell -j4
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Add cases one at a time. A case that passes on first write is suspicious —
verify it can fail by temporarily inverting the expected value before moving on.

- [ ] **Step 3: Run the affected regression set**

```bash
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_timeline_graph_model_tests
./build/marrow_timeline_model_tests
./build/marrow_preference_tests
```

- [ ] **Step 4: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 7: Preset tokens on the Agent and MCP surfaces

**Files:**

- Modify: `src/editor/agent_dispatch.cpp`
- Modify: `src/samples/agent_dispatch_smoke.cpp`
- Modify: `tools/mcp/tools/editing.py`
- Modify: `tools/mcp/test_client.py`

**Interfaces:**

- Consumes: MAR-169's `interpolation_request_arg()` and
  `timeline.set_interpolation` handler, `curve_preset_from_token()`,
  `kCurvePresets`.
- Produces: four accepted tokens and their MCP schema. **No registry row.**

- [ ] **Step 1: Add the failing agent behaviour cases**

In `src/samples/agent_dispatch_smoke.cpp`, keep
`std::array<OperationExpectation, 57>` **exactly as MAR-169 left it** and add
behavioural coverage per design spec §19.5:

- each of `"ease"`, `"ease_in"`, `"ease_out"`, `"ease_in_out"` applied to a
  Transform key, verified through a follow-up dry run whose
  `previous_interpolation` equals the exact quadruple;
- `"linear"` and `"stepped"` unchanged;
- `[0.2, -0.4, 0.8, 1.6]` unchanged, including its overshoot round trip;
- `"ease-in"`, `"easeIn"`, and `"bounce"` each rejected with `invalid_request`
  and a byte-identical project;
- a multi-key preset call adding exactly one history entry, with `undo`
  restoring every key;
- `set_transform` with no `interpolation` still creating a Linear key.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_agent_dispatch_smoke -j4
./build/marrow_agent_dispatch_smoke
```

Expected: the registry is still 57 (that assertion passes) and the four new
token cases fail with `invalid_request`.

- [ ] **Step 3: Implement the token branch**

In `src/editor/agent_dispatch.cpp`, inside `interpolation_request_arg()`'s
string branch only, after the existing `"linear"` and `"stepped"` checks:

```cpp
if (const auto preset = marrow::editor::curve_preset_from_token(value->as_string())) {
    const auto& definition = marrow::editor::curve_preset_definition(*preset);
    *kind_out = definition.kind;
    *control_points_out = definition.control_points;
    return true;
}
```

Handling `"linear"` and `"stepped"` through the same table is acceptable and
preferable, since `kCurvePresets` carries both — but only if the resulting
behaviour is byte-identical to MAR-169's. Verify with the existing cases, not by
inspection.

Update the error string to
`"<name> is required and must be linear, stepped, ease, ease_in, ease_out, ease_in_out, or a 4-number bezier array."`

**Leave `interpolation_arg()` byte-identical.** Add no registry row. Change no
handler logic in `agent_handlers_editing.cpp` — the tokens resolve to a kind and
a quadruple before the handler ever sees them.

- [ ] **Step 4: Implement the MCP schema and its coverage**

In `tools/mcp/tools/editing.py`, widen `_bezier_interpolation_schema()`'s string
enum to the six tokens and extend its description to name each quadruple. Leave
`_interpolation_schema()`, `_timeline_interpolation_key_schema()`, every tool
name, and `get_tools()`'s length untouched.

In `tools/mcp/test_client.py`, keep `len(registry_names) == 57` and
`len(mcp_names) == 57` **unchanged**, and add per design spec §19.6:

```python
require_ok(
    "timeline.set_interpolation ease_in_out preset",
    await client.send_command(
        "timeline.set_interpolation",
        {"keys": [interpolation_key], "interpolation": "ease_in_out"},
    ),
)
read_back = require_ok(
    "timeline.set_interpolation preset read-back",
    await client.send_command(
        "timeline.set_interpolation",
        {"keys": [interpolation_key], "interpolation": "linear", "dry_run": True},
    ),
)
stored = read_back["scene_delta"]["keys"][0]["previous_interpolation"]
assert [round(value, 4) for value in stored] == [0.42, 0.0, 0.58, 1.0]

require_rejected(
    "timeline.set_interpolation rejects hyphenated preset tokens",
    await client.send_command(
        "timeline.set_interpolation",
        {"keys": [interpolation_key], "interpolation": "ease-in"},
    ),
)
require_ok("undo timeline preset", await client.send_command("undo"))
```

- [ ] **Step 5: Run the Agent/MCP gates and observe GREEN**

```bash
cmake --build build --target marrow_agent_dispatch_smoke marrow_editor_shell -j4
./build/marrow_agent_dispatch_smoke
./build/marrow_agent_socket_tests
./build/marrow_c_smoke
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
kill %1
```

Expected: the registry is **still exactly 57**, C++/Python parity is 57/57, the
socket tests are 4/4, and the C ABI smoke is unchanged.

- [ ] **Step 6: Prove no operation and no existing contract changed**

```bash
git diff -- src/editor/agent_dispatch.cpp
git diff --stat -- src/editor/agent_handlers_editing.cpp
git diff --stat -- include/marrow/c_api src/c_api
git diff -- tools/mcp/tools/editing.py
git diff --check
git status --short
```

Expected: the `agent_dispatch.cpp` diff touches only
`interpolation_request_arg()`'s string branch and its error string;
`agent_handlers_editing.cpp` and the C ABI paths show **zero** changes; the
`editing.py` diff touches only `_bezier_interpolation_schema()`.

---

### Task 8: Full validation, documentation, and MAR-170 closure

**Files:**

- Modify: `AGENTS.md`
- Modify: `docs/root1/discription.md`
- Modify: `docs/root1/quick-start.md`
- Modify: `docs/root1/concepts.md`
- Modify: `docs/root1/editing-gap-analysis.md`
- Modify: `docs/root1/refector.md`
- Modify: `docs/root1/platform-validation.md` (only the stale "next milestone" line)
- Modify: `docs/root1/format-spec.md` (only if MAR-169 left a `curve` paragraph to extend)
- Modify: `docs/superpowers/specs/2026-08-30-mar-170-curve-presets-and-defaults-design.md`
- Modify: `.agents/tasks/prd-marrow-runtime.json`

**Interfaces:**

- Consumes: completed Tasks 0–7 and fresh command output.
- Produces: synchronized documentation, exact validation evidence,
  `MAR-170 status=done`, and `MAR-171 status=open`.

- [ ] **Step 1: Run the fresh default/focused gate**

```bash
cmake -S . -B build &&
cmake --build build -j4 &&
./build/marrow_preference_tests &&
./build/marrow_timeline_graph_model_tests &&
./build/marrow_timeline_model_tests &&
./build/marrow_viewport_interaction_tests &&
./build/marrow_selection_tests &&
./build/marrow_project_smoke assets/fixtures/player_idle.marrow &&
./build/marrow_project_smoke --create /tmp/player_idle.marrow &&
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2 &&
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2 &&
ctest --test-dir build -N &&
ctest --test-dir build --output-on-failure
```

Expected: every command exits zero. MAR-170 registers no new CTest, so the
default suite should report the MAR-169 count; confirm with
`ctest --test-dir build -N` and document the actual justified count if it
differs.

- [ ] **Step 2: Run Debug and Release display-enabled gates**

```bash
cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON &&
cmake --build build-display -j4 &&
ctest --test-dir build-display --output-on-failure &&
cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON &&
cmake --build build-platform-release -j4 &&
ctest --test-dir build-platform-release --output-on-failure
```

Expected: both display-enabled suites pass with the MAR-169 counts, including
the three display tests. Label the evidence automated; claim no manual UI or
Windows qualification.

- [ ] **Step 3: Run export/runtime compatibility gates**

```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar170.mskl --export-binary /tmp/marrow_mar170.mbin
./build/marrow_inspect --compare /tmp/marrow_mar170.mbin /tmp/marrow_mar170.mskl
./build/marrow_fixture_smoke /tmp/marrow_mar170.mskl /tmp/player_idle.matl
python3 -m json.tool /tmp/marrow_mar170.mskl >/dev/null
```

Expected: export, JSON/MBIN comparison, and the runtime fixture smoke pass.
Record the **exact** comparison verdict, key counts, and output sizes from this
run rather than copying MAR-168/169's numbers. Replacing a Linear segment with a
cubic preset can change whether a segment stays packable by the MBIN AKEY
optimization, so the byte counts are expected to move; a `match` verdict is what
must hold.

- [ ] **Step 4: Run the Agent/MCP and integrity gates**

```bash
./build/marrow_agent_dispatch_smoke
./build/marrow_agent_socket_tests
./build/marrow_c_smoke
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
kill %1
cmake --build build --target marrow_verify_third_party
python3 -m json.tool assets/fixtures/player_idle.marrow >/dev/null
python3 -m json.tool .agents/tasks/prd-marrow-runtime.json >/dev/null
git diff --check
git lfs status
```

Expected: all commands exit zero, the Agent registry is **exactly 57** with
matching C++/Python parity, and no LFS object is staged or normalized.

- [ ] **Step 5: Prove the compatibility claim explicitly**

```bash
git diff --stat -- include/marrow/c_api src/c_api
git diff --stat -- src/editor/project.cpp src/runtime/skeleton_parse.cpp src/runtime/binary.cpp
git diff --stat -- src/editor/preferences.cpp include/marrow/editor/preferences.hpp
git diff -- include/marrow/editor/authoring.hpp
rg -n 'kEditorSettingsVersion' include/marrow/editor/preferences.hpp
rg -n '"version"' assets/fixtures/player_idle.mskl | head -3
```

Expected: the C ABI paths, the format parser/writer paths, **and the preferences
module** all show zero changes; the `authoring.hpp` diff contains only added
lines; `kEditorSettingsVersion` is still `1`. Together these are the concrete
proof that `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, and
`editor-settings.json` v1 are untouched because MAR-170 writes only into fields
that already existed.

- [ ] **Step 6: Verify the operation count did not move**

```bash
rg -n '\b57\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n '\b58\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n '\b56\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n 'timeline\.set_interpolation' src tools AGENTS.md docs
```

Expected:

- every `57` found in Task 0 Step 2 is still present and still 57;
- no `58` states a registry size anywhere;
- every remaining `56` is a **historical** `Validation Results` section, a dated
  checkpoint sentence, a past PRD acceptance criterion, or one of the known
  false positives (an `IM_COL32` channel, a colour table, a fixture coordinate,
  a byte size, a timestamp, a struct size);
- MAR-170's own new `AGENTS.md` section states that the surface **stayed** at 57
  and that the change was to one operation's argument vocabulary.

Apply the classification rule when in doubt: a sentence carrying a date, a
milestone ID, or the words "historical", "baseline", or "checkpoint … passed"
records what was true then; a sentence stating what the surface *is* must read
57 and, for MAR-170, already does.

- [ ] **Step 7: Synchronize user and architecture documentation**

Write only evidence supported by Steps 1–6. **Change no operation count.**

- `quick-start.md`: replace the MAR-169 boundary paragraph with the preset
  contract — six fixed presets in both timeline tabs, applied to every
  compatible selected key as one undo entry, incompatible keys skipped and
  reported, the `Default:` combo seeding newly added Transform/Deform/Slot Color
  keys, that default stored per user in `editor-settings.json` and never in the
  project, and automatic/project-local curve handles arriving in MAR-171.
- `concepts.md`: extend the easing paragraph — presets are six source constants
  written through the same `set_keyframe_interpolation()` primitive the handle
  drag and the Agent use, the "current preset" readout is a pure function of the
  stored four floats with no persisted marker, and the remembered default lives
  in the user preference store, not the project.
- `discription.md`: add a dated MAR-170 contract paragraph, make MAR-171 next,
  and update the milestone-history sentences that currently end at MAR-169.
  Extend line 36's MAR-156 paragraph with one sentence naming MAR-170 as the
  consumer of `default_curve` and restating that the preference still never
  touches project dirty/history/revision, the runtime formats, the C ABI, or the
  Agent/MCP surface. State that the Agent/MCP surface remains 57 operations.
- `editing-gap-analysis.md`: mark the MAR-170 roadmap row complete, update the
  "다음 직접 제품 milestone" line and the P1 chain sentence so MAR-171 is next,
  and note that the preset vocabulary was added to the existing
  `timeline.set_interpolation` rather than as a new operation. **Leave lines 23,
  83, and 190 at 57.**
- `refector.md`: update the `MAR-170–191` row and the sentences naming the next
  milestone. **Leave the registry-size sentences at lines 20 and 112 at 57**,
  and leave lines 161, 305, and 321 at 56 — they are the dated Task #28 record.
- `platform-validation.md`: update the "next product milestone" line to MAR-171
  without changing any qualification status.
- `format-spec.md`: **no new field and no version change.** If MAR-169 added the
  `[0, 1]` guarantee sentence to the `curve` encoding list, append one clause
  noting that all six fixed presets satisfy it by construction. If MAR-169 did
  not, make no change at all.
- `AGENTS.md`:
  - update the `Project State` line so MAR-170 is complete and MAR-171 is next;
  - **leave `Current Validation`'s registry line at 57 operations**; confirm
    `./build/marrow_preference_tests` is already listed (it is, at line 36) and
    extend its description to mention the curve-preset constants;
  - add a `MAR-170 Fixed Curve Presets and Remembered Defaults Validation
    Results` section using the MAR-168/169 table plus command-output format with
    these slices:

  | Slice | Verification | Result |
  | --- | --- | --- |
  | Preset constants | The six presets are Linear, Stepped, Ease `[0.25, 0.1, 0.25, 1]`, Ease-In `[0.42, 0, 1, 1]`, Ease-Out `[0, 0, 0.58, 1]`, Ease-In-Out `[0.42, 0, 0.58, 1]`; a compile-time assertion keeps every `cx` inside `[0, 1]` before and after float32 narrowing; each evaluates finite, monotone, and overshoot-free on `[0, 1]` | |
  | Preset application | Applying to compatible selected keys is order-independent and deterministic, skips and reports easing-free keys, collapses duplicates, materializes runtime-only tracks, and produces exactly one previewed undoable transaction; re-applying commits nothing | |
  | Remembered default | `default_curve` seeds newly authored Transform, Deform, and Slot Color keys; it is stored atomically in `editor-settings.json` v1 with `recent_projects` and unknown fields preserved; missing, malformed, unsupported-version, and unreadable data fall back to Linear and rewrite no curve and no file | |
  | Project isolation | Changing and saving the default leaves `serialize_project()`, `dirty()`, `undo_count()`, `redo_count()`, and `project_revision()` byte-identical; `MARROW_CONFIG_HOME` isolates every test process from the real user settings file | |
  | Curve identity | The current-preset readout is a pure bit-exact `float32` function of the stored curve; a preset survives save, reload, and export as the same preset; a handle drag makes it read `Custom`; a preset followed by a drag is exactly two undo entries | |
  | Agent and MCP parity | `timeline.set_interpolation` accepts the four new preset tokens with matching C++/Python dry-run, validation, affected-key, mutation, and undo behaviour; the registry stayed at **57** operations and `interpolation_arg()`, `set_transform`, `set_deform_keyframe`, and `set_slot_color_keyframe` are unchanged | |
  | Compatibility | `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1, and `SelectionSet` unchanged; only the pre-existing `curve` and `default_curve` fields are written | |

- design spec: change status from `Approved design, implementation not started`
  to `Implemented and validated` only after every gate is green.

- [ ] **Step 8: Close only MAR-170 in the PRD**

```json
{
  "id": "MAR-170",
  "status": "done",
  "completedAt": "<verified date>"
}
```

Keep MAR-171 open and dependent on MAR-170. Update the overview so the remaining
strict chain begins at MAR-171. Do not alter MAR-192 through MAR-210.

- [ ] **Step 9: Run documentation/roadmap integrity checks**

```bash
python3 -m json.tool .agents/tasks/prd-marrow-runtime.json >/dev/null
jq -e '
  (.stories[] | select(.id == "MAR-169") | .status) == "done" and
  (.stories[] | select(.id == "MAR-170") | .status) == "done" and
  (.stories[] | select(.id == "MAR-171") | .status) == "open" and
  ([.stories[] | select(.id >= "MAR-192" and .id <= "MAR-210") | .status] | all(. == "open"))
' .agents/tasks/prd-marrow-runtime.json >/dev/null
! rg -n "MAR-170 (is )?(the )?next|MAR-170.*status.*open" AGENTS.md docs/root1 .agents/tasks/prd-marrow-runtime.json
! rg -n "preset.*(is |remains )?MAR-170|MAR-170 경계" src docs
rg -n "MAR-171" AGENTS.md docs/root1/discription.md docs/root1/editing-gap-analysis.md .agents/tasks/prd-marrow-runtime.json
rg -n "57 operations|57-op" AGENTS.md docs/root1/editing-gap-analysis.md
git diff --check
```

Expected: JSON and jq checks pass, the stale "MAR-170 next/boundary" searches
return no match, current MAR-171 references exist, and the current-surface
documents still read 57.

- [ ] **Step 10: Perform requirement-by-requirement completion audit**

For every MAR-170 acceptance criterion, point to both source and executed
evidence:

1. **Presets are Linear, Stepped, Ease `[0.25, 0.1, 0.25, 1]`, Ease-In
   `[0.42, 0, 1, 1]`, Ease-Out `[0, 0, 0.58, 1]`, Ease-In-Out
   `[0.42, 0, 0.58, 1]`** — `kCurvePresets` plus the `static_assert`, and
   `marrow_preference_tests`' literal-valued constant case, its float32
   narrowing case, and its runtime well-posedness case.
2. **Applying a preset to compatible selected keys is deterministic and creates
   one previewed undoable transaction** — `apply_timeline_curve_preset()`'s
   single `set_keyframe_interpolation()` call inside one `EditTransaction`; the
   project smoke's order-independence assertion; the headless scenario's
   one-history-entry, skip-count, empty-selection, re-apply, and undo/redo
   cases; the actual-frame click case.
3. **The last chosen default initializes newly authored continuous segments and
   is atomically stored in `editor-settings.json` without dirtying the project**
   — the three seeding sites; the headless default-seeding cases for Transform,
   Deform, and Slot Color and the Stepped default case; the paste-is-not-reseeded
   case; the project-untouched assertion over `serialize_project()`, `dirty()`,
   `undo_count()`, `redo_count()`, and `project_revision()`; MAR-156's atomic
   temp-plus-rename write, unchanged.
4. **Missing or invalid preference data falls back to Linear and never rewrites
   existing curves** — the §8.3 fallback matrix, re-asserted row by row in
   `marrow_preference_tests`; the headless malformed-settings case asserting the
   default is Linear and no keyframe changed; the assertion that a load performs
   no write and a first run creates no file.
5. **GUI, C++ agent, and Python MCP preset application share the interpolation
   operation and preserve segment-wide easing** — all three paths call
   `set_keyframe_interpolation()`, which has no component parameter; the
   registry stayed at 57; `marrow_agent_dispatch_smoke`'s four token cases; the
   headless display-Y/change-X case and the project smoke's
   "exactly one `interpolation` difference" assertion.
6. **Preference, project, shell, agent, and MCP tests cover constants,
   `MARROW_CONFIG_HOME` restoration, undo grouping, save/reload, and export** —
   `marrow_preference_tests` (constants, tokens, fallback, preservation,
   environment restoration), `marrow_project_smoke`
   (`validate_mar170_curve_presets`, save/reload preset identity, export),
   `marrow_editor_shell` (the headless scenario and the actual-frame cases,
   including the preset-then-drag two-entry case),
   `marrow_agent_dispatch_smoke`, and `tools/mcp/test_client.py`.

Treat any missing or indirect evidence as incomplete. Fix via a new RED-GREEN
cycle, rerun the affected focused test, then rerun the full gate that supports
the claim.

- [ ] **Step 11: Final no-commit checkpoint**

```bash
git status --short
git diff --stat
git diff --check
git lfs status
diff <(ls -la "$HOME/Library/Application Support/Marrow/" 2>/dev/null) /tmp/mar170_settings_before.txt
```

Report the changed files, fresh test totals, export comparison metrics, that the
operation count is unchanged at 57, the exact remaining qualification
limitations, that the real user settings file was never touched, and that no
commit, push, or reset occurred.

---

## Full verification checklist

Every command below must pass before MAR-170 is considered complete.

```bash
# Configure and build
cmake -S . -B build
cmake --build build -j4

# Focused UI-free tests
./build/marrow_preference_tests
./build/marrow_timeline_graph_model_tests
./build/marrow_timeline_model_tests
./build/marrow_viewport_interaction_tests
./build/marrow_selection_tests

# Project and shell smokes
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/player_idle.marrow
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2

# Default CTest registry
ctest --test-dir build -N
ctest --test-dir build --output-on-failure

# Debug display-enabled suite
cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON
cmake --build build-display -j4
ctest --test-dir build-display --output-on-failure

# Release display-enabled suite
cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON
cmake --build build-platform-release -j4
ctest --test-dir build-platform-release --output-on-failure

# Export, compare, and runtime fixture gates
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar170.mskl --export-binary /tmp/marrow_mar170.mbin
./build/marrow_inspect --compare /tmp/marrow_mar170.mbin /tmp/marrow_mar170.mskl
./build/marrow_fixture_smoke /tmp/marrow_mar170.mskl /tmp/player_idle.matl

# Agent and MCP gates (the surface MAR-170 must keep at 57)
./build/marrow_agent_dispatch_smoke
./build/marrow_agent_socket_tests
./build/marrow_c_smoke
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
kill %1

# Repository integrity
cmake --build build --target marrow_verify_third_party
python3 -m json.tool assets/fixtures/player_idle.marrow >/dev/null
python3 -m json.tool assets/fixtures/player_idle.mskl >/dev/null
python3 -m json.tool .agents/tasks/prd-marrow-runtime.json >/dev/null
git diff --check
git lfs status

# Compatibility proof
git diff --stat -- include/marrow/c_api src/c_api
git diff --stat -- src/editor/project.cpp src/runtime/skeleton_parse.cpp src/runtime/binary.cpp
git diff --stat -- src/editor/preferences.cpp include/marrow/editor/preferences.hpp
git diff -- include/marrow/editor/authoring.hpp
rg -n 'kEditorSettingsVersion' include/marrow/editor/preferences.hpp

# Operation-count consistency (must be unchanged)
rg -n '\b57\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n '\b58\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n '\b56\b' --glob '!build*' src tools AGENTS.md docs .agents

# Preference isolation proof
diff <(ls -la "$HOME/Library/Application Support/Marrow/" 2>/dev/null) /tmp/mar170_settings_before.txt
```

Expected results to record in `AGENTS.md`:

- preference tests: all cases pass, including the new preset-constant,
  token-agreement, float32-narrowing, runtime-well-posedness, identity,
  fallback-matrix, and environment-restoration cases;
- graph model and timeline model tests: all cases pass, unchanged;
- project smoke: passes, including every new MAR-170 preset case and the
  save/reload preset-identity assertion;
- shell smoke: passes, including the new headless preset scenario and the
  actual-frame preset-button frames;
- default CTest: unchanged count (MAR-170 adds no CTest; record the actual count
  from `ctest --test-dir build -N`);
- Debug and Release display suites: unchanged counts, including 3 display tests;
- `marrow_inspect --compare`: match, with freshly recorded key counts and byte
  sizes;
- `marrow_agent_dispatch_smoke`: **still exactly 57** operations;
- `tools/mcp/test_client.py`: 57/57 C++/Python parity, plus the preset token
  sequence;
- `git diff --stat` over `include/marrow/c_api`, `src/c_api`,
  `src/editor/project.cpp`, `src/runtime/skeleton_parse.cpp`,
  `src/runtime/binary.cpp`, `src/editor/preferences.cpp`, and
  `include/marrow/editor/preferences.hpp`: **empty**;
- every `57` in the tree still states 57, no `58` states a registry size, and
  every remaining `56` is a historical record or a known false positive;
- the real user `editor-settings.json` directory is byte-identical before and
  after the full run.

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.
