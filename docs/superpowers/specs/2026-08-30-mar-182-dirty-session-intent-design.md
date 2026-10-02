# MAR-182 Unify Dirty-Session Intent Handling — Design

Story: `MAR-182`, "Unify dirty-session intent handling".
Depends on: `MAR-181` (`43572f0`), which depends on `MAR-180` (`1a1ed33`).
Branch: `feat/mar-168`.

---

## 0. Where this story sits

The project-I/O arc is four stories:

| Story | What it owns |
|---|---|
| MAR-180 | Atomic `save`, Save As rebasing, `EditorSession::create`/`close`, failure-safe source adoption |
| MAR-181 | New / Open / Save / Save As as in-ImGui path modals on top of those primitives |
| **MAR-182** | **One Save / Discard / Cancel state machine in front of every path that can lose unsaved work** |
| MAR-183 | Recent projects in the versioned preference store |

MAR-181 built the seam for this story and then deliberately left the hole. Its
own record (`AGENTS.md:372-378`) states it plainly:

> **No dirty-session check exists anywhere in MAR-181.** A New or Open over a
> dirty session **discards unsaved work silently**. This is deliberate: MAR-182
> owns the intent state machine, and a partial check here is rework MAR-182 must
> undo.

This document closes that hole and, in doing so, also closes the three older
holes (Reload, Quit, OS window close) that were never guarded at all.

---

## 1. The measured gap

Everything in this section was read out of the tree at `43572f0` — the MAR-181
commit, and the anchor for **every `file:line` in this document**. Line numbers
move as soon as a task lands; the plan's Task 0 says how to tell an expected
shift from a substantive disagreement. Every claim carries a `file:line`. Section 12 records the four places where the governing
documents for this story — the MAR-181 header comments and the brief that
commissioned this design — turned out to be wrong.

### 1.1 There is no dirty check anywhere. Not one.

```
$ grep -rn "dirty" src/editor/shell_file_paths.cpp
575:    update_project_dirty_state(state);          # after a SUCCESSFUL save
696:    // purpose, and reload_project's hardcoded `project_dirty = false` would
```

One call, after a save that already succeeded, and one comment. `grep -rn
"unsaved" src/editor` finds two status strings and one panel chip label, no
control flow.

So the story title's premise — "unify", implying several inconsistent checks —
is **false as stated**. There are not several checks. There are **zero**. Five
distinct paths can destroy unsaved work and none of them asks. "Unify" is still
the right shape for the fix (one state machine, one entry point), but the work
is *construction*, not *reconciliation*, and the plan is sized accordingly.

### 1.2 Every path that can discard unsaved work, measured

| # | Path | Entry point | What it does today about dirtiness |
|---|---|---|---|
| 1 | **New Project…** | `shell_project_panels.cpp:723-725` → `begin_file_action(state, FileAction::New)` (`shell_file_paths.cpp:585`, New case `:591-595`) | Nothing. Raises the New form; a successful `Create` arms `pending_file_application`, and `apply_pending_file_action` (`shell_file_paths.cpp:686`) calls `session.create()`, which replaces the session outright |
| 2 | **Open Project…** | `shell_project_panels.cpp:726-728` → `begin_file_action(…::Open)` (`shell_file_paths.cpp:596-606`) | Nothing. Chooser → `commit_path_choice` (`shell_file_paths.cpp:172-178`) arms the deferral → `session.open()` (`shell_file_paths.cpp:653`) |
| 3 | **Reload Project** — *three* surfaces | menu `shell_project_panels.cpp:739-741`; toolbar icon `:643-646`; Project-panel icon `:936-942`. All three write `*reload_requested = true` | Nothing. Consumed at end of frame in **both** frame bodies (`shell_main.cpp:611-613`, `shell_smoke_frames.cpp:125-128`) → `reload_project` (`shell_core.cpp:625`) → `session.reload()`/`session.open()` |
| 4 | **Quit** | `shell_project_panels.cpp:743-745` returns `ProjectMenuAction::QuitRequested`; `shell_main.cpp:559-562` turns it into `window_host->request_close()` | Nothing. The loop condition `while (!window_host->should_close())` (`shell_main.cpp:784`) then exits |
| 5 | **Native OS close / `SDL_EVENT_QUIT`** | `sdl_window_host.cpp:78-83` latches `close_requested_ = true` inside `poll_events` | Nothing, and **nothing can veto it**: the latch is write-only from outside (`request_close()` at `:109` sets it; there is no clearer) |

Two more paths were checked and are **not** discard paths:

- **Runtime asset hot-reload.** `poll_runtime_asset_changes` →
  `EditorSessionShellBinding::adopt_runtime_sources` (`shell_asset_watch.cpp:153-155`)
  → `EditorSession::adopt_runtime_sources` (`session.cpp:1895`). Its commit block
  (`session.cpp:2001-2009`) replaces the four *runtime* fields and calls
  `impl_->update_dirty()`; the comment at `:2005-2007` states, and the code
  shows, that the authored `ProjectData` and the undo/redo stacks survive. An
  unsaved edit is preserved across a hot-reload. **No gate needed.**
- **Agent-driven load.** There is none. The 64-row registry
  (`agent_dispatch.cpp:29-96`) contains **no** open, create, reload, or close
  operation. The only project-writing row is `{"save", "management", …}`, and
  `agent.terminate` (`agent_handlers_management.cpp:60-65`) sets three fields on
  `AgentControl` and does not touch the session or the process. **No gate
  needed, and the registry does not change** (§7).

### 1.3 There are two dirty notions, and only one of them is authoritative

**`EditorSession::dirty()`** — `session.cpp:2592`, backed by `Impl::update_dirty`
(`session.cpp:1181-1184`):

```cpp
void update_dirty() {
    project_dirty = load.project != nullptr &&
        serialize_project(*load.project) != saved_serialized_project;
}
```

It is **content-keyed**: it compares the live document against the bytes last
written or read. The session recomputes it itself at every commit, undo, redo,
adoption and rebuild (`session.cpp:1303,1386,1481,1515,1552,1569,1674,2004,2744`),
and sets it directly at the four lifecycle points where content equality is
known by construction — `create` `true` (`:1767`), `close` `false` (`:1784`),
`open` `false` (`:1830`), `reload` `false` (`:1888`), `save` `false` (`:2068`).
It cannot go stale, because nothing outside the session can change the document.

