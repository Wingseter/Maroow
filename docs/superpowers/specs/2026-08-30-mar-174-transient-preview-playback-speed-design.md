# MAR-174 Transient Preview Playback Speed — Design

- **Story**: MAR-174, "Control transient preview playback speed"
- **Depends on**: MAR-173 (Scale selected key times atomically)
- **Date**: 2026-08-30
- **Branch**: `feat/mar-168`
- **Registry effect**: **none**. MAR-174 adds no Agent operation and no MCP tool.

## 1. Summary

Add one bounded, strictly positive multiplier to the Timeline transport that scales
how fast preview playback advances. It is shell-private transient state: one
`double` on `ShellState`, one clamped accessor, one setter, and one multiplication
at the single site where preview time actually advances.

The whole story is that multiplication plus the proof that nothing else can see it.
The interesting work is not the arithmetic — it is the non-effect gate: after any
sequence of speed changes the serialized project must be **byte-identical**, the
history must be **the same length**, all three session revisions must be
**unchanged**, the runtime export must be **byte-identical**, and the operation
registry must be **the same size**.

MAR-168's design already listed "preview playback speed" as one of its explicit
non-goals (§14). This is the story that owns it.

## 2. Acceptance Criteria → Design

| # | Criterion | Where it is satisfied |
|---|---|---|
| 1 | Finite values from 0.05x through 8x, presets 0.25/0.5/1/2 | §5 bounds + §6 setter contract + §7 UI |
| 2 | Multiplies progression; composes with reverse, looping, scrubbing, pause | §8 arithmetic, §8.2 reverse, §8.3 loop, §8.4 scrub/pause |
| 3 | Never serializes, dirties, enters history, or alters export | §9 non-observation surfaces + §11.6 non-effect gate |
| 4 | Open/reload/replace resets through one documented session default | §10 reset |
| 5 | Shell smokes cover bounds, presets, forward/reverse, loop, pause/resume, unchanged dirty/history; no agent operation | §11 test design, §12 registry |

## 3. As-Built Facts This Design Rests On

Measured on the current tree, not assumed. Task 0 of the plan re-measures every
one of these against the as-built MAR-172/MAR-173 code before any edit.

1. **`advance_timeline_playback()` is the sole progression site.**
   `src/editor/timeline_controller.cpp:762`. Its whole body is a guard, a
   `set_playing(true)`, `state->session.advance(delta_seconds)`, and a sync. It is
   called from exactly two places: `src/editor/shell_main.cpp:550` (the real frame
   loop) and `src/editor/shell_smoke_frames.cpp:58` (the headless frame loop). Both
   pass `io.DeltaTime`.

2. **`EditorSession::advance()` forwards one delta into two consumers.**
   `src/editor/session.cpp:1914` → `PreviewImpl::advance()` at `session.cpp:668`,
   which does `state_.time_seconds + delta_seconds`, then either
   `std::fmod(next_time, duration)` when looping or a clamp-to-`duration` +
   `playing = false` when not, and then hands **the same delta** to
   `animation_state_->update(delta_seconds)`. Scaling the one argument therefore
   scales the displayed time, the sampled pose, the queue/mix crossfade, and event
   dispatch consistently, by construction.

3. **`advance()` refuses a non-positive or non-finite delta.**
   `session.cpp:668-678`: `if (!state_.playing || delta_seconds <= 0.0) return false;`
   then `if (!std::isfinite(delta_seconds) …) return false;`. A negative product is
   a silent no-op today.

4. **The runtime has no `timeScale` concept.** `grep -rn 'time_scale|timeScale|speed'`
   over `src/runtime/**` and `include/marrow/runtime/**` returns nothing. There is
   nothing to reuse and nothing to keep in step.

5. **Reverse is a sampling transform, not a direction of travel.**
   `sample_time_for_pose()` (`src/runtime/animation_state.cpp:36-71`) maps a
   monotonically increasing `track_time` to `duration - fmod(track_time, duration)`
   when `entry.reverse`. Track time always moves forward. Magnitude and direction
   are already orthogonal in the runtime; this design keeps them that way.

6. **Event dispatch requires forward time.**
   `animation_state.cpp:930-935` early-returns on `current_time <= previous_time`.
   A backwards step drops every event on that frame.

