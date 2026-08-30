# MAR-183 Persist and Manage Recent Projects — Design

Story: `MAR-183`, "Persist and manage recent projects".
Depends on: `MAR-182` (`9ad69c6`), which depends on `MAR-181` (`43572f0` + `d743569`), which depends on `MAR-180` (`1a1ed33`).
Branch: `feat/mar-168`.

---

## 0. Where this story sits

The project-I/O arc is four stories, and this is the last of them:

| Story | What it owns |
|---|---|
| MAR-180 | Atomic `save`, Save As rebasing, `EditorSession::create`/`close`, failure-safe source adoption |
| MAR-181 | New / Open / Save / Save As as in-ImGui path modals on top of those primitives |
| MAR-182 | One Save / Discard / Cancel state machine in front of every path that can lose unsaved work |
| **MAR-183** | **A bounded, canonical, de-duplicated recent-project list in the versioned preference store, and the surface that drives it** |

MAR-184 (stepped inherit timeline overlays) begins a different arc entirely and
shares nothing with this one but its `dependsOn` edge.

Two earlier stories deliberately left holes shaped like this one, and both said
so in code:

- `include/marrow/editor/preferences.hpp:24-30` — *"Recent paths remain in their
  authored order and spelling. Canonicalization, de-duplication, existence
  checks, MRU promotion, and list bounds belong to the Recent Projects feature
  rather than this storage layer."*
- `src/editor/shell_file_paths.cpp:27-33` — *"There is no persisted 'last used
  directory': that is preference state, and preference state for the File
  workflow is MAR-183's territory."*

This document fills the first hole. It **does not** fill the second: see §10.

---

## 1. The measured gap

Everything in this section was read out of the tree at `9ad69c6` — the MAR-182
commit, and the anchor for **every `file:line` below**. Line numbers move as soon
as a task lands; the plan's Task 0 says how to tell an expected shift from a
substantive disagreement.

### 1.1 The storage layer already exists, and it is finished

This is the headline correction. Three of this arc's stories found their brief's
premise wrong; MAR-183's brief warned "grep for any existing recent-projects
field before assuming you must add one", and the grep is decisive.

| Layer | State today | Evidence |
|---|---|---|
| Field on the preference struct | **Present** | `EditorPreferences::recent_projects` — `std::vector<std::filesystem::path>` — `include/marrow/editor/preferences.hpp:33` |
| Wire parse | **Present, with field-local fallbacks** | `src/editor/preferences.cpp:269-295` |
| Wire serialize | **Present, skipping empty paths** | `src/editor/preferences.cpp:315-323` |
| Atomic write | **Present**, shared with the project writer | `src/editor/atomic_file_write.cpp` via `write_file_atomically` |
| Additive-field preservation | **Present** | `preserved_root` at `include/marrow/editor/preferences.hpp:36`, overlaid at `preferences.cpp:313-321` |
| Unit coverage of the storage | **Present, thorough** | `src/tests/preference_store_tests.cpp:264,327,451,864` — round-trip of order/spelling/duplicates, wrong-typed field, missing field, non-string entries, empty-string entries, version statuses, additive preservation |

The parse is already exactly as forgiving as the `CurvePreset` reader beside it:

```
preferences.cpp:269  recent_projects missing        -> empty list  + LoadedWithDefaults
preferences.cpp:273  recent_projects not an array   -> empty list  + LoadedWithDefaults
preferences.cpp:281  entry not a string, or empty   -> SKIP that entry, keep the rest
```

`src/tests/preference_store_tests.cpp:383-394` already asserts
`["first.marrow", 7, "", null, "second.marrow"]` loads as exactly
`["first.marrow", "second.marrow"]`.

**Consequence: MAR-183 adds no storage. It adds a policy layer above the storage
and a surface above that.** The plan is sized accordingly — this is a narrower
story than MAR-181 or MAR-182.

### 1.2 There is no recent-projects *feature*. Not one line.

```
$ grep -rniE "recent|mru|last_?opened|project_history" src/ tools/ python/
```

returns hits in **exactly two files**: `preferences.cpp` (the parse/serialize
pair above) and `preference_store_tests.cpp` (their tests). Zero hits in
`src/editor/shell_*`, zero in `include/`, zero in `tools/`. Concretely, all of
the following are **absent**:

| Absent thing | Consequence today |
|---|---|
| Any writer of `recent_projects` outside the tests | The list on disk is **always empty**; the key is emitted by `preferences.cpp:323` and never populated |
| Any reader of `recent_projects` outside the tests | Nothing in the shell has ever looked at it |
| Any canonicalization, dedup, cap, or ordering rule | The storage's own comment disclaims all four |
| Any menu, panel, or shortcut surface | The File menu has exactly six items (`shell_project_panels.cpp:724-750`) |
| Any `--project` history, "last opened", or "open on startup" | `shell_smoke.cpp:75` and `shell_main.cpp` take the path from the command line only |

`set_shell_default_curve` (`shell_preferences.cpp:92-108`) is the only production
writer of the settings file, and its comment at `:95-96` says it mutates the
loaded value *specifically* so `recent_projects` survives — a preservation
guarantee for a field nothing has ever written.

### 1.3 `kEditorSettingsVersion` does not move

`include/marrow/editor/preferences.hpp:12` — `inline constexpr int
kEditorSettingsVersion = 1;`

MAR-156 shipped `recent_projects` **inside version 1**. Both the reader
(`preferences.cpp:269`) and the writer (`preferences.cpp:323`) already handle it.
MAR-183 changes neither the key, its type, nor its element type.

**Decision: the version stays 1, and there is no migration.** The three
compatibility cases and what each does today, unchanged by this story:

| On-disk state | Behaviour | Where |
|---|---|---|
| A file written by MAR-156..182 | Carries `"recent_projects": []` (the writer always emits the key). Loads as an empty list, `Loaded` | `preferences.cpp:315-323`, `:270` |
| A hand-written file with no `recent_projects` | Loads as an empty list, status `LoadedWithDefaults`, diagnostic *"recent_projects is missing; using an empty list"* | `preferences.cpp:270-272` |
| `"version": 2` or any other number | `UnsupportedVersion`, defaults used, **file left untouched** | `preferences.cpp:245-250` |

Bumping to 2 would be actively harmful: it would make every settings file this
editor has ever written unreadable by this editor, to describe a field that
version 1 already describes correctly.

### 1.4 The intent machine as built, and the one thing it cannot express

`begin_session_intent` (`shell_file_paths.cpp:682-703`) is the gate. Its
`Open` arm reaches `perform_session_intent` (`:663-678`), which calls
`begin_file_action(state, FileAction::Open)` (`:606-616`) — and that **raises the
chooser**. It has no way to say *"open this specific path"*.

The one place a specific path becomes a deferred Open today is
`commit_path_choice` (`shell_file_paths.cpp:171-178`), reached only from inside
the chooser modal:

```cpp
case FilePathTarget::Action:
    if (action == FileAction::Open) {
        PendingFileApplication pending;
        pending.action = FileAction::Open;
        pending.path   = chosen;
        state->pending_file_application = std::move(pending);
    }
```

So a Recent entry needs **a targeted Open that still enters the gate**. §4 is
that design, and §5 is the proof it cannot be bypassed.

### 1.5 Where a successful project I/O lands today

Three success sites, and each is a candidate recording point:

| Action | Success site | What it already does |
|---|---|---|
| **Open** | `shell_file_paths.cpp:802-825` | `session.open(pending.path)`, then `state->project_path = pending.path`, then `adopt_session_project_into_shell(..., project_is_clean=true)` |
| **Save As** | `shell_file_paths.cpp:562-593` | `session.save(chosen)`, then `state->project_path = chosen`, `update_project_dirty_state`, `reset_runtime_asset_watch` |
| **New** | `shell_file_paths.cpp:839-860` | `session.create(options)`, then `state->project_path = pending.path`, adoption with `project_is_clean=false` — **and nothing is written to disk** (MAR-181 C4) |
| **Save** | `shell_core.cpp:664-688` | `session.save(state->project_path)`, snapshot, `project_dirty` refresh |
| **Reload** | `shell_core.cpp:625`+ via `reload_project` | Re-opens the *same* path |

`EditorSession::create` clears the saved baseline (`session.cpp:1766`) and sets
`project_dirty = true` (`:1767`) — *"a created project has never been written"*.
That is why New cannot record at creation time: there is nothing on disk to
record.

### 1.6 Target layout, and the isolation that constrains it

| Target | Sources | Constraint |
|---|---|---|
| `marrow_editor` (UI-free library) | `CMakeLists.txt:499-521`, includes `preferences.cpp` and `atomic_file_write.cpp`, **no `shell_*`** | `test_editor_session_isolation` (`preference_store_tests.cpp:864-936`) pins that an `EditorSession` is bit-identical across a full preference load/save cycle |
| `marrow_preference_tests` | `CMakeLists.txt:675-687` — the test cpp only, linking `marrow_editor` | `preference_store_tests.cpp:971-977`: *"Do not 'fix' this split by linking the shell into a unit test."* |
| `marrow_editor_shell` | `CMakeLists.txt:875-909`, every `shell_*.cpp` | Where all `ShellState` and ImGui wiring lives |

AC6 requires **"Preference and shell tests"** to cover canonicalization,
ordering, de-duplication and eviction. Those four are pure list algebra, and
`marrow_preference_tests` links only `marrow_editor`. Therefore the algebra must
compile into `marrow_editor`, and the wiring must not. §3 splits on exactly that
line.

### 1.7 Baselines measured at `9ad69c6`

| Fact | Measured |
|---|---|
| Agent registry rows | **64**, split **39 edit / 12 inspection / 10 management / 3 validation** |
| `!= 64U` guards | **10** — `shell_smoke_constraints.cpp:147,676`; `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`; `shell_smoke_timeline.cpp:3697` |
| `std::array<OperationExpectation, 64>` | **1**, `src/samples/agent_dispatch_smoke.cpp:39` |
| Python `== 64` assertions | **2**, `tools/mcp/test_client.py:53,55` |
| `kEditorSettingsVersion` | **1**, `include/marrow/editor/preferences.hpp:12` |
| `marrow_preference_tests` cases | **11**, `preference_store_tests.cpp:1218-1250` |
| File menu items | **6**: New Project…, Open Project…, Save, Save As…, Reload Project, Quit (`shell_project_panels.cpp:724-750`) |
| `~/Library/Application Support/Marrow` | **ABSENT** |

---

## 2. Decisions

Every one of these is a decision, not a preference. Each carries the reason it
beat the alternative.

### 2.1 List shape

| Property | Decision | Why |
|---|---|---|
| **Capacity** | **10**, `kRecentProjectLimit` | AC1. Eviction is from the tail after the insert, so the newest entry is never the one dropped |
| **Ordering** | Most-recent-first | AC1. `promote` is `erase(equal) ; insert(begin) ; truncate` |
| **Identity** | Byte equality of the **canonical path**, `path::native()`, **no case folding** | §2.2 |
| **Canonical form** | `std::filesystem::weakly_canonical`, falling back to `absolute(...).lexically_normal()`, falling back to the path as given | §2.3 |
| **Element type** | `std::filesystem::path`, as the store already stores | No change to the wire type |

### 2.2 "Same project" means the same canonical path string, compared bytewise