**`ShellState::project_dirty`** — `shell_state.hpp:852`. A shell-side *cache* of
the above, refreshed by `update_project_dirty_state` (`shell_core.cpp:251-258`)
at roughly forty explicit call sites, plus two direct assignments
(`shell_core.cpp:294`, `:682`). It has exactly three readers, all of them
display: the toolbar `UNSAVED` chip (`shell_project_panels.cpp:656`), the Project
panel head (`:932`), and two status strings (`:1024`, `:1037`).

The decisive line is `shell_core.cpp:572`, inside `adopt_session_project_into_shell`:

```cpp
state->project_dirty = !project_is_clean;
```

That is a **provenance-keyed** assignment: it records *how the adoption was
requested*, not what the document now is. `reload_project` passes
`project_is_clean = true` unconditionally (`shell_core.cpp:660`); MAR-181's Open
passes `true` (`shell_file_paths.cpp:669`) and its New passes `false`
(`shell_file_paths.cpp:705`) precisely because the shared reset block would
otherwise paint a never-written project clean.

**Decision: the gate reads `state->session.dirty()`.** Three reasons, in order
of weight:

1. **It is content-keyed; the cache is not.** Standing lesson 3 for this arc —
   prefer content-keyed conditions over provenance-keyed ones — points at
   `shell_core.cpp:572` directly. A gate that consults the cache is a gate that
   consults a caller-supplied boolean.
2. **A cache can be one refresh behind; the truth cannot.** `project_dirty` is
   correct only where someone remembered to refresh it. Two of its refresh
   points are in `shell_main.cpp` alone (`:801` after `drain_commands`, and the
   `sync_shell_from_editor_session_if_revised` at the top of the frame) and are
   absent from `shell_smoke_frames.cpp`. A gate keyed on it would therefore mean
   something different in the smoke than in the shipped shell — the exact
   duplicate-frame-body hazard this arc keeps finding.
3. **The cost of a wrong answer is asymmetric.** A false negative silently
   destroys the user's work; a false positive shows one extra dialog. The
   signal that cannot go stale is the one to trust.

`project_dirty` keeps its job — the `UNSAVED` chip — and MAR-182 neither reads
nor writes it. `session.dirty()` alone is also correct when no project is loaded:
`update_dirty` returns `false` for a null project, and `close()` sets it `false`.

### 1.4 `ShellState::saved_project_snapshot` is still write-only

`shell_state.hpp:854` (declaration) and `shell_core.cpp:573`, `:579`, `:680`
(writes). **Zero reads.** It is a third, weaker copy of the same content
comparison the session already performs, and MAR-182 **adds no read**. Any
design that wants one has re-derived `session.dirty()` badly.

*(The commissioning brief said "four writes". Measured: three writes and one
declaration, four lines total — matching `AGENTS.md:309`.)*

### 1.5 The seam MAR-181 left, and what is actually usable in it

- **`begin_file_action(ShellState*, FileAction)`** (`shell_file_paths.cpp:585`)
  is a real single entry point for New/Open/Save/Save As. Every one of the five
  surfaces MAR-181 added calls it and nothing else: four menu items
  (`shell_project_panels.cpp:724,727,732,736`) and `Ctrl+S`
  (`shell_preview.cpp:222`). This is genuinely load-bearing and MAR-182 keeps it.
- **`draw_file_path_modals(ShellState*)`** is called from **two mutually
  exclusive branches of `draw_menu_bar`**, and exactly one of them runs per
  frame: `shell_project_panels.cpp:706`, the early return taken when
  `BeginMainMenuBar()` is false (the menu bar is clipped, and MAR-181's comment
  at `:704-705` explains that an open interaction must not vanish while it is),
  and `shell_project_panels.cpp:925`, after `EndMainMenuBar()`. It has **no
  other caller anywhere**. Both frame bodies reach it through their existing
  `draw_menu_bar` call, so **any modal drawn from here needs zero frame-body
  edits** — the single most important structural fact in this design (§4.4).
  The two-branch shape is a feature, not a hazard: it is what makes the prompt
  survive a clipped menu bar. Do not "simplify" it to one call.
- **`apply_pending_file_action(ShellState*) -> bool`** is the one end-of-frame
  consumer of deferred session replacement, present exactly once in each frame
  body (`shell_main.cpp:619`, `shell_smoke_frames.cpp:131`) and guarded by case
  C11 (`shell_smoke_project.cpp:2674`).
- **`FilePathRequest` lives on `ShellState`** (`shell_state.hpp:860`) rather than
  in a file-static, with the stated reason "MAR-182 must be able to observe a
  cancel" (`shell_file_paths.hpp:52-54`). It does deliver that — but not in the
  way the header claims (§12, defect B).

---

## 2. Goal

One state machine. Every intent that can replace or terminate the session enters
it; the machine is the only thing that decides whether the intent proceeds; and
the machine cannot be bypassed by adding a new surface later without noticing.

Non-negotiables, from the acceptance criteria:

1. New, Open, Reload, Quit and native OS close all enter it (AC1).
2. Save completes the intent **only after an atomic save succeeds** (AC2).
3. Cancel keeps the session **and** leaves the intent re-expressible (AC3).
4. A failed save preserves session and intent, reports the error, and **never
   falls through** to discard, replacement or shutdown (AC4).
5. The smokes cover every intent, every response, repeats, modal close, save-path
   cancellation, save failure, OS close, and successful continuation (AC5).

---

## 3. The state machine

### 3.1 Types

Added to `src/editor/shell_file_paths.hpp`, beside the MAR-181 types they extend:

```cpp
/** @brief Every intent that replaces or ends the session. */
enum class SessionIntent {
    New,
    Open,
    Reload,
    /// Both the File>Quit item and a native OS close request. They are the same
    /// intent from two origins; nothing downstream needs to tell them apart.
    Quit,
};

/** @brief Whether the prompt is up, or a save it asked for is still in flight. */
enum class DirtyIntentPhase {
    Prompting,
    AwaitingSave,
};

/** @brief The three answers. */
enum class DirtyIntentResponse {
    Save,
    Discard,
    Cancel,
};

/**
 * @brief A live dirty-session intent.
 *
 * On `ShellState` for the same two reasons `FilePathRequest` is: a menu item
 * cannot open a root-scope modal (ImGui::OpenPopup hashes against the menu
 * popup's id stack), so the item must RECORD and a root-scope drawer must open
 * on a later frame -- `opened` is that latch; and the machine's state has to be
 * inspectable by a UI-free test, because six of this story's eight cases never
 * render a frame.
 */
struct DirtyIntentRequest {
    SessionIntent intent{SessionIntent::New};
    DirtyIntentPhase phase{DirtyIntentPhase::Prompting};
    bool opened{false};
};

/// The FULL ImGui window name, `##` suffix included -- the smoke finds the
/// modal by this exact string, exactly as it finds MAR-181's two.
constexpr char kDirtyIntentModal[] = "Unsaved Changes##dirty_intent";
```

`ShellState` gains two fields (`shell_state.hpp`, beside the MAR-181 block at
`:860-862`):

```cpp
std::optional<DirtyIntentRequest> dirty_intent;
/// Set only by perform_session_intent(Quit). The main loop's ONLY exit
/// condition, so no path can terminate without passing the machine.
bool should_exit{false};
```

### 3.2 The one entry point

```cpp
/**
 * @brief THE gate. Every session-replacing or terminating surface calls this.
 * @post Either the intent has been performed, or `dirty_intent` holds it.
 */