7. **Preview state is captured into history but does not create history entries.**
   `capture_history_snapshot()` (`shell_core.cpp:122`) copies
   `playing`/`reverse`/`loop`/`time` into `snapshot.preview_state`, and
   `assign_history_snapshot()` writes them back on undo — but
   `history_snapshots_equal()` (`shell_core.cpp:163`) compares only
   `serialized_project`, `preview_skin_names`, and `preview_slot_overrides`. This is
   the trap this design avoids: **speed must not join `marrow::editor::PreviewState`**,
   or undo would silently rewrite it.

8. **`reload_project()` is the single open/reload/replace entry point.**
   `shell_core.cpp:531`. It branches internally on `reload_current_project`
   (`session.reload()` vs `session.open()`), so one assignment above the branch
   covers all three of AC 4's words.

9. **Every authoring gesture already pauses playback.** Gesture-begin paths set
   `timeline_playing = false` (`shell_timeline.cpp:183,368`,
   `shell_timeline_graph.cpp:1424`, `viewport_ffd_controller.cpp:667`,
   `viewport_interaction_controller.cpp:668`). On top of that,
   `EditorSession::advance()` returns `false` outright while
   `impl_->active_transaction` is set.

10. **`idle` in `assets/fixtures/player_idle.marrow` has an inferred duration of
    exactly `1.0` s** (no `duration` member; the largest key time is `1.0`). Every
    numeric expectation in §11 is derived from that.

11. **Clamping is the house idiom for transient preview scalars.**
    `normalize_state_preview_settings()` (`shell_core.cpp:70`) does
    `preview_custom_mix_duration = std::max(0.0, …)` and the same for
    `preview_queue_delay`. Atomic rejection is the idiom for *authoring* operations
    that open a transaction. Speed opens none.

## 4. Why Editor-Side, Not Runtime-Side

The alternative is a `time_scale` on `marrow::runtime::TrackEntry`, applied inside
`AnimationState::update()`.

**Rejected.** For a single-track preview, `entry.time_scale * delta` inside the
runtime and `delta * speed` at the shell call site are the same number reaching the
same accumulator — fact 2 above shows the one delta is the only input. The runtime
version costs a new `TrackEntry` field, a `.mskl` schema question, an `.mbin`
version question, a C ABI surface question, and a permanent maintenance obligation,
all to buy nothing this story needs. The shell version costs one multiply.

MAR-174 therefore leaves `src/runtime/**`, `include/marrow/runtime/**`,
`src/c_api/**`, and `include/marrow/c_api/**` at a **zero-byte diff**. That is a
verification gate, not a hope (§13).

## 5. The Value Domain

```
kPreviewSpeedMinimum  = 0.05    // AC 1's floor
kPreviewSpeedMaximum  = 8.0     // AC 1's ceiling
kDefaultPreviewSpeed  = 1.0     // AC 4's "one documented session default"
kPreviewSpeedPresets  = { 0.25, 0.5, 1.0, 2.0 }   // AC 1's four presets
```

- **Continuous, not quantized.** AC 1 says "accepts finite values from 0.05x through
  8x" *and* "exposes … presets". Quantizing to the preset set would make the first
  half of the sentence false. The presets are shortcuts onto a continuous domain,
  not the domain itself. Every preset is inside `[0.05, 8.0]`, asserted in the smoke.
- **Strictly positive.** `0.0` is not in the domain. Neither is any negative value.
  See §8.2 and §8.5.
- **`1.0` is both the default and a preset**, so "return to normal" is one click and
  the reset target is a value the user can also reach by hand.

## 6. Shell Surface

All three additions are shell-private. No header under `include/marrow/` changes.

### 6.1 `src/editor/shell_state.hpp`

One field on `ShellState`, placed next to `preview_reverse` so the transport's
transient scalars sit together:

```cpp
// MAR-174: transient preview transport speed. Shell-private and strictly
// positive; direction is preview_reverse's job. Never serialized, never in
// PreviewState, never in EditorHistorySnapshot, never in EditorPreferences.
// reload_project() resets it to kDefaultPreviewSpeed.
double preview_speed{kDefaultPreviewSpeed};
```