**No case folding.** This is the same rule `resolve_choice` already ships, for
the same stated reason (`shell_file_paths.hpp:161-166`): *"folding here would
make the editor's own acceptance depend on the host, and a rejected `.MARROW` is
a one-keystroke fix while a silently accepted one is a portability bug."*

The consequence, stated rather than claimed away: **on macOS, opening
`/x/A.marrow` and then `/x/a.marrow` — the same file on a case-insensitive,
case-preserving volume — produces two entries.** Three alternatives were
considered and rejected:

- **Fold case.** Correct on macOS and Windows, wrong on Linux, where the two
  really are different files. Makes the list's meaning host-dependent.
- **`std::filesystem::equivalent`.** Actually correct, and would also merge
  hardlinks and symlinks. But it **requires both files to exist**, and this list
  must hold entries whose files are gone (AC4). It would need a second identity
  rule for missing entries, and would cost up to 10 `stat` pairs per record.
- **Compare inode/file-id.** Same existence requirement, plus a platform split.

The chosen rule is deterministic, testable with no filesystem at all, and never
merges two entries that a user typed differently. It is the conservative
direction: a spurious duplicate is visible and removable; a wrong merge silently
loses an entry.

### 2.3 `weakly_canonical`, not `canonical`

`std::filesystem::canonical` **throws / fails when the path does not exist**.
Entries must outlive their files (AC4), and a recorded path can vanish between
one launch and the next, so `canonical` is unusable at load time.

`weakly_canonical` resolves the longest existing prefix — including symlinks —
and appends the remainder lexically. Every call uses the `std::error_code`
overload; on error the fallback chain is
`absolute(p, ec).lexically_normal()` → `p` unchanged. **Nothing in this feature
throws**, matching `list_directory`'s already-shipped discipline
(`shell_file_paths.cpp:64-71`).

Canonicalization is not cosmetic here. `ShellState::project_path` defaults to the
**relative** `assets/fixtures/player_idle.marrow` (`shell_state.hpp:43`), and the
smoke runs with exactly that. Without canonicalization the stored list would be
relative to whatever directory the editor happened to be launched from.

### 2.4 When an entry is recorded

AC2 is explicit: *"Only successful Open, Save As, or the first successful save of
a New session records or promotes an entry; failed, cancelled, Reload, and
ordinary Save actions do not."*

| Event | Records? | Site |
|---|---|---|
| Open succeeds | **Yes** | `apply_pending_file_action`'s Open branch, after `state->project_path = pending.path` |
| Open fails | No | The early `return false` above it |
| Save As succeeds | **Yes** | `apply_save_as`, after `state->project_path = chosen` |
| Save As fails | No | The early `return false` — the path is not moved either |
| New: `create` succeeds | No — **arms** | Nothing is on disk yet (§1.5) |
| New: its **first** save succeeds | **Yes** | `save_project_file`, gated on the arm |
| Ordinary Save | No | Same function, arm absent |
| Save fails | No | The early `return false` |
| Reload (success or failure) | No | `reload_project` is not touched at all |
| Cancelled chooser / cancelled intent | No | No success site is reached |

**The arm.** "First successful save of a *New* session" is a claim about which
action created the session; no property of the document distinguishes it. So the
discriminator is a single, explicit, inspectable field —
`ShellState::pending_recent_on_first_save`, an
`std::optional<std::filesystem::path>` — set **only** by the successful `create`
branch to the path that session was created at, and consumed by the *one*
`save_project_file` success that writes that exact path.

This is the shape MAR-182 chose for `dirty_intent` over a hidden global, and for
the same two reasons: a UI-free test can read it, and its lifetime is auditable.
It is deliberately **not** a bare boolean: the comparison
`saved_path == *pending_recent_on_first_save` means a Save As that moves the path
elsewhere cannot accidentally consume the arm.

Two rejected alternatives:

- **`!fs::exists(project_path)` sampled before the save.** Content-keyed and
  attractive, and it handles the common case exactly. But a New project whose
  target path already exists — legal, because the New form's chooser uses
  `FilePathMode::SaveTarget`, which accepts an existing file with the note
  *"Replaces the existing file."* — would silently never be recorded.
- **"Record iff the path is not already the head of the list."** Also
  content-keyed, and it makes an ordinary Save a no-op *most* of the time. It
  fails on the shipped startup flow: `marrow_editor_shell --project X` loads `X`
  through `reload_project`, which records nothing, so `X` is not at the head, so
  the first Ctrl+S would record it — violating AC2's "ordinary Save actions do
  not". This is exactly the inversion I1 below, and it is why the arm exists.

### 2.5 Pruning: **never automatically. Not on load, not on display, not on click.**

AC4 requires missing entries to *remain visible but disabled*, with explicit
Remove and Clear Missing actions. So the only question is whether anything
*else* also prunes. It does not, and here is why each alternative was rejected:

| Where | Failure mode if chosen |
|---|---|
| **On load** | A project on an unmounted external volume or a sleeping network share disappears from the list permanently, on a launch where the user did nothing. Unrecoverable — the entry is gone from disk too, because pruning-on-load implies writing on load. |
| **On display** | Same data loss, plus it makes drawing a frame a filesystem *write*. |
| **On click** | The one case where the user has just expressed intent — the worst moment to delete their bookmark. AC4 also makes the entry non-clickable, so this is unreachable anyway. |
| **Never (chosen)** | An entry can point at a file that is gone. It renders disabled, and the user removes it when they mean to. |

**Existence is evaluated at display time only**, once per entry per frame the
submenu is open, via `fs::is_regular_file(p, ec)`. That is content-keyed in the
sense §11's lesson intends: the menu reads what the path *is* right now, so a
remounted volume re-enables its entry with no user action and no state to
invalidate. It is the same choice `list_directory` already made
(`shell_file_paths.cpp:72-75`): *"Recomputed every frame: no cache, no watcher…
a cache would buy nothing and would need an invalidation rule."*