void begin_session_intent(ShellState* state, SessionIntent intent);
```

```
begin_session_intent(state, intent):
    if state->dirty_intent:
        if phase == AwaitingSave: return            # a save is in flight; drop it
        state->dirty_intent->intent = intent        # last wish wins; stay Prompting
        return
    if !state->session.dirty():
        perform_session_intent(state, intent)       # nothing to lose
        return
    state->dirty_intent = DirtyIntentRequest{intent, Prompting, /*opened=*/false}
```

`perform_session_intent` is **file-internal** to `shell_file_paths.cpp`:

```
perform_session_intent(state, intent):
    New    -> begin_file_action(state, FileAction::New)      # raises the New form
    Open   -> begin_file_action(state, FileAction::Open)     # raises the chooser
    Reload -> begin_file_action(state, FileAction::Reload)   # arms the deferral
    Quit   -> state->should_exit = true
```

**Why the gate is here and not inside `begin_file_action`.** MAR-181's header
(`shell_file_paths.hpp:158-166`) promised the gate would go "at the TOP of
`begin_file_action`". Following that would force `perform_session_intent` to
re-enter the same function with the gate suppressed — a bypass flag or a bypass
parameter, i.e. a condition keyed on *who is calling* rather than on *what the
document is*. That is precisely the provenance-keyed shape lesson 3 warns
against, and it would be a mutable latch on `ShellState` that any future caller
could leave set. Splitting the layers instead — `begin_session_intent` decides,
`begin_file_action` performs — needs no flag, and the performer stays exactly the
function MAR-181 shipped. §12 records the header comment as a defect to correct.

**Why Save and Save As are not in `SessionIntent`.** They cannot discard
anything. Save *is* the resolution; gating it would deadlock the machine. The
File menu therefore calls two functions, and that is correct rather than a
failure to unify: the unification claim is over *discarding* paths, and the set
of those is exactly the four `SessionIntent` values.

### 3.3 Resolving

```cpp
/**
 * @brief Answers a live prompt.
 *
 * Returns void deliberately. `Save` has THREE outcomes -- completed, waiting on
 * a destination, failed -- and no bool carries three. MAR-181 shipped a bool it
 * described as a completion signal that could not in fact distinguish a cancel
 * from an idle frame (see the design's section 12, defect B); repeating that
 * mistake with a second bool would be worse. The state IS the signal, and every
 * field of it is on ShellState where a UI-free test can read it.
 */
void resolve_dirty_intent(ShellState* state, DirtyIntentResponse response);
```

**Cancel.**

```
state->dirty_intent.reset()
```

Nothing else. The session, the selection, the preview, the history and
`project_dirty` are untouched — Cancel is the response that does nothing by
definition, and the test asserts that as a bit-identical `SessionSnapshot`. The
intent is *re-expressible*, not *retained*: AC3's "available for a deterministic
retry" is satisfied by the surfaces still being there and `begin_session_intent`
being idempotent, not by a hidden sticky intent that would ambush the next click.

**Discard.**

```
intent = state->dirty_intent->intent
state->dirty_intent.reset()
perform_session_intent(state, intent)
```

Note what Discard does **not** do: it does not clear, revert, or overwrite
anything. It declines to save, and lets the intent proceed. For New, Open and
Reload the unsaved work survives right up until the replacement lands, and MAR-180
made every replacement atomic — so a user who Discards and then cancels the
chooser still has their edits. Only for **Quit** is Discard immediately lossy.
This is a property worth stating because it is the opposite of what the word
suggests, and it is why a failed Open after a Discard (MAR-181 C7) loses nothing.

**Save.**

```
state->dirty_intent->phase = AwaitingSave
begin_file_action(state, FileAction::Save)
tick_dirty_intent(state)                       # the same evaluation the frame uses
```

and the evaluation, which is the heart of the story:

```
tick_dirty_intent(state):
    if !state->dirty_intent or phase != AwaitingSave: return
    if !state->session.dirty():                       # (1) the save landed
        intent = state->dirty_intent->intent
        state->dirty_intent.reset()
        perform_session_intent(state, intent)
        return
    if state->file_path_request:                      # (2) a destination is being chosen
        return
    state->dirty_intent->phase = Prompting            # (3) failed or cancelled
    state->dirty_intent->opened = false               # re-raise the prompt