plus the four constants of §5 beside the existing `kOnionSkinFrameRate` group, and
one inline defensive accessor beside `authoring_gesture_active()`:

```cpp
/** Clamped read of the transient preview speed. The single choke point:
    a corrupted or non-finite field can never reach EditorSession::advance(). */
inline double preview_playback_speed(const ShellState& state) noexcept {
    if (!std::isfinite(state.preview_speed)) {
        return kDefaultPreviewSpeed;
    }
    return std::clamp(state.preview_speed, kPreviewSpeedMinimum, kPreviewSpeedMaximum);
}
```

### 6.2 `src/editor/timeline_controller.hpp` / `.cpp`

One ImGui-free setter, taking plain scalars so the headless smoke drives it directly
— the same discipline MAR-168 §11.4 established for its drag gestures:

```cpp
bool set_preview_playback_speed(
    ShellState* state,
    double speed,
    std::string_view source,
    bool update_status_message);
```

**Contract.**

| Input | Result | State |
|---|---|---|
| `NaN`, `+inf`, `-inf` | returns `false` | `preview_speed` bit-unchanged, no status message |
| finite, `< 0.05` (including `0.0` and negatives) | returns `true` | clamped to `0.05` |
| finite, `> 8.0` | returns `true` | clamped to `8.0` |
| finite, in range | returns `true` | assigned exactly |

It writes exactly one field plus, optionally, `status_message`. It calls **nothing**
on `EditorSession`, opens no transaction, records no history entry, and does not
consult `authoring_gesture_active()` (§8.6).

`advance_timeline_playback(ShellState*, double)` gains the multiply of §8.1. Nothing
else in the file changes.

### 6.3 `src/editor/shell_core.cpp`

One unconditional line in `reload_project()` (§10). No other change; in particular
`capture_history_snapshot()` and `assign_history_snapshot()` are **not** touched, and
that is deliberate (fact 7).

## 7. UI

One row addition in `draw_timeline_window()` (`src/editor/shell_timeline.cpp`),
immediately after the `Icon::Loop` transport toggle and before the Time slider:

```
[Rewind] [PrevKey] [Play/Pause] [NextKey] [Loop]  Speed [ 1.00x ] [0.25][0.5][1][2]
```

- `ImGui::SetNextItemWidth(90.0f)` then
  `ImGui::DragScalar("Speed", ImGuiDataType_Double, &speed, 0.01f,
   &kPreviewSpeedMinimum, &kPreviewSpeedMaximum, "%.2fx", ImGuiSliderFlags_AlwaysClamp)`.
- Four `ImGui::SmallButton` presets on the same line, labelled `0.25x` `0.5x` `1x` `2x`.
- Both paths call `set_preview_playback_speed(state, value, "Timeline", true)` and
  nothing else.
- Disabled together with the rest of the transport when
  `selected_animation(*state) == nullptr`.

**It is deliberately NOT inside the collapsible "Preview options" section.** Every
control in that block routes through the local `apply_state_preview_change` lambda
(`shell_timeline.cpp:2176-2180`), which sets `timeline_playing = false` and calls
`refresh_preview_pose()`. A speed change must not stop playback — changing speed
mid-playback and watching it take effect is the entire point of the control. Speed
lives on the transport row precisely because it must bypass that lambda.

## 8. Arithmetic

### 8.1 The multiply

```cpp
void advance_timeline_playback(ShellState* state, double delta_seconds) {
    if (!state->timeline_playing || delta_seconds <= 0.0) {
        return;
    }
    if (!state->animation_state || !state->preview_skeleton || !state->load_result) {
        state->timeline_playing = false;
        state->session.set_playing(false);
        return;
    }
    // MAR-174: the single site where preview time advances, so the single site
    // where the transient speed applies. preview_playback_speed() is clamped and
    // finite, and delta_seconds is already > 0, so the product is > 0 unless it
    // overflows to infinity on an absurd delta — which the guard below catches.
    const double scaled_delta = delta_seconds * preview_playback_speed(*state);
    if (!std::isfinite(scaled_delta) || scaled_delta <= 0.0) {
        return;
    }
    state->session.set_playing(true);
    (void)state->session.advance(scaled_delta);
    sync_shell_from_editor_session(state);
}
```