The cost, stated: up to **10** `stat` calls per frame while the submenu is open,
and a `stat` on a dead network mount can block. Bounded, only while a
user-opened menu is up, and no worse than the directory browser MAR-181 shipped.

**Loading never writes.** `load_shell_preferences` normalizes the list *in
memory* (canonicalize, drop empties, dedup keeping the first occurrence, cap at
10) and writes nothing. So a settings file the user is midway through hand-editing
survives a launch untouched — the guarantee `shell_preferences.hpp:17-21` already
makes for `default_curve`. An oversized list on disk (say 40 entries) is
truncated to 10 in memory and stays 40 on disk until the next real mutation.
Stated, not hidden.

### 2.6 Where it renders, given there is no file dialog

There is still no file dialog in this codebase — MAR-181 measured exactly one hit
for the concept in the whole tree, and it is prose. The surface is therefore
in-ImGui, in the File menu, immediately after `Open Project...`:

```
File
 ├ New Project...
 ├ Open Project...
 ├ Open Recent                    ▸   (BeginMenu, disabled when the list is empty)
 │   ├ player_idle.marrow##/abs/path/player_idle.marrow      (enabled iff it exists)
 │   ├ … up to 10, most-recent first …
 │   ├ ──────────
 │   ├ Remove                     ▸   (BeginMenu, one item per entry, ALWAYS enabled)
 │   │   └ … the same 10 labels …
 │   └ Clear Missing                  (enabled iff ≥1 entry is missing)
 ├ ──────────
 ├ Save / Save As... / Reload Project / Quit
```

**Why a nested `Remove` submenu rather than a per-row control.** A missing entry
is disabled, and a disabled `MenuItem` cannot be clicked — so Remove cannot live
on the row it removes. A parallel, always-enabled `Remove` submenu gives every
entry, present or missing, exactly one removal affordance, keeps the primary
action (open) at **one click**, and is reachable by the existing mouse-probe
harness. A right-click context menu was rejected: disabled items do not open one,
which is precisely the case that needs it.

**Label form: `"<filename>##<canonical path>"`.** Everything after `##` is
excluded from display and included in the ImGui id — the same fact
`shell_file_paths.hpp:52-54` already records for the modal names. This gives a
short display, an id that cannot collide between two directories holding the same
filename, and an exact probe string for the mouse case. The full path is shown as
a hover tooltip.

### 2.7 How it is driven headlessly under `--auto-close`

The same split MAR-182 used, for the same reason: most of this story's cases
never render a frame. Every decision lives in a UI-free function on `ShellState`,
and the menu is a thin caller:

| Surface widget | UI-free seam it calls |
|---|---|
| A recent entry | `open_recent_project(state, path)` |
| `Remove > <entry>` | `forget_recent_project(state, path)` |
| `Clear Missing` | `forget_missing_recent_projects(state)` |
| (enabled flag) | `recent_project_exists(path)` |

Six of the seven new shell cases call those seams directly. The seventh drives a
real mouse through the real menu, because the other six would all pass with every
menu item deleted — MAR-181's C9 lesson, restated by MAR-182's C19 phase 4.

### 2.8 Preference writes

Every mutation goes through one function, `persist_recent_projects(ShellState*)`,
which mirrors `set_shell_default_curve` (`shell_preferences.cpp:92-108`) line for
line:

1. **No-op skip.** If the list operation did not change the vector, return
   without touching the disk. (`set_shell_default_curve:94` does the same for an
   unchanged curve.)
2. Save the **loaded** `state->preferences`, mutated in place — never a fresh
   value — so `default_curve` and `preserved_root` survive.
3. `PreferenceStore` default-constructed at the call site, so
   `MARROW_CONFIG_HOME` is honoured, and atomicity comes free from
   `write_file_atomically`.
4. On failure: set `state->error_message`, **keep the in-memory change**, return
   false. Identical to `set_shell_default_curve:100-102`.

`fsync` remains a non-goal, inherited verbatim from MAR-180 §3.2. A crash between
the temp file's creation and the rename can leave one orphan `*.tmp.*` beside
`editor-settings.json`. Stated, not claimed away.

### 2.9 Malformed and hostile settings files

The store already handles the wire; the feature handles the *values*. Nothing in
this table is new parse code — the first four rows are already shipped and
already tested, and the plan re-asserts them rather than reimplementing them.

| On-disk `recent_projects` | Result |
|---|---|
| Absent | Empty list, `LoadedWithDefaults` (`preferences.cpp:270`) — **shipped** |
| `false`, `7`, `"x"`, `{}` | Empty list, `LoadedWithDefaults` (`preferences.cpp:273`) — **shipped** |
| Array containing `7`, `null`, `""` | Those entries skipped, the rest kept (`preferences.cpp:281`) — **shipped** |
| `[]` | Empty list, `Loaded` — **shipped** |
| 40 valid entries | Normalized to the first 10 after dedup; disk untouched until the next mutation — **new** |
| Two entries that canonicalize equal | Deduped to the first, order preserved — **new** |
| A relative entry (`"a/b.marrow"`) | Canonicalized against the process CWD at load — **new** |
| An entry naming a directory, or a `.txt` | Kept. Rendered **disabled** (`is_regular_file` is false for a directory), removable. No extension filter — the list records what was opened, and an extension check would be a second, weaker identity rule |

### 2.10 The registry does not change

AC5 requires *"no C++ agent or Python MCP mutation"*. Structurally: this feature
touches only preference state, and there is no agent operation that reads or
writes a preference — the 64-row registry (`agent_dispatch.cpp:29-96`) has no
open, create, reload, close, or settings row, as MAR-182 §1.2 measured.