```

Three properties of this block are load-bearing:

- **Condition (1) is content-keyed.** `!session.dirty()` means "the bytes on disk
  now match memory", which is exactly the precondition for letting the intent
  proceed. The provenance-keyed alternative — "`save_project_file` returned
  `true`" — is both unavailable through the deferred Save As branch and subtly
  wrong on the immediate one, because `save_project_file` also returns `false`
  without saving when `authoring_gesture_active` (`shell_core.cpp:668-670`).
- **Branch (3) is the whole of AC4.** A failed save leaves `session.dirty()`
  true and clears no request, so control lands here: the intent survives, the
  phase returns to `Prompting`, `state->error_message` still carries the
  session's formatted error, and `perform_session_intent` was never reached.
  There is no code path from a failed save to a discard, a replacement or a
  shutdown, because the only call to `perform_session_intent` in this function
  sits under condition (1).
- **Branch (2) depends on `file_path_request` being truthful.** Today it is not,
  in one case; §4.3 fixes that, and the fix is a precondition for this branch,
  not a nicety.

### 3.4 The full transition table

| From | Event | To | Side effects |
|---|---|---|---|
| *(none)* | `begin_session_intent`, session clean | *(none)* | intent performed immediately |
| *(none)* | `begin_session_intent`, session dirty | `Prompting` | none |
| `Prompting` | `begin_session_intent(X)` | `Prompting` | intent := X (last wish wins) |
| `Prompting` | `Cancel` | *(none)* | none whatsoever |
| `Prompting` | `Discard` | *(none)* | intent performed |
| `Prompting` | `Save`, save succeeds | *(none)* | file written atomically; intent performed |
| `Prompting` | `Save`, save fails | `Prompting` | `error_message` set; session dirty; intent kept |
| `Prompting` | `Save`, no destination yet | `AwaitingSave` | chooser raised (Save As) |
| `Prompting` | prompt closed by Escape / external | *(none)* | treated as `Cancel` (§4.3) |
| `AwaitingSave` | chooser committed, save succeeds | *(none)* | intent performed |
| `AwaitingSave` | chooser committed, save fails | `AwaitingSave` | chooser stays open (MAR-181 `commit_path_choice:183-185`); next tick sees it and waits |
| `AwaitingSave` | chooser cancelled or closed | `Prompting` | prompt re-raised (`opened = false`) |
| `AwaitingSave` | `begin_session_intent(X)` | `AwaitingSave` | **ignored**; X is re-expressible |

---

## 4. Where it lives

### 4.1 Reload joins the deferred rail

Reload is the only intent whose performance is neither a modal nor a flag: it
replaces the session, so like New and Open it must land at **end of frame**, not
mid-frame, or windows drawn before and after `draw_menu_bar` would describe two
different projects.

Today it lands there through a `bool* reload_requested` out-parameter threaded
through three drawing functions and consumed by a hand-written `if` in **each of
the two duplicate frame bodies** (`shell_main.cpp:611-613`,
`shell_smoke_frames.cpp:125-128`). `perform_session_intent` cannot reach a
frame-local `bool`.

MAR-182 therefore **retires `reload_requested` and moves Reload onto the existing
deferred rail**:

- `FileAction` gains a fifth value, `Reload`. It is a File-menu action
  (`Reload Project`), it replaces the session, and it is deferred — the same
  three properties that put `New` and `Open` in this enum. It never participates
  in `FilePathMode`, `FilePathRequest` or `commit_path_choice`: the only writer
  of `FilePathRequest::action` is `seed_action_request`
  (`shell_file_paths.cpp:112-128`), called only for `Open` and `SaveAs`.
- `begin_file_action`'s switch gains a `Reload` case that clears the two modal
  states and arms `pending_file_application` with `action = Reload` and an
  **empty** `path` — `reload_project` reads `state->project_path` itself
  (`shell_core.cpp:637-642`), and duplicating it would create a second source of
  truth for the same fact.
- `apply_pending_file_action` gains, before the Open branch,
  `if (pending.action == FileAction::Reload) return reload_project(state);`.
- `draw_menu_bar`, `draw_shell_toolbar` and `draw_project_window` **lose their
  `bool*` first parameter**, and the two frame-body `if (reload_requested)`
  blocks are deleted.

This is a net **removal** of duplicated frame-body logic, and it inherits case
C11's protection for free: the rail it joins is already proved to be wired in the
smoke's own frame body. Two consequences to state rather than discover:

- Today a Reload and a New/Open could both fire in one frame and both run.
  Afterwards `pending_file_application` is a single optional, so the second
  arming replaces the first. Interactively unreachable (modals block the menu),
  and strictly safer.
- `shell_smoke_frames.cpp:125-128` currently aborts the smoke when a reload
  fails; afterwards `apply_pending_file_action`'s `false` is discarded. No smoke
  frame triggers a reload, so the observable behaviour is identical — but it is
  a deliberate change, not an oversight.

### 4.2 Quit and native close: the decision moves into `ShellState`

A prompt that cannot block the exit is worse than no prompt, so the veto has to
be real. Three changes:

**(a) The menu item stops returning an action.**
`shell_project_panels.cpp:743-745` becomes
`begin_session_intent(state, SessionIntent::Quit);`. `ProjectMenuAction` then has
one value and no readers, so it is deleted along with `draw_menu_bar`'s return
type (`shell_project_panels.hpp:7-10,32-34`).

The reason this matters is not tidiness. Quit's handling lives today at
`shell_main.cpp:559-562` — inside the frame body that has a hand-maintained twin
which **discards the return value entirely** (`shell_smoke_frames.cpp:65`). Any
gate placed there is invisible to every test. Gating at the menu item puts it
where both bodies already run the same code.

**(b) The window host gains a veto and loses its bypass.**
`EditorWindowHost` (`window_host.hpp:42-43`):

```cpp
virtual bool should_close() const noexcept = 0;
virtual void cancel_close_request() noexcept = 0;   // NEW: clears the latch
// virtual void request_close() noexcept = 0;       // DELETED
```

`SdlWindowHost::cancel_close_request()` is `close_requested_ = false;`, beside
the existing accessors at `sdl_window_host.cpp:108-109`. `request_close()` is
**deleted**: after (a) its only caller is gone, and leaving it would preserve a
second way to terminate that bypasses the machine — a loaded gun in exactly the
place this story exists to disarm. The deletion is compiler-enforced; there is
only one implementor (`sdl_window_host.cpp:34`).

**(c) A UI-free absorber the smoke can drive.**

```cpp
/**
 * @brief Folds a host close-request into the intent machine.
 * @return Whether the host's latch must be CLEARED, i.e. the exit is vetoed.
 *
 * Split out of shell_main.cpp's loop on purpose. The loop itself is unreachable
 * from any headless test -- run_headless_smoke returns before a window host is
 * ever created -- so the decision is put in a pure function that a smoke can
 * call with true and false and assert both answers. What remains untested in
 * shell_main.cpp is then two lines of glue, and that is stated in section 11
 * rather than papered over.
 */
bool absorb_close_request(ShellState* state, bool host_close_requested);
```

```
absorb_close_request(state, requested):
    if !requested:            return false
    if state->should_exit:    return false          # a confirmed exit passes through
    begin_session_intent(state, SessionIntent::Quit)
    return !state->should_exit                      # vetoed iff the gate held it