The `float` overload is unchanged; it already widens and delegates.

Only `advance_timeline_playback` is touched. In particular
`state->session.advance_parameter_state(io.DeltaTime)`, one line below the call site
in both frame loops, keeps the raw delta — see §14.

### 8.2 Reverse

Speed is a strictly positive **magnitude**. Direction stays entirely with the
existing `preview_reverse` toggle. Three reasons, each grounded in as-built code:

1. `PreviewImpl::advance()` returns early on `delta_seconds <= 0.0` (fact 3), so a
   negative product is a silent no-op — a "reverse" that does nothing.
2. `AnimationState`'s event dispatcher returns on `current_time <= previous_time`
   (fact 6), so a backwards step would drop every event that frame.
3. The runtime already models reverse as a sampling transform over a monotonically
   increasing track time (fact 5). Magnitude and direction compose by construction,
   and there is nothing to reconcile. A negative speed would create a second
   direction control and make `reverse` XOR `sign(speed)` ambiguous for no gain.

Composition is therefore exact: at any speed, `state.timeline_time_seconds` (the
track time) follows the same forward progression, and the sampled pose is
`duration - fmod(track_time, duration)`. The smoke asserts both halves (§11.4).

### 8.3 Loop boundaries

`PreviewImpl::advance()` wraps with `std::fmod(next_time, duration)` — a single
exact modulo, not an accumulated subtract-while-loop. A `scaled_delta` spanning
several whole periods therefore lands on the mathematically correct phase in one
step, with no per-period rounding to accumulate. Speed cannot cause drift.

Speed also cannot *skip* MAR-172's managed boundary key, because looped playback
never samples that key at **any** speed, including 1.0: `fmod` yields
`[0, duration)`, so time exactly `duration` is not in the sampled range. MAR-172's
contract makes the key at `duration` a bit-for-bit copy of the time-zero key, so the
wrap is value-continuous regardless of where in the period a frame lands. Under
reverse + loop, `sample_time_for_pose()` returns `0.0` rather than `duration` when
`|fmod| <= kEpsilon`; the same MAR-172 equality makes those two answers agree.

Events across a multi-period step are dispatched by
`dispatch_events_for_entry()`'s `start_loop`/`end_loop` floor walk
(`animation_state.cpp:945-980`), which iterates every crossed period. Nothing is
skipped at 8x.

Non-looping: `next_time >= duration` clamps to exactly `duration` and clears
`playing`. Speed can reach the end sooner but can never overshoot past it, and the
terminal state at 8x is identical to the terminal state at 1x.

### 8.4 Scrubbing and pause

- **Scrubbing is absolute, so speed does not touch it.** AC 2 says speed "multiplies
  preview time progression"; `scrub_timeline_time()` sets a position, not a rate.
  `scrub_timeline_time()` is unmodified and the smoke asserts a scrub lands on its
  exact requested time at 8x (§11.5).
- **Pause is `timeline_playing`, untouched.** `advance_timeline_playback()` still
  returns immediately when paused, at every speed. Resuming resumes at the current
  speed. Speed never reads or writes `timeline_playing`.

### 8.5 Zero and out-of-range

`0.0` is finite and below the floor, so it clamps to `0.05`. **Zero is not a pause.**
AC 1 fixes the floor at 0.05x, so a "0x means pause" reading would put a value
outside the stated accepting range into the accepting range, and would duplicate a
control that already exists.

Finite out-of-range values clamp rather than reject (fact 11): clamping is what the
shell already does for `preview_queue_delay` and `preview_custom_mix_duration`, and
the DragScalar clamps identically via `ImGuiSliderFlags_AlwaysClamp`, so the UI and
programmatic paths agree. Non-finite values reject outright and leave the field bit
-unchanged — there is no sensible clamp target for `NaN`, and silently substituting
one would hide a caller bug.

The `preview_playback_speed()` accessor clamps a second time at the point of use, so
even a directly-poked field can never reach `EditorSession::advance()`.

### 8.6 During an authoring gesture

A speed change is **allowed** mid-gesture and is provably inert:

- `set_preview_playback_speed()` opens no transaction and calls nothing on the
  session, so it cannot disturb a live one.