Proof obligation, not assertion: an **empty** `git diff --stat` over
`src/editor/agent_dispatch.cpp`, `src/editor/agent_handlers_*.cpp`,
`src/samples/agent_dispatch_smoke.cpp` and `tools/`, plus a re-measure of
64 / 39-12-10-3, the 10 guards, the 1 array and the 2 python assertions.

---

## 3. The module split

Two new translation units, on the line §1.6 draws.

### 3.1 `include/marrow/editor/recent_projects.hpp` + `src/editor/recent_projects.cpp` → `marrow_editor`

Pure list algebra over `std::vector<std::filesystem::path>`. **References no
`EditorSession`, no `ProjectData`, no `ShellState`, no `PreferenceStore`, no
ImGui.** Compiled into `marrow_editor` so `marrow_preference_tests` reaches it
without linking the shell — honouring `preference_store_tests.cpp:971-977`.

```cpp
namespace marrow::editor {

/// AC1's bound. Ten entries, most-recent first.
inline constexpr std::size_t kRecentProjectLimit = 10;

/// weakly_canonical -> absolute+lexically_normal -> as given. Never throws.
std::filesystem::path canonical_recent_path(const std::filesystem::path& path);

/// True iff the path names a regular file right now. Never throws.
bool recent_project_exists(const std::filesystem::path& path);

/// erase-equal, insert-at-front, truncate. @return whether @p list changed.
bool promote_recent_project(
    std::vector<std::filesystem::path>* list,
    const std::filesystem::path& path);

/// @return whether @p list changed.
bool forget_recent_path(
    std::vector<std::filesystem::path>* list,
    const std::filesystem::path& path);

/// Drops every entry that is not a regular file. @return whether @p list changed.
bool drop_missing_recent_paths(std::vector<std::filesystem::path>* list);

/// Canonicalize, drop empties, dedup keeping the FIRST, cap.
/// @return whether @p list changed. Idempotent: a second call returns false.
bool normalize_recent_paths(std::vector<std::filesystem::path>* list);

} // namespace marrow::editor
```

Every mutator returns *whether it changed anything*, because that boolean is what
§2.8's no-op skip is keyed on. `normalize_recent_paths` being idempotent is an
assertion the tests make, not a comment: it is the invariant that lets every
later operation assume an already-normalized list.

### 3.2 `src/editor/shell_recent_projects.{hpp,cpp}` → `marrow_editor_shell`

Binds the algebra to `ShellState`, the store, and the menu.

```cpp
namespace marrow::editor::shell {

constexpr char kRecentMenu[]        = "Open Recent";
constexpr char kRecentRemoveMenu[]  = "Remove";
constexpr char kRecentClearMissing[]= "Clear Missing";

/// "<filename>##<canonical path>" -- short display, unique id, exact probe.
std::string recent_menu_label(const std::filesystem::path& path);

/// The ONE writer of editor-settings.json in this feature. No-op when unchanged.
bool persist_recent_projects(ShellState* state, bool changed);

void record_recent_project(ShellState* state, const std::filesystem::path& path);
void forget_recent_project(ShellState* state, const std::filesystem::path& path);
void forget_missing_recent_projects(ShellState* state);

/// THE only way a Recent entry becomes an Open. Delegates to the MAR-182 gate.
void open_recent_project(ShellState* state, const std::filesystem::path& path);

/// The submenu body. Called from inside BeginMenu("File").
void draw_recent_projects_menu(ShellState* state);

} // namespace marrow::editor::shell
```

---

## 4. Threading a targeted Open through the MAR-182 gate

### 4.1 The change

`SessionIntent` gains **no new value**. A Recent open *is* an Open; MAR-182's own
comment (`shell_file_paths.hpp:113-115`) makes the same argument for Quit —
*"They are the same intent from two origins; nothing downstream needs to tell
them apart."* What it gains is a **destination**:

```cpp
struct DirtyIntentRequest {
    SessionIntent intent{SessionIntent::New};
    /// Non-empty ONLY for a targeted Open (a Recent entry). Empty means
    /// "raise the chooser", which is every other origin of every intent.
    std::filesystem::path path;
    DirtyIntentPhase phase{DirtyIntentPhase::Prompting};
    bool opened{false};
};

void begin_session_intent(
    ShellState* state,
    SessionIntent intent,
    const std::filesystem::path& path = {});
```

Three edits inside `shell_file_paths.cpp`:

1. **`arm_open(ShellState*, const fs::path&)`** — a new file-local helper holding
   the four lines `commit_path_choice:174-178` already contains.
   `commit_path_choice` calls it; so does the targeted branch below. One writer
   of a deferred Open, two callers.

2. **`perform_session_intent`** takes the path and branches on emptiness:

   ```cpp
   case SessionIntent::Open:
       if (!path.empty()) { arm_open(state, path); return; }  // a Recent entry
       begin_file_action(state, FileAction::Open);            // the chooser
       return;
   ```

   The other three intents ignore the path. It is empty for all of them, always,
   because nothing else passes one.

3. **The "last wish wins" retarget** (`shell_file_paths.cpp:692-695`) copies the
   path too:

   ```cpp
   state->dirty_intent->intent = intent;
   state->dirty_intent->path   = path;   // <-- or Recent B opens Recent A
   ```

   Both fields, or neither. Forgetting this one line is inversion I2, and it is
   the sharpest bug this change can introduce.

`tick_dirty_intent` and `resolve_dirty_intent` both read
`state->dirty_intent->intent`; they now read `->path` alongside it and pass both
to `perform_session_intent`. No other logic in the machine moves.

### 4.2 The exact call path