```

`shell_main.cpp`'s loop becomes:

```cpp
while (!shell_state.should_exit) {
    window_host->poll_events(/* … unchanged … */);
    if (absorb_close_request(&shell_state, window_host->should_close())) {
        window_host->cancel_close_request();
    }
    /* … unchanged … */
}
```

`should_exit` becomes the loop's only termination condition, so nothing can end
the process without passing the machine. The three existing `break`s
(`frame_outcome.error`, `surface_starved`, `--auto-close`) are untouched and
remain unconditional — in particular `--auto-close` still ends every smoke on
frame count regardless of dirtiness, which is required or a dirty display smoke
would hang forever.

### 4.3 The prompt, and the stale-request hole it depends on

The prompt is an in-ImGui modal — there is no file dialog capability in the tree
and MAR-181 measured that (`AGENTS.md:307`). It is drawn at root scope from
`draw_file_path_modals`:

```cpp
void draw_file_path_modals(ShellState* state) {
    if (state == nullptr) return;
    if (draw_dirty_intent_modal(state)) return;   // true == the prompt owns the frame
    if (state->new_project_form.has_value()) { draw_new_project_modal(state); return; }
    draw_path_chooser_modal(state);
}
```

`draw_dirty_intent_modal` runs `tick_dirty_intent` first (so an `AwaitingSave`
resolved by last frame's chooser is seen before anything is drawn), then returns
`true` only while `phase == Prompting` — in `AwaitingSave` the chooser must still
be drawn, and it is. Body: the project path, one line naming what the intent will
do, a separator, and `Save` / `Discard` / `Cancel` buttons whose plain labels the
smoke sweeps scoped to `kDirtyIntentModal`, exactly as MAR-181 C9 sweeps `Cancel`
scoped to `kFilePathModal`.

**The stale-request hole.** `draw_path_chooser_modal` sets `opened = true`, calls
`OpenPopup`, and returns early whenever `BeginPopupModal` is false
(`shell_file_paths.cpp:194-205`). If ImGui closes that popup by any route other
than the two buttons — Escape being the obvious one — `file_path_request` stays
set forever with `opened` already true, so the popup never reopens. Today this is
merely untidy: the next `begin_file_action` reseeds the whole request
(`seed_action_request` assigns a fresh value), so it self-heals.

Under MAR-182 it is a **hang**. `tick_dirty_intent` branch (2) reads
`file_path_request.has_value()` as "a destination is being chosen"; a stale
request means an `AwaitingSave` intent that never resolves and a prompt that
never returns. So MAR-182 makes the optional truthful:

```cpp
if (state->file_path_request->opened &&
    !ImGui::BeginPopupModal(kFilePathModal, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    // Closed by something other than Choose or Cancel. An external close of a
    // chooser IS a cancel, and MAR-181 already documents Cancel and success as
    // clearing this identically.
    state->file_path_request.reset();
    return;
}
```

The same rule gives the prompt its AC5 "modal close" behaviour: a closed prompt
is a Cancel. The New form has the identical shape and is **not** fixed here
(§10) — it self-heals as today and nothing in this machine reads it.

### 4.4 Why this needs no frame-body edit

`draw_file_path_modals` is called only from `draw_menu_bar`, from two mutually
exclusive branches (`shell_project_panels.cpp:706` and `:925`, §1.5), exactly one
of which runs per frame; both frame bodies reach it through their existing
`draw_menu_bar` call. The
prompt, the tick, and `resolve_dirty_intent` are all downstream of that one line.
The deferred rail is `apply_pending_file_action`, already present once in each
body and guarded by C11.

The only frame-body edits MAR-182 makes are **deletions**: the `bool
reload_requested` local and its `if` block, in each body. A deletion cannot
create a "works interactively, invisible to the smoke" divergence — the failure
mode C11 exists to catch is an *addition* present in one body and absent from the
other. The `shell_main.cpp` loop changes (§4.2) are additions, and they are
addressed by extracting the decision into `absorb_close_request` (tested) and
naming the two-line remainder as untested glue (§11).

---

### 4.5 The chooser accepts an existing file, and the prompt's Save depends on it

MAR-181's own design contradicts itself here, and the wrong half is the one that
reads like a rule: its §3.2 says "`Choose` is disabled whenever the diagnostic is
non-empty", while its §3.4 rule 5 requires an existing file to be **accepted**
*with* a diagnostic. **§3.4 is what shipped.** `FilePathChoice` carries a
separate `acceptable` flag (`shell_file_paths.hpp:101-111`), `Choose` is gated on
it and nothing else (`shell_file_paths.cpp:259-260`), and a Save-target over an
existing file sets `acceptable = true` alongside
`diagnostic = "Replaces the existing file."` (`shell_file_paths.cpp:508-514`),
which case C8's eleventh row asserts simultaneously
(`shell_smoke_project.cpp:1513-1515`).

This is not a distant concern for MAR-182. `resolve_dirty_intent(Save)` on a
session with no destination recurses into Save As (§3.3), so the prompt's Save
button reaches this exact chooser — and the overwhelmingly common case there is
**writing over a file that already exists**. Under §3.2's rule `Choose` would be
disabled precisely then, and the `AwaitingSave` phase would have no exit: the
prompt would be unresolvable for the ordinary user. Anyone "fixing" the chooser
toward §3.2 while implementing this story breaks it.

The design therefore keeps `acceptable` as the only gate, changes nothing about
`resolve_choice`, and C15 asserts the combination end to end by committing a save
over an existing file and watching the intent complete (§9).

## 5. Failure taxonomy

| Failure | Where detected | What survives | What the user sees |
|---|---|---|---|
| Save fails (rename seam, EACCES, full disk) | `session.save` → `save_project_file` returns false, session stays dirty | Session, selection, preview, history, `project_path`, the destination file byte-for-byte (MAR-180 atomicity) | `error_message` from `ProjectSaveResult`; status `Project save failed`; prompt re-raised with the same intent |
| Save refused: an authoring gesture is live | `save_project_file` (`shell_core.cpp:668-670`) returns false before touching disk | Everything | Status `Finish the active edit before saving`; prompt re-raised |
| Save As destination cancelled | chooser's `Cancel` clears `file_path_request` | Everything | Prompt re-raised, same intent |
| Save As destination invalid | `resolve_choice` disables `Choose` | Everything | Diagnostic in the chooser; no state change |
| Save As write fails | `apply_save_as` returns false, chooser **stays open** (`shell_file_paths.cpp:183-185`) | Everything, including `project_path` (`shell_file_paths.cpp:568-571`) | Error in the chooser; intent still `AwaitingSave` |
| Discarded Open then fails to load | `apply_pending_file_action` (`shell_file_paths.cpp:654-660`) | Everything — `session.open` is atomic; MAR-181 C7 | `Project load failed` + error; the unsaved work is **still there** |
| Discarded Reload then refused (gesture live) | `reload_project` (`shell_core.cpp:627-629`) | Everything | Status message; nothing lost |
| Discarded New then fails to create | `apply_pending_file_action` (`shell_file_paths.cpp:687-693`) | Everything — `session.create` is atomic | `New project failed` + error |
| Prompt closed externally | §4.3 | Everything | Same as Cancel |

The single invariant behind the table: **`perform_session_intent` is called from
exactly two places** — `begin_session_intent`'s clean-session branch, and
`tick_dirty_intent`'s `!session.dirty()` branch. Neither is reachable from a
failure.

---

## 6. Properties

1. **One gate.** `begin_session_intent` is the only function that consults
   dirtiness, and the only caller of `perform_session_intent` outside
   `tick_dirty_intent`.
2. **Content-keyed throughout.** The gate reads `session.dirty()`; completion
   reads `!session.dirty()`. Neither reads a flag describing how something was
   produced.
3. **Save never falls through.** Proved structurally (§5) and by case C14.
4. **Cancel is a no-op.** Bit-identical `SessionSnapshot`, asserted by C17.
5. **Discard is not destructive for New/Open/Reload.** Nothing is lost until an
   atomic replacement lands.
6. **No path terminates without the machine.** `should_exit` is the loop's only
   condition, and `request_close()` is deleted.
7. **`saved_project_snapshot` stays write-only.** 1 declaration, 3 writes, 0
   reads, before and after.
8. **`project_dirty` is untouched.** MAR-182 neither reads nor writes it.

---

## 7. Registry

**Unchanged at 64**, split 39 edit / 12 inspection / 10 management / 3 validation
(re-measured at `43572f0` from `agent_dispatch.cpp:29-96`).

Proved **by diff**, not by assertion: MAR-182 adds no operation, and the four
trees that would have to change are untouched.

```
git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_handlers_*.cpp \
                   src/samples/agent_dispatch_smoke.cpp tools/     # MUST be empty
```

The substantive reason is §1.2: no agent operation replaces, reloads, closes or
terminates a session, so no agent path can discard unsaved work and none needs a
gate. `agent.terminate` (`agent_handlers_management.cpp:60-65`) ends an *agent*
session, not the editor.

Count sites, all of which must stay exactly where they are: **10** `!= 64U`
guards (`shell_smoke_constraints.cpp:147,676`;
`shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`;
`shell_smoke_timeline.cpp:3697`), **1** `std::array<OperationExpectation, 64>`
(`agent_dispatch_smoke.cpp:39`), **2** Python assertions
(`test_client.py:53,55`).

Never blind-substitute a count. `AGENTS.md` contains `t = 0.62` and "62 lines"
(self-referential, they shift on every insertion); `(51, 56, 64)` and
`rgb(54,57,64)` are colours; `test_client.py:1484` is an ordinal. Never touch
`IM_COL32(56, 61, 69, 255)`, `"x": 56.0`, `56,995,840`,
`PhysicsBoneState … 56 bytes/bone`, `IM_COL32(208,134,57,230)`.

---

## 8. Compatibility

No format, ABI or protocol change. MAR-182 touches no serializer and no schema:
`project.cpp`, `session.cpp`, `include/marrow/c/`, `src/c/` and
`include/marrow/runtime/` are not edited at all, so `.marrow`, `.mskl` v1,
`.mbin` v2 and C ABI v1 are byte-compatible and `docs/root1/format-spec.md`
needs no change. No new dependency and no new CMake source file — every change
lands in files already on the `marrow_editor_shell` source list.

The one behavioural break is intended and is the story: a New, Open, Reload,
Quit or window close over a dirty project now asks instead of destroying. Two
smaller, deliberate breaks: `draw_menu_bar` and friends lose a parameter (all
eight call sites are in this tree), and `EditorWindowHost::request_close()` is
deleted (one implementor, one caller, both updated here).

---

## 9. Validation strategy

Eight cases, C12–C19, continuing the shared C-series (MAR-180 C1–C3, MAR-181
C4–C11). All run inside `marrow_editor_shell`'s headless smoke — MAR-182 adds no
model-layer primitive, so `marrow_project_smoke` carries none of it.

The governing rule for this arc: **for every inversion, name the mechanism by
which it is observable at the layer under test.** MAR-178, 179, 180 and 181 each
specified inversions that could not bite; MAR-180 found three and MAR-181's
headline gate had to be replaced outright (its I9). The columns below are not
decoration.

| Case | What it asserts | Layer | Why an inversion bites here |
|---|---|---|---|
| **C12** | The gate itself. On a dirty session each of the four intents arms `dirty_intent` and performs **nothing** (no `pending_file_application`, no `new_project_form`, no `file_path_request`, `should_exit == false`, `SessionSnapshot` bit-identical). On a clean session each performs immediately | UI-free | The four intents land in **four different observable fields**, so a gate that fires for only some of them fails on the specific row. A single-field assertion could not tell "unified" from "New happens to be gated" |
| **C13** | Save completes the intent only after a successful atomic save: intent `Open` + `Save` → session clean, the file **RELOADS via `load_project`**, chooser now up with `action == Open`, `dirty_intent` cleared | UI-free | Standing lesson 2: `validate_project_for_save` takes no base document, so a passing `save()` proves nothing. Only the reload distinguishes a real save from a write that cannot be opened (MAR-181 I6 measured exactly this) |
| **C14** | Save **failure** never falls through. `ScopedRenameCallback` → `permission_denied`, intent `Quit`, `Save` → `should_exit == false`, intent still `Quit`/`Prompting`, session still dirty, `error_message` non-empty, destination byte-identical and still `load_project`-able. Release the seam, `Save` again → `should_exit == true` and the file reloads | UI-free | `should_exit` is a **single bool** that the fall-through defect sets. There is no way to write the fall-through bug and leave it false |
| **C15** | Save-path cancellation **and** save-path completion over an existing file. Empty `project_path` (the documented `begin_file_action` guard branch, `shell_file_paths.cpp:622-625`) → `Save` recurses into Save As → `phase == AwaitingSave` + `file_path_request` set. Half A: clear the request, tick → back to `Prompting`, intent unchanged, session still dirty, nothing reloaded; then `Cancel` → intent gone, session untouched. Half B: re-enter `AwaitingSave`, assert `resolve_choice` **accepts** a destination that already exists (`acceptable == true` *with* `diagnostic == "Replaces the existing file."`), commit it, tick → intent completes and the overwritten file **RELOADS** | UI-free | `AwaitingSave` is a **distinct enum value** no "Save always resolves" implementation ever produces — asserting the phase, not just the outcome, is what makes the middle state observable. Half B bites §4.5: under MAR-181 §3.2's rule `Choose` is disabled exactly when the destination exists, so `AwaitingSave` would have no exit in the common case, and the assertion pair (`acceptable` **and** a non-empty diagnostic) is the only thing that catches it |
| **C16** | Discard performs without persisting. Record the on-disk bytes, intent `Reload`, `Discard` → `pending_file_application.action == Reload`, session **still dirty**, file **byte-identical**; then `apply_pending_file_action` → reload succeeds, session clean, the unsaved edit is gone from the document | UI-free | Two independent comparisons: the byte compare catches a Discard that secretly saves; the post-reload document compare catches a Discard that secretly keeps the edit. One assertion would catch only one defect |
| **C17** | Cancel and repeats. Two `begin_session_intent(New)` → one intent, still `New`. `begin_session_intent(Quit)` while `Prompting` → intent becomes `Quit`. `Cancel` → `dirty_intent` empty, `SessionSnapshot` bit-identical, `should_exit` false, nothing armed. `begin_session_intent(New)` again → arms cleanly. While `AwaitingSave`, a `begin_session_intent(Quit)` is **ignored** | UI-free | Each clause reads a **different field** (`intent`, `phase`, `should_exit`, the snapshot), so replace-vs-ignore-vs-stack are three distinguishable outcomes rather than one |
| **C18** | OS close through `absorb_close_request`. Clean: `(state, true)` → returns **false**, `should_exit == true`. Dirty: → returns **true**, `should_exit == false`, intent `Quit`; a second call still vetoes and still holds one intent; `Discard` → `should_exit == true`; a call after that → **false** (confirmed exit passes through). `(state, false)` → false, no intent | UI-free | **The return value IS the veto.** A design that forgot to veto returns `false` on a dirty project, which is one `if` in the test. And the post-confirmation pass-through catches the deadlock where a confirmed exit is vetoed forever |
| **C19** | The prompt by a **real mouse**, MAR-181-C9-shaped. Dirty the project → click `File` → click `Reload Project` → `kDirtyIntentModal` Active at root scope and `dirty_intent` holds `Reload`; sweep for `Save`, `Discard`, `Cancel`; click `Cancel` → modal closes, intent cleared, `SessionSnapshot` bit-identical. Then re-raise and close it **externally** → intent cleared (AC5 "modal close"). Then open the chooser and close it externally → `file_path_request` cleared (§4.3) | Real ImGui frames + synthesized mouse | **C12–C18 all drive the seams directly and would pass with every menu item unwired** — MAR-181's I8 measured exactly that. C19 is the only case that observes the wiring, and the only one that observes the modal exists at root scope at all |

Ten planned inversions, each with its predicted failing case and the mechanism:

| # | Inversion | Predicted | Mechanism |
|---|---|---|---|
| I1 | `begin_session_intent` performs regardless of `session.dirty()` | C12 only | C12's dirty half asserts each intent's *own* field stayed untouched |
| I2 | Gate on `state->project_dirty` instead of `session.dirty()` | C12 and C16 | C16 dirties via a transaction and never calls `update_project_dirty_state`, so the cache is stale and the gate opens. This is the §1.3 argument made executable |
| I3 | `Save` performs the intent before checking dirtiness | C13 and C14 | C14's failure half is the sharp one: the intent proceeds and `should_exit` becomes true with the file unwritten |
| I4 | `Save` keys completion on `save_project_file`'s return instead of `!session.dirty()` | C15 | The Save As branch has no return value to read, so `AwaitingSave` resolves immediately and the phase assertion fails |
| I5 | `Discard` calls `save_project_file` first | C16 only | The on-disk byte comparison |
| I6 | `Cancel` clears the session or resets the preview | C17 only | The bit-identical `SessionSnapshot` |
| I7 | `begin_session_intent` stacks instead of replacing | C17 only | The `intent == Quit` assertion after the New→Quit sequence |
| I8 | `absorb_close_request` returns `false` on a dirty project | C18 only | The return value is asserted directly, in both polarities |
| I9 | Leave the menu's Quit item returning `ProjectMenuAction::QuitRequested` | C19 (Quit half) — and **must be checked** | This is MAR-181's I9 risk repeating. If C19 does not bite, the gate is wrong, not the code: strengthen the case, never weaken it |
| I10 | Skip §4.3's stale-request fix | C15 and C19 | C15's `AwaitingSave → Prompting` transition never fires; C19's chooser half sees a set optional after an external close |

Two harness facts C19 must **measure rather than assume**, both flagged for the
implementer because this design was written without a build:

- **Whether Escape closes an ImGui modal opened with `p_open == nullptr` under
  this harness.** If it does, C19 sends Escape. If it does not, C19 closes the
  popup through `imgui_internal.h` (already included at
  `shell_smoke_project.cpp:18`) and asserts the same postcondition. The
  production fix in §4.3 is correct either way — it makes the optional truthful
  regardless of what closed the popup — but the *test* must drive a close that
  actually happens.
- **The `AlwaysAutoResize` first-frame stub size.** MAR-181 measured the
  chooser's rect one frame after opening as `(452,259)-(468,296)`, 16×37 px. The
  sweep must render settle frames and re-read the rect, or it scans a sliver and
  reports every button absent. See **Headless Frame Smoke Notes** in `AGENTS.md`
  before writing this case.

---

## 10. Non-goals

- **MAR-183's recent projects.** No preference read or write, no
  `MARROW_CONFIG_HOME` use, no `EditorPreferences` field. MAR-183's AC3 says
  opening a recent entry "uses the MAR-182 dirty-intent flow" — that is MAR-183
  calling `begin_session_intent(SessionIntent::Open)`, and this machine is
  already shaped for it.
- **No read of `saved_project_snapshot`.** It stays write-only (§1.4, §6.7).
- **No "Revert to saved", no autosave, no crash recovery, no backup file.** None
  is in any acceptance criterion.
- **No `EditorSession::close()` adoption.** MAR-180 built it; it still has no
  caller outside `session.cpp`, and MAR-182 gives it none. Quit exits the
  process; a session teardown before `SDL_Quit` buys nothing and adds a failure
  mode.
- **No native OS dialog.** Same measured reason as MAR-181: there is no dialog
  capability in the tree.
- **No multi-document or per-window sessions.** One session, one prompt.
- **No fix for the New form's identical stale-`opened` hole.** It self-heals on
  the next `begin_file_action(New)`, nothing in this machine reads it, and
  fixing it is unrelated scope. Recorded in §11.
- **No `project_dirty` change.** It remains the `UNSAVED` chip's cache (§1.3).
- **No gesture check in the machine.** `authoring_gesture_active` already
  disables the menu items and already refuses inside `save_project_file` and
  `reload_project`. Adding a second gate is the opposite of unifying.

---

## 11. Risks and known limitations

- **R1 — `shell_main.cpp`'s two lines of loop glue are untested, and this is
  the known blind half of a known blind spot.** The `absorb_close_request` call
  and the `if (…) cancel_close_request()` live in the real main loop, which no
  headless test reaches (`run_headless_smoke` returns before a window host
  exists). The *decision* is fully covered by C18; that the decision is *called*
  is not. State plainly which half each detector catches:

  | Frame-body / loop line | Detector | Which half it catches |
  |---|---|---|
  | `apply_pending_file_action` in the **smoke's** body | MAR-181 C11 | smoke half only |
  | `apply_pending_file_action` in **`shell_main.cpp`** | **none** | *(pre-existing gap, not created and not closed here)* |
  | `absorb_close_request` / `cancel_close_request` in the loop | **none** | manual check only; C18 covers the decision, not the call |
  | the prompt itself | C19 | both bodies, via `draw_menu_bar`'s single reachable path |

  The second row is a gap MAR-181's review confirmed: C11 catches deleting the
  *smoke's* call and is blind to deleting the *interactive* one — the exact
  inverse of the inversion it was built for. It is unfixable without unifying the
  two hand-maintained frame bodies, which is out of scope here; MAR-182 neither
  widens it nor closes it, and adds no new line to either body (§4.4).
  Mitigations for row three: (a) `should_exit` is the loop's only exit condition,
  so omitting the call makes the editor unclosable — a failure no one can miss;
  (b) `request_close()` is deleted, so the old bypass cannot be reached by
  accident; (c) a manual verification line is added to `AGENTS.md`, in the style
  of the existing macOS launch-focus note.
- **R2 — I9 may not bite.** MAR-181's I9 predicted two failures and produced
  zero. C19 is the only case that observes menu wiring, and it must be run under
  I9 before the story is called done. If it passes, the rule is the plan's: add a
  case that observes the wiring, never relax the assertion.
- **R3 — the `AwaitingSave` window is real.** Between `resolve_dirty_intent(Save)`
  raising the chooser and the user committing it, the app is running with no
  prompt and a pending intent. If the user reaches the OS close button in that
  window, `begin_session_intent` is ignored per §3.3 and the latch is cleared, so
  the close does nothing until the chooser resolves. That is deliberate — a save
  in flight must land — but it will read as a dropped click to someone. Stated
  rather than hidden.
- **R4 — `--auto-close` bypasses the prompt by design.** Smokes end on frame
  count, not on `should_exit`. A dirty smoke exits without asking. Any other
  choice hangs CI.
- **R5 — the New form's stale-`opened` hole survives** (§10). Untouched, still
  self-healing, now documented.
- **R6 — this design was written without building or running anything.** Section
  12 lists what was measured. Everything the plan's Task 0 re-measures is marked
  there; the two ImGui behaviours in §9 are explicitly measurements the
  implementer must make, not facts this document asserts.

---

## 12. Corrections to this story's governing documents

Every story in this arc has found errors in the documents that commissioned it
(175: three, 176: seven, 177: six, 178: six, 179: six, 180: seven, 181: four --
plus a fifth, its §3.2/§3.4 contradiction, which its own review found later).
**Five here.**

- **A — `shell_file_paths.hpp:158-166` puts the gate in the wrong function.** It
  states "MAR-182 inserts the dirty-session intent gate at the TOP of this
  function", meaning `begin_file_action`. Doing so forces the intent's own
  resolution to re-enter with the gate suppressed, i.e. a bypass flag — a
  provenance-keyed condition of exactly the kind this arc has twice been bitten
  by. The gate goes in `begin_session_intent` and `begin_file_action` stays the
  gate-free performer (§3.2). **The comment must be rewritten, not left to
  contradict the code.**
- **B — `shell_file_paths.hpp:170-177` states a discrimination rule that does not
  work.** It says `apply_pending_file_action`'s bool is "MAR-182's completion
  signal" and that a cancelled action is
  `!state->file_path_request.has_value() && !applied`. That predicate is also
  true on **every idle frame**, and on every frame after a *successful* action
  (which clears the request too — the comment says so one line earlier). It
  cannot distinguish cancel from idle. MAR-182 does not use it; the intent's own
  `phase` is the signal, and **`apply_pending_file_action`'s signature and
  semantics do not change** (its `bool` keeps meaning exactly "a pending action
  ran to success this frame"). The comment must be corrected.
- **C — the brief's claim that MAR-181's I2 shows the two dirty notions
  "legitimately diverging" is a misreading.** The string
  `session.dirty()=true project_dirty=false` at `AGENTS.md:360` is the *inversion's*
  failure message — what C4 prints when the code is deliberately broken by
  passing `project_is_clean = true` for New. In the as-built tree the two agree
  on every shipped path. They are not two competing notions of dirtiness; one is
  the truth and one is a display cache of it (§1.3). The design's choice is
  unchanged, but the reason is different and stronger than "they diverge".
- **D — `saved_project_snapshot` has three writes, not four.** `shell_state.hpp:854`
  (declaration) plus `shell_core.cpp:573`, `:579`, `:680`. Four lines total, zero
  reads — matching `AGENTS.md:309` and not the brief.

- **E — MAR-181's design §3.2 contradicts its own §3.4, and §3.2 is the wrong
  half.** "`Choose` is disabled whenever the diagnostic is non-empty" (§3.2,
  line 256) versus "a Save target over an existing file is accepted *and*
  carries a diagnostic" (§3.4 rule 5). The shipped code and case C8 implement
  §3.4. MAR-182 depends on §3.4 being the live rule — its Save-from-the-prompt
  path goes through the same chooser, over an existing file, in the common case
  (§4.5). Design against §3.4; §3.2's sentence should be struck.

And one correction to the story text itself: **"unify" implies inconsistent
checks exist. Zero exist** (§1.1). The work is construction. The plan is sized
for that, and the acceptance criteria are satisfied by it either way.