- Every gesture-begin path already sets `timeline_playing = false` (fact 9), so
  playback is not running to be affected.
- `EditorSession::advance()` refuses outright while `active_transaction` is set, so
  even a forced `advance_timeline_playback()` call mid-transaction moves no time.

The setter therefore deliberately does **not** consult `authoring_gesture_active()`.
Adding that gate would add a coupling with no observable difference. The smoke proves
the claim by opening a real `EditTransaction`, changing speed, and asserting
`undo_count()` is unchanged, the transaction is still live, time did not move, and
the project round-trips byte-identically after rollback (§11.7).

## 9. Surfaces That Must Not Observe Speed

Enumerated so the plan can gate each one. Speed appears in **none** of:

| Surface | Why it must not, and how it is prevented |
|---|---|
| `ProjectData`, `.marrow` JSON, `serialize_project()` | AC 3. The field lives on `ShellState`, which is never serialized. |
| `EditorMetadata` (`preview_skins`, `active_animation`, `viewport`, `timeline`) | Same. No metadata member is added. |
| `marrow::editor::PreviewState` | Fact 7 — joining it would make undo rewrite speed. `include/marrow/editor/session.hpp` is unchanged. |
| `EditorHistorySnapshot`, `capture_history_snapshot()`, `assign_history_snapshot()` | Follows from the row above; those functions are untouched. |
| Undo / redo | Not in the snapshot, so undo cannot restore or clobber it. |
| `session.dirty()`, `ShellState::project_dirty` | `dirty()` is a project-serialization comparison; nothing in it changes. |
| `project_revision()`, `runtime_revision()`, `preview_revision()` | The setter makes zero session calls, so no counter can move. |
| `.mskl` / `.mbin` runtime export | Export reads `ProjectData` and the base document; neither sees the field. |
| `EditorPreferences`, `editor-settings.json` | §10.2. Zero-byte diff on `preferences.cpp`/`.hpp`. |
| Agent operation registry, MCP facade, `AgentControlState` | §12. |
| `marrow::runtime::TrackEntry` / `AnimationState` | §4. Zero-byte diff on `src/runtime/**`. |

## 10. Reset and Persistence

### 10.1 Reset

AC 4: "Opening, reloading, or replacing a project resets speed through one documented
session default." `reload_project()` is the sole entry point for all three (fact 8),
and it branches internally. One unconditional assignment above the branch, placed
with the other transport resets:

```cpp
state->timeline_playing = false;
state->preview_speed = kDefaultPreviewSpeed;   // MAR-174
```

"One documented session default" = the single constant `kDefaultPreviewSpeed`, which
is also the `ShellState` member initializer, so a fresh shell and a reloaded shell
start identically. Exactly two textual uses of the constant in production code.

Speed does **not** reset on:

- **Animation change.** AC 4 enumerates project-level events only. An animator who
  sets 0.25x to study a fast clip and then switches clips expects to stay at 0.25x;
  resetting there would fight the workflow and is not asked for.
- **Undo / redo.** It is not in the history snapshot at all (§9). Undo leaves it
  exactly where the user put it, which is the correct behaviour for a display control.

### 10.2 Persistence: no

The speed is **not** written to `EditorPreferences` / `editor-settings.json`.

Three reasons, in increasing force. (a) The story title says *transient*. (b) AC 3
forbids serialization. (c) Decisively: AC 4 resets the speed on every project open,
and a project open is the first thing that happens after startup — so a persisted
value would be overwritten before a single frame of playback could observe it. A
persisted-then-immediately-reset preference is a contradiction, not a feature.

`editor-settings.json` v1 is therefore unchanged, and no MAR-174 production code path
touches `PreferenceStore()`. The smoke still installs `ScopedPreferenceIsolation`
because it constructs a `ShellState` and calls `reload_project()`, which is the
existing house rule for every scenario in the shell smoke, not a MAR-174 requirement.

## 11. Test Design

One new scenario, `validate_preview_playback_speed_shell_smoke()`, in
**`src/editor/shell_smoke_timeline.cpp`** — the transport/preview smoke TU, not
`shell_smoke_graph.cpp`. Two reasons: this is transport behaviour rather than graph
behaviour, and MAR-173 is concurrently adding `validate_timeline_scale_shell_smoke()`
to `shell_smoke_graph.cpp`, so this placement removes the merge conflict entirely.