```
File > Open Recent > <entry>          shell_project_panels.cpp (menu body)
  -> draw_recent_projects_menu             shell_recent_projects.cpp
    -> open_recent_project(state, p)       shell_recent_projects.cpp
      -> begin_session_intent(state, SessionIntent::Open, p)   shell_file_paths.cpp:682
         |
         +-- dirty_intent already Prompting? -> retarget intent AND path, return
         +-- dirty_intent AwaitingSave?      -> return (a save must land)
         +-- session.dirty()?                -> dirty_intent = {Open, p, Prompting}
         |                                      ...prompt... Save/Discard
         |                                      -> perform_session_intent(Open, p)
         +-- clean                           -> perform_session_intent(Open, p)
                                                  -> arm_open(state, p)
                                                     -> pending_file_application
                                                        = {Open, p}
   end of frame:
      apply_pending_file_action              shell_file_paths.cpp:778
        -> session.open(p)
           +-- FAILS   -> error_message, return false. NOTHING recorded (AC2/AC6)
           +-- SUCCEEDS-> project_path = p; adopt; record_recent_project(state, p)
```

### 4.3 Why it cannot be bypassed — as a checkable obligation

`open_recent_project` is a one-line function whose only statement is the
`begin_session_intent` call. That is an argument, not a proof. The proof is four
greps the plan runs and requires to come back empty:

| # | Grep | Must return |
|---|---|---|
| P1 | `grep -n "session\.open\|session\.create\|session\.reload" src/editor/shell_recent_projects.cpp` | **0 lines** |
| P2 | `grep -n "pending_file_application\|begin_file_action" src/editor/shell_recent_projects.cpp` | **0 lines** |
| P3 | `grep -rn "pending_file_application *=" src/editor/` | Exactly the writes in `shell_file_paths.cpp` (`arm_open`, the Reload case, the two resets) and `shell_smoke_project.cpp:2668` (C11's deliberate arm). **No new file appears.** |
| P4 | `grep -rn "begin_session_intent" src/editor/` | `shell_project_panels.cpp` (New/Open/Reload/Quit/two Reload icons), `shell_file_paths.cpp` (`absorb_close_request`), `shell_recent_projects.cpp` (the one Recent call), and the smokes. **No caller anywhere else.** |

P2 is the load-bearing one: with `pending_file_application` and
`begin_file_action` both absent from the recent module, the only route from a
Recent click to a session replacement is through `begin_session_intent`, and that
function's first act is to consult `session.dirty()`.

Behavioural confirmation is C22 (§7): a *dirty* session plus
`open_recent_project` must leave `pending_file_application` **empty** and a
`SessionIntent::Open` intent armed carrying that path.

---

## 5. What each acceptance criterion maps to

| AC | Mechanism | Covered by |
|---|---|---|
| 1 — canonical absolute, deduped, MRU, capped at 10 | §2.1-2.3, `promote_recent_project` / `normalize_recent_paths` | P-case, C20 |
| 2 — only Open / Save As / first save of New records | §2.4, the arm | C21 |
| 3 — Recent open uses the MAR-182 flow, promotes only after success | §4 | C22, C21 |
| 4 — missing paths visible but disabled; Remove and Clear Missing | §2.5, §2.6 | C23, C25 |
| 5 — atomic, never dirties the project or enters undo, no agent/MCP surface | §2.8, §2.10 | C24, the registry diff |
| 6 — preference and shell tests over all seven properties | §7 | P-case + C20-C25 |

---

## 6. Inversions, and the mechanism that makes each observable

Five consecutive stories in this arc specified an inversion that could not bite.
Every row below therefore names the *state a test reads* and the *reason that
state differs* — not merely the case that "should" fail.

| # | Inversion | Observable because | Detector |
|---|---|---|---|
| **I1** | Drop the arm: record on **every** successful save | The startup flow records nothing, so the list is **empty** before the save. Under I1 it becomes `[X]`; correct behaviour leaves it empty and leaves the settings file **absent**. *(Note: the naive shape — open A, then Ctrl+S — could NOT bite: A is already the head, `promote` returns false, no write. The case must use the startup path.)* | C21 |
| **I2** | Retarget `intent` but not `path` in "last wish wins" | Recent A then Recent B while the prompt is up, then Discard. `pending_file_application->path` is A instead of B — a plain field comparison | C22 |
| **I3** | `open_recent_project` calls `session.open` directly | Over a **dirty** session: `dirty_intent` is empty and `project_path` has moved, instead of an armed intent and a bit-identical session snapshot | C22 |
| **I4** | Promote on a **failed** open | Reuse MAR-181 C7's valid-JSON-with-missing-`.mskl` project as a recent entry. Correct: the list order is unchanged. Under I4 the broken path is at the head | C21 |
| **I5** | Prune missing entries on load | Seed a settings file naming a missing path, then `load_shell_preferences`. Correct: the entry is present in memory **and** the settings file is **byte-identical**. Under I5 the entry is gone (and, if pruning writes, so are the bytes) | C23 |
| **I6** | Skip canonicalization / dedup only on the raw spelling | Record `assets/fixtures/player_idle.marrow` and then its absolute form. Correct: **one** entry. Under I6: two | P-case, C20 |
| **I7** | Cap off by one (`> limit` for `>= limit`, or truncate before inserting) | Record 12 distinct paths. Correct: size **10**, head is the 12th, the 1st and 2nd are gone. Under I7: size 11, or the 12th itself evicted | P-case |
| **I8** | Route a record through `session.begin_edit` | On a **clean** session: `dirty()` flips false→true and `can_undo()` false→true. Four-value snapshot comparison | C24 |
| **I9** | Never emit the `Open Recent` submenu (or leave the item unwired) | Only a real mouse can see this. The sweep never finds a widget whose id equals the label's — the C9/C19 mechanism verbatim | C25 |
| **I10** | Disable the `Remove` items for missing entries | A missing entry's `Remove` click leaves it in the list. **Gated on Task 0's measurement** — see §8 |
| **I11** | Write the settings file during `load_shell_preferences` (normalize-and-save) | The settings file is byte-compared before and after the load, and must be identical — including still **absent** on a first run | C23 |
| **I12** | Drop the no-op skip in `persist_recent_projects` | Re-record the head entry: the file's bytes and mtime must not change | C24 |

---

## 7. Test surfaces

### 7.1 `marrow_preference_tests` — one new case, 11 → 12

`"recent project list algebra and isolated round trip"`. Pure, plus a
`MARROW_CONFIG_HOME`-isolated `PreferenceStore` round trip using the file's
existing `ScopedPreferenceEnvironment` / `TemporaryDirectory` helpers.

Covers: canonicalization of a relative path; `weakly_canonical` on a **missing**
path (must not throw, must still absolutize); MRU ordering; dedup on re-record;
dedup during `normalize_recent_paths`; `normalize` idempotence; eviction at
exactly 10 over a 12-entry sequence; `drop_missing_recent_paths` keeping every
present entry; every mutator's changed-bool in both polarities; and a
save→load→compare of a normalized list through the store.

It must **not** link the shell (`preference_store_tests.cpp:971-977`).

### 7.2 `marrow_editor_shell` — six new cases, C20 → C25

Numbering continues MAR-181's C4-C11 and MAR-182's C12-C19. Each declares its
prototype in `shell_smoke_scenarios.hpp` and is called from the rail at
`shell_smoke_project.cpp:4119-4170`, after the MAR-182 block.

Every case installs its **own nested** `ScopedPreferenceIsolation`. Nesting is
safe and intended: the ctor saves the outer temp dir into `previous_` and the
dtor restores it (`shell_preferences.cpp:38-66`). The outer isolation at
`shell_smoke.cpp:52` is the backstop; the nested ones give each case a clean,
empty settings file it can byte-compare.

| Case | What it asserts |
|---|---|
| **C20** — list algebra through the shell | A relative `project_path` records as an **absolute** canonical path; re-recording promotes without duplicating; 12 records leave exactly 10 with the newest at the head; the settings file round-trips through a real `PreferenceStore` and reloads equal |
| **C21** — the recording policy, all seven rows of §2.4's table | Open records; Save As records the new path; New records **nothing** until its first save, which records; a **startup** project's Ctrl+S records nothing **and leaves the settings file absent** (I1); a failed Open leaves the order unchanged (I4); a Reload records nothing |
| **C22** — the dirty gate | Clean session: `open_recent_project` arms `pending_file_application` with the path. Dirty session: it arms `dirty_intent = {Open, path, Prompting}`, leaves `pending_file_application` empty, and leaves the session snapshot bit-identical (I3). Retarget: A then B then Discard performs **B** (I2). Cancel leaves everything and the entry unpromoted |
| **C23** — missing entries and load semantics | A settings file naming one present and one missing path loads with **both** entries and a **byte-identical** file (I5, I11); `recent_project_exists` is false for the missing one; `forget_recent_project` removes exactly it; `forget_missing_recent_projects` removes every missing entry and keeps every present one; each mutation's file reloads equal |
| **C24** — non-interference and write failure | A record on a **clean** session leaves `session.dirty()`, `can_undo()`, `can_redo()` and `serialize_project(*project)` all identical (I8). Re-recording the head writes nothing — file bytes and mtime unchanged (I12). Under an RAII-scoped rename-seam failure, `persist_recent_projects` returns false, sets `error_message`, keeps the in-memory list, and leaves the settings file byte-identical |
| **C25** — real mouse through the real menu | `File` → `Open Recent` opens a child popup; every seeded entry's label is emitted (I9); a click on an entry over a **dirty** session raises `kDirtyIntentModal` and arms an `Open` intent carrying that path; `Remove > <entry>` removes it; `Clear Missing` is present and removes only the missing ones |

**C24's rename-seam hazard.** `set_preference_rename_callback_for_testing` is
process-global and shared by the settings writer and the project writer
(`atomic_file_write.hpp:22-25`). C24's installation must be RAII-scoped and must
perform **no project save** inside that scope.

**The converse hazard, which is new.** `apply_save_as` now writes the settings
file on success. MAR-181's C6 injects a rename failure around a *failed* Save As,
so no settings write occurs inside its scope — but the plan verifies this rather
than assuming it, and C5/C7 are re-run for the same reason.

---

## 8. Facts that must be MEASURED, not assumed

MAR-182's first Escape-closes-modal reading was an artifact. Every ImGui and
filesystem fact this design leans on is listed here, with the ones already
verified marked as such. **The three unmeasured ones are Task 0 gates, and each
must be printed on every run of the case that depends on it.**

| # | Fact | Status |
|---|---|---|
| M1 | A `BeginMenu` **inside a popup menu window** has id `window->GetID(label)` — i.e. `ProbeIdKind::Direct`, **not** `MenuItem` | **Verified from source.** `ImGui::BeginMenuEx` (`external/imgui/imgui_widgets.cpp:9199`) computes `const ImGuiID id = window->GetID(label);` and only *then* calls `PushID(label)`. The `##MenuBar` seed in `ProbeIdKind::MenuBarMenu` comes from `BeginMenuBar`, which a nested submenu does not go through. **Still printed at runtime by C25** |
| M2 | The ImGui window **name** of an open nested submenu popup, and whether a **hover** (not a click) is enough to open it under the smoke harness | **UNMEASURED — Task 0 gate.** C25 reads it from `OpenPopupStack.back().Window->Name` and prints it, exactly as C19 prints `File###Menu_00` (`shell_smoke_project.cpp:3706-3708`) |
| M3 | Whether the position sweep can find a **disabled** `MenuItem` — i.e. whether `g.HoveredId` is set for an item carrying `ImGuiItemFlags_Disabled` | **UNMEASURED — Task 0 gate, and it changes the test.** If disabled items are hoverable, C25 asserts the missing entry's label is *emitted but not actionable*. If they are not, C25 must assert the **effect** (a click at that position performs no open) instead of the **presence** of the widget, and say so in its printed line. Guessing here is how I10 becomes an inversion that cannot bite |
| M4 | `std::filesystem::weakly_canonical` on a path whose file does not exist returns an absolute, normalized path and sets no error on macOS | **UNMEASURED — Task 0 gate.** The P-case prints the result for a known-missing path every run. If it errors, §2.3's fallback chain is what runs, and the P-case must assert *that* |
| M5 | `~/Library/Application Support/Marrow` is absent | **Verified: ABSENT.** Re-checked before and after every run |
| M6 | `EditorSession::create` writes nothing to disk | **Verified**, MAR-181 C4 (`validate_mar181_new_project_writes_nothing`, `shell_smoke_project.cpp:2077`) |
| M7 | Nested `ScopedPreferenceIsolation` restores the outer value | **Verified from source**, `shell_preferences.cpp:39` captures `previous_`, `:57-61` restores it |

---

## 9. Non-goals

- **Anything in MAR-184.** Stepped inherit timeline overlays, `.marrow` overlay
  schema, `SkeletonData` merge, `.mskl`/`.mbin` export. Different arc.
- **A persisted "last used directory"** for the chooser. `shell_file_paths.cpp:30-32`
  names this as MAR-183's territory, but it is in **none** of MAR-183's six
  acceptance criteria, and it is a different preference with a different lifetime
  and a different failure mode. Recorded as deliberately deferred, not forgotten.
- **Recent *assets*** (skeletons, atlases) or a recent list for the New form's rows.
- **Pinning, favourites, or reordering** entries by hand.
- **Opening the most recent project at startup.** The command line stays the only
  source of the initial project.
- **Thumbnails, per-entry metadata, or last-opened timestamps.** Order carries
  recency; a timestamp would be a second, divergeable source of truth.
- **Cross-process merge.** Two editors writing the settings file concurrently:
  last atomic write wins, whole file. Same as `default_curve` today.
- **`fsync`.** Inherited non-goal from MAR-180 §3.2.
- **A file dialog.** Still none in this codebase.
- **Any agent or MCP operation.** AC5 forbids it; §2.10 proves it by diff.
- **Case-insensitive identity.** §2.2, with its consequence stated.
- **Automatic pruning.** §2.5, with each rejected variant's failure mode stated.

---

## 10. Known limitations, stated rather than hidden

1. **macOS case duplicates.** `/x/A.marrow` and `/x/a.marrow` produce two
   entries on a case-insensitive volume. §2.2.
2. **A New project created *over an existing file*, then saved, is recorded** —
   because the arm is keyed on the create, not on the file's prior absence. This
   is the *right* direction, but it means the arm, not the filesystem, is the
   authority; if the arm is ever cleared incorrectly the entry is silently missed.
   C21 asserts both polarities of the arm for exactly this reason.
3. **`stat` per entry per frame while the submenu is open.** Up to 10, and a dead
   network mount can block one. §2.5.
4. **An oversized on-disk list stays oversized** until the next mutation. §2.5.
5. **Orphan `*.tmp.*` files** beside `editor-settings.json` after a crash mid-write.
   §2.8, inherited.
6. **`shell_main.cpp`'s frame body is reachable from no test.** Unchanged by this
   story — MAR-182 recorded it, and the two hand-maintained frame bodies still
   diverge silently. This story adds nothing to either; see §11 R3.
7. **A settings write failure is reported once and then forgotten.** The
   in-memory list keeps the change, so the session behaves as if it persisted and
   the next launch disagrees. Identical to `set_shell_default_curve`'s shipped
   behaviour, and deliberately not diverged from it.

---

## 11. Risks

| # | Risk | Mitigation |
|---|---|---|
| **R1** | The `path` retarget in "last wish wins" is one line and easy to miss | I2 is the sharpest inversion in §6, and C22 asserts the *destination*, not just that something was performed |
| **R2** | `apply_save_as` and `apply_pending_file_action` now write the settings file on success, inside cases that previously wrote none | Every MAR-180/181/182 case is re-run. C6's rename-seam scope is re-read to confirm no settings write lands inside it. The nested isolations make any leak land in a temp dir, and M5 is checked after the whole run |
| **R3** | The two hand-maintained frame bodies (`shell_main.cpp`, `shell_smoke_frames.cpp`) diverge silently, and MAR-181's C11 detector catches only the smoke side | **This story adds no frame-body code.** The menu draws inside `draw_menu_bar`, which both bodies already call, and the deferral rides `apply_pending_file_action`, which C11 already pins. If an implementation finds itself editing either file, that is a design deviation and the plan says to stop |
| **R4** | M3 (disabled-item hoverability) decides C25's shape, and guessing produces an inversion that cannot bite | Task 0 gate; the measurement is printed every run and C25's assertion is chosen from it |
| **R5** | `weakly_canonical` behaviour on missing paths differs by platform | M4 gate, printed every run; §2.3's fallback chain is the specified behaviour if it errors |
| **R6** | A `MenuItem` label built from a path could collide or contain ImGui-significant characters | The `##` split makes the id the full canonical path. A path containing `##` is pathological but harmless: it shortens the display, never the id. A path containing `%` is passed as a `%s` argument, never as a format string |
| **R7** | Adding a source to `marrow_editor` could disturb the UI-free isolation | The new TU references no session, no project, no store, no shell — verified by grep in Task 0's exit check — and `test_editor_session_isolation` is re-run |