Declared in `shell_smoke_scenarios.hpp`, called from `shell_smoke.cpp` after
MAR-173's scenario. No CMake change: both files are already in the
`marrow_editor_shell` target (`CMakeLists.txt:878-879`).

Preamble: `ScopedPreferenceIsolation("preview-speed")` (asserting `installed()`),
`reload_project()` on `player_idle.marrow`, `set_selected_animation(… "idle" …)`,
`session.clear_history()`. `idle` has duration exactly `1.0` s (fact 10). All
tolerances are `1e-9`.

### 11.1 Registry guard

`marrow::editor::agent_operation_descriptor_count()` equals the count Task 0
measured. Re-asserted at the end of the scenario, unchanged (§12).

### 11.2 Domain

- Default on a fresh `ShellState`: `preview_speed == 1.0`.
- `set_preview_playback_speed(&state, 0.0, …)` → returns `true`, value `0.05`.
- `(-3.0)` → `true`, value `0.05`. **Negative never means reverse.**
- `(100.0)` → `true`, value `8.0`.
- `(0.05)`, `(8.0)` → `true`, assigned exactly.
- `NaN`, `+inf`, `-inf` → each returns `false` and leaves the previous accepted value
  bit-identical (compared with `std::memcmp` on the two doubles, not `==`, so a
  `NaN` write cannot pass).
- Each of `{0.25, 0.5, 1.0, 2.0}` assigns exactly and lies inside `[0.05, 8.0]`.

### 11.3 Forward progression

From `t = 0`, `timeline_playing = true`, `timeline_loop = true`, delta `0.25`:

| speed | expected `timeline_time_seconds` |
|---|---|
| 1.0 | 0.25 |
| 2.0 | 0.5 |
| 0.25 | 0.0625 |
| 0.5 | 0.125 |
| 8.0 | 0.0 (`fmod(2.0, 1.0)`) |
| 0.05 | 0.0125 |

Plus the **equivalence law**: N advances at `(speed s, delta d)` leave the same time
as N advances at `(speed 1.0, delta s*d)`, for `s ∈ {0.25, 2.0}` and `N = 3`. This is
what "multiplies preview time progression" means, stated as an assertion.

### 11.4 Reverse

`preview_reverse = true`, seek 0, speed 2.0, delta 0.25:

- `timeline_time_seconds == 0.5` — identical to forward, proving speed is a magnitude
  that does not alter direction.
- The sampled `spine` bone pose at that track time **differs** from the forward pose
  at the same track time (reverse is genuinely in effect at 2x), and **matches** the
  forward pose at `duration - t` within `1e-6`.

### 11.5 Loop, clamp, scrub, pause

- **Loop wrap.** `timeline_loop = true`, seek `0.9`, speed `8.0`, delta `0.25`
  (`+2.0`, two whole periods). Assert `t == 0.9`, `timeline_playing` still `true`,
  and the `spine` world transform equals the one reached by walking to `0.9` at speed
  1.0 within `1e-6` — no drift across a multi-period step.
- **Non-loop clamp.** `timeline_loop = false`, seek `0.9`, speed `8.0`, delta `0.25`.
  Assert `t == 1.0` exactly and `timeline_playing == false`. Speed reaches the end
  sooner; it cannot overshoot it.
- **Scrub independence.** speed `8.0`, `scrub_timeline_time(&state, 0.3, …)` →
  `t == 0.3` exactly.
- **Pause.** `timeline_playing = false`, speed `8.0`,
  `advance_timeline_playback(&state, 0.25)` → `t` unchanged. Then resume and assert
  the next advance moves by `0.25 * 8.0` from where it paused.

### 11.6 The non-effect gate — the core assertion

Capture, with no `advance` calls anywhere between the capture and the compare:

```
before_json   = serialize_project(*state.session.project())
before_undo   = state.session.undo_count()
before_redo   = state.session.can_redo()
before_proj   = state.session.project_revision()
before_rt     = state.session.runtime_revision()
before_prev   = state.session.preview_revision()
before_ops    = agent_operation_descriptor_count()
```

Replay every accepted and every rejected call from §11.2, then assert **all seven are
identical**, `session.dirty()` is still `false`, and `state.project_dirty` is still
`false` after `update_project_dirty_state(&state)`.

`preview_revision()` is included deliberately: the setter must make zero session
calls, so even the cheapest preview counter must not move. That is a sharper gate
than dirty-state alone and is what actually catches an accidental
`session.set_*()` in the setter.

**Export gate.** Using this TU's existing `save_project` +
`materialize_temp_project_runtime_assets` + `export_runtime_skeleton` pattern
(`shell_smoke_timeline.cpp:1165-1178`), export the runtime skeleton and binary to two
paths inside the isolation directory — once before the speed batch and once after —
and byte-compare both pairs with `std::ifstream` reads. Both must be **byte-identical**.

This is the right gate for MAR-174. MAR-173's planner correctly noted that a pure
time-only edit leaves `.mbin` size unchanged, so "size moved" does not discriminate.
Here the discriminating signal is the opposite and unambiguous: a transient control
must move **nothing at all**, and byte-identity proves exactly that.

### 11.7 Mid-transaction inertness

Open an `EditorSession::EditTransaction` via `session.begin_edit(…)` (the pattern at
`shell_smoke_graph.cpp:3996`). While it is open:

- `set_preview_playback_speed(&state, 4.0, …)` returns `true` and assigns.
- `session.undo_count()` is unchanged and the transaction is still valid.
- `advance_timeline_playback(&state, 0.25)` leaves `t` unchanged (`advance()`
  refuses while a transaction is live).

Roll the transaction back and assert `serialize_project(...)` is byte-identical to
the pre-transaction capture.

### 11.8 Reset

- Set `4.0`, then `reload_project(&state)` with `project_path` unchanged (the
  `session.reload()` branch) → `preview_speed == 1.0`.
- Set `4.0`, point `project_path` at a copy of the project saved into the isolation
  directory, then `reload_project(&state)` (the `session.open()` branch) →
  `preview_speed == 1.0`.

Both `reload_project()` branches are exercised, so "opening, reloading, or replacing"
is covered by construction rather than by assertion about which word maps to which
call.

## 12. Registry: Unchanged

**MAR-174 adds no Agent operation and no MCP tool, and the registry count does not
change.** Speed is a display control with no project effect, so exposing it to an
agent would give the agent a lever that provably does nothing to the document — the
opposite of the UI-free-primitive discipline, which exists so that agent and GUI
paths reach *the same authoring domain*. There is no authoring domain here.

Concretely, MAR-174 does not edit `src/editor/agent_dispatch.cpp`,
`src/editor/agent_dispatch_smoke.cpp`, `tools/mcp/**`, or any count prose in
`AGENTS.md` / `docs/root1/*.md`. The as-built count is 59 today
(`std::size(kOperationSpecs)`, measured); MAR-173's plan takes it to 60. Whatever
MAR-173 leaves, MAR-174 leaves alone. MAR-174's only count reference is the guard
inside its own new scenario, which Task 0 sets to the measured value.

This also makes MAR-174 merge-safe against the concurrent MAR-173 work: the two
stories share no file except `shell_smoke_scenarios.hpp` and `shell_smoke.cpp`, where
each adds one declaration and one call in different places.

## 13. Compatibility Gates

Asserted as zero-byte diffs, not as claims:

- `src/runtime/**`, `include/marrow/runtime/**`, `src/c_api/**`,
  `include/marrow/c_api/**` — `.mskl` v1, `.mbin` v2, C ABI v1 untouched (§4).
- `src/editor/preferences.cpp`, `include/marrow/editor/preferences.hpp` —
  `editor-settings.json` v1 untouched (§10.2).
- `include/marrow/editor/session.hpp`, `src/editor/session.cpp` — `PreviewState` and
  the preview/history contract untouched (fact 7, §9).
- `include/marrow/editor/project.hpp`, `src/editor/project.cpp` — the project schema
  and exporter untouched (§9).
- `src/editor/agent_dispatch*.cpp`, `tools/mcp/**` — the agent surface untouched (§12).

## 14. Non-Goals

- **Anything in MAR-175 ("Unify manual weight authoring")**: the canonical weight
  primitive, the Replace brush, the active-vertex numeric influence table,
  selected-scope Normalize, setup-pose Rebind, and the weight bind / numeric agent
  operations. MAR-174 touches no weight code and adds no agent operation, so it moves
  MAR-175's registry baseline not at all.
- **Anything in MAR-173**: selection-edge time scaling, pivots, event ties, neighbour
  rejection. MAR-174 changes no key time.
- Per-animation, per-track, or per-lane speed. One shell-wide preview speed.
- Speed ramps, easing of the speed itself, or keyframed speed.
- Scaling `advance_parameter_state()`. Parameter Modeling mode disables the Play
  button outright (`shell_timeline.cpp:2306`), so timeline playback does not run
  there and there is no composition question to answer.
- Scaling onion-skin sampling. `kOnionSkinFrameDuration` is an absolute time offset,
  not a rate; speed is a rate multiplier and does not apply.
- A runtime `TrackEntry::time_scale`, or any runtime, `.mskl`, `.mbin`, or C ABI change.
- Persistence in `editor-settings.json` (§10.2).
- Negative speed as a reverse mechanism (§8.2), or zero speed as a pause (§8.5).
- Keyboard shortcuts for speed. The existing global `Space`/`Home` shortcuts are
  untouched.
- Frame-time clamping or spiral-of-death protection. `io.DeltaTime` handling is
  pre-existing and unchanged; speed multiplies whatever it already receives.
- Audio, or any speed-linked media.
- Manual-visible-UI, Windows 11, or physical-input qualification credit. MAR-192
  through MAR-210 stay open.

## 15. Decisions Taken Under Ambiguity

1. **Continuous with preset shortcuts, not a quantized set.** AC 1 states a range
   *and* a preset list. Reading the presets as the whole domain would contradict
   "accepts finite values from 0.05x through 8x", so the range is the domain and the
   presets are shortcuts onto it.

2. **Speed is a positive magnitude; reverse stays the reverse toggle.** AC 2 says
   speed "composes predictably with the existing reverse toggle", which presupposes
   the toggle keeps owning direction. As-built code makes this the only workable
   reading: a negative delta is a silent no-op in `PreviewImpl::advance()` and drops
   every event in `AnimationState`. §8.2.

3. **Zero clamps to 0.05 rather than meaning pause.** AC 1 fixes the accepting range
   at `[0.05, 8]`; treating an out-of-range value as a distinct mode would put it back
   in range with different semantics, and pause already exists.

4. **Finite out-of-range clamps; non-finite rejects.** AC 1 does not say which. The
   shell's existing transient preview scalars clamp (fact 11); atomic rejection is
   reserved for transactional authoring, which this is not. `NaN` has no clamp target,
   so it rejects and leaves the field bit-unchanged.

5. **Reset on project open/reload/replace only, never on animation change or undo.**
   AC 4 lists project-level events. Extending the reset to animation selection would
   fight the workflow the control exists for; extending it to undo would require
   putting speed in the history snapshot, which AC 3 forbids.

6. **Not persisted.** AC 3 forbids serialization and AC 4 resets on every open, so a
   persisted value could never be observed. §10.2.

7. **No agent operation, registry unchanged.** AC 5 says so outright ("no agent
   operation is added"); §12 gives the reason the story is right to say it.

8. **The test scenario lives in `shell_smoke_timeline.cpp`, not
   `shell_smoke_graph.cpp`.** The story is transport behaviour, and MAR-173 is
   concurrently editing the graph smoke TU. This is a merge-safety decision as much
   as a taxonomy one.

9. **Speed is allowed mid-gesture rather than gated.** The ambiguity is whether
   `set_preview_playback_speed()` should mirror the `authoring_gesture_active()` gate
   that `dispatch_agent_command()` uses. It should not: that gate protects live
   transactions, and this setter cannot reach one. §8.6 proves the inertness instead
   of asserting it.

10. **The UI sits on the transport row, not in "Preview options".** The section's
    shared `apply_state_preview_change` lambda pauses playback on every change, which
    would defeat the control. §7.
