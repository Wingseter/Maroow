# MAR-189 Commit PSD Reimports Atomically — Design

Story: **MAR-189 — Commit PSD reimports atomically**
Depends on: **MAR-188** (`71465db`), which depends on MAR-187 (`f3a3768`).
Date: 2026-09-01. Written against the tree at `cf6a199` / `71465db`.

Companion plan: `docs/superpowers/plans/2026-09-01-mar-189-atomic-psd-reimport-commit.md`.

---

## 0. Where this story sits

### 0.1 The story in one sentence

MAR-188 produced a *reviewable* plan and wrote nothing outside a caller-supplied
staging root. MAR-189 takes that staged bundle, validates it against the live
project overlay **before touching anything**, replaces the project's real
skeleton, atlas, texture and layer directory through a **journal of renames**,
adopts the new runtime sources, updates provenance — and, on any failure at any
point, puts the original bytes back.

### 0.2 The centre of gravity is AC3, and it dictates the whole design

> *"Parse/build failures and an injected failure after every commit step restore
> the original target bundle byte-for-byte and leave the active runtime source
> unchanged."*

Two words in that sentence set the architecture:

- **"every"** — not a sample. If a step exists that no mechanism can inject a
  failure after, that step is untested and the criterion is *unmet there*, no
  matter how many other steps are covered. So the failpoint mechanism is
  designed **first** (§3), the commit's step list is an **enum**, and the sweep
  iterates the enum rather than a hand-written list of interesting steps.
- **"byte-for-byte"** — not a success flag, not a file count, not "the project
  still loads". Every restoration clause compares a **full byte map of the
  target bundle**, keyed by path, captured before the commit and again after it,
  and reports the first differing path and offset (§6.2).

### 0.3 Errors found in the incoming brief and in the source documents

Recorded here rather than silently corrected, per `AGENTS.md`'s rule that a
correction naming an artefact must be resolved in the artefact.

| # | Where | Claim | Finding |
|---|---|---|---|
| **F1** | The brief | *"MAR-180 extracted `atomic_file_write.{hpp,cpp}` with a process-global `RenameCallback` seam for exactly this kind of injection; read it and decide whether it suffices"* | **It does not suffice, and the reason is structural, not stylistic.** The callback is consulted at exactly one point — `atomic_file_write.cpp:212`, inside `write_file_atomically` — so it can only inject at a *rename of a file this story writes through that function*. Five of MAR-189's fifteen commit steps perform no such rename (§3.2). Accepted as a *component* (placement uses it), rejected as *the* seam. |
| **F2** | The brief | *"MAR-189 is UI-free so [`CheckFrameBodies.cmake`] should be invisible"* | **Half right, and the other half is worse than the gate.** The gate greps only `draw_*_window(` calls between two anchors (`cmake/CheckFrameBodies.cmake:39-71`, compared at `:98`), so MAR-189 never trips it. But AC5 requires approval to *execute*, and today's approval is the `Acknowledge` button at `shell_agent_panel.cpp:148-181`, inside `draw_agent_window` — which is on the gate's own `_allowed_app_only` list because the smoke never enables `show_agent_panel`. **No test in this repository can observe that button.** See §9.1. |
| **F3** | The brief | *"`agent_dispatch_smoke.cpp` (the `std::array<OperationExpectation, N>` bound — re-derive its line…)"* | Re-derived: **`:42`**, matching MAR-188's correction. **But MAR-189 does not touch it**: `import.psd_layers` already exists (`agent_dispatch.cpp:97`), so the registry stays at **66** and none of the eleven `!= 66U` guards, neither `test_client.py:53,55`, nor any prose site moves. Verified: `grep -c '^    {"' src/editor/agent_dispatch.cpp` = 66; guards split **7** `shell_smoke_graph.cpp` / **2** `shell_smoke_constraints.cpp` / **2** `shell_smoke_timeline.cpp`. |
| **F4** | MAR-188's deliverable | `PsdReimportPlan` exposes `staged_skeleton_path`, `staged_atlas_path`, `staged_layers_directory` | **There is no `staged_texture_path`, and AC1 names textures explicitly.** `PsdImportResult::texture_path` exists (`psd_import.hpp:51`) and the planner discards it. MAR-189 must add the field (§2.2). |
| **F5** | MAR-188's deliverable | The staged bundle can be committed onto the project's targets | **It cannot, as staged today — and the failure is silent.** `plan_psd_reimport` hardcodes `staged.mskl` / `staged.matl` and `atlas_name = "staged"` (`psd_reimport_plan.cpp:143-152`). `build_packed_atlas_artifact` derives the texture as the atlas path with `.png` (`atlas_packer.cpp:1024-1025`) and `build_atlas_document_text` writes **`"image": <image_path.filename()>`** (`:880`). So the staged `.matl` says `"image": "staged.png"`. Copy it byte-identically onto `player_idle.matl` and the project references a PNG that does not exist beside it. Every byte-comparison clause still passes. This is the story's sharpest trap and §2.2 is the fix. |
| **F6** | `tools/inversion/README.md` | *"`rebuild.sh` — Deletes the named object files, then builds"*, presented as the harness's answer to the GNU Make mtime hazard | **`invert.sh` never passes any object paths.** `invert.sh:32` calls `rebuild.sh "$build_dir"` with no further arguments, and `rebuild.sh`'s `for o in "$@"` loop then iterates an empty list. The two documented guards (abort on a failed mutation build; refuse a stale baseline) **are** wired and are real. The object-deletion guarantee the README advertises is not. MAR-189 supplies the object paths itself (plan §0.10) rather than editing shared tooling mid-flight. |
| **F7** | MAR-188's "Not independently covered" | *"`preserve` is exposed and nothing consumes it. MAR-190 owns the checklist"* | Correct about the **checklist**. MAR-189 nonetheless consumes the field, because AC6 names *"missing preservation/deletion inputs"* and a commit that silently ignores `preserve` cannot satisfy it. MAR-190 still owns the UI that sets it. §2.6. |

Everything else the brief asserted resolved against the code: `validate_project_for_save`
is at `project.cpp:6077`, inside the anonymous namespace spanning `:25-7114`;
`json::Value::Object` is `std::map<std::string, Value, std::less<>>`
(`include/marrow/runtime/json.hpp:24`); `project_relative_path` is public at `project.hpp:1082`;
`marrow_psd_import_smoke` is built (`CMakeLists.txt:853`) and is **not** in CTest.

---

## 1. The measured gap

### 1.1 Nothing commits a PSD reimport, and the op says so out loud

`import.psd_layers` today (`agent_handlers_management.cpp:93-190`) validates a
whitelist, builds a four-key preview object, and either returns a dry-run
acknowledgement or queues an `ImportOrPack` review. Approval then reaches
`shell_agent_panel.cpp:148-181`, whose own comment (`:149-151`) is the honest
statement of the gap:

```
// Import/pack v1 queues targets only; execution stays in
// the CLI importers, so the button must not claim the
// operation ran.
```

The button is literally labelled `Acknowledge` rather than `Approve`, and the
status message says *"(not executed; run the CLI importer to apply it)"*.
AC5 is the request to close that.

### 1.2 The target bundle is four artefacts, not one file

For a project whose art came from a PSD, the reimport target set is:

| # | Artefact | Where it comes from |
|---|---|---|
| T1 | Extracted layer **directory** | `editor_metadata.import_sources->psd->layers_directory`, resolved |
| T2 | Atlas **texture** (`.png`) | the atlas path with `.png` (`atlas_packer.cpp:1024-1025`) |
| T3 | **Atlas** (`.matl`) | `runtime_assets.atlas_paths[n]`, resolved |
| T4 | **Skeleton** (`.mskl`) | `runtime_assets.skeleton_path`, resolved |

T1 is a directory, and `write_imported_layers` calls `std::filesystem::remove_all`
on whatever directory it is handed (`psd_import.cpp:1076`). That is the reason
MAR-188's planner takes a staging *root* and never an output path, and it is the
same reason MAR-189's commit must move a directory **aside** rather than delete
it in place.

### 1.3 A passing `save()` proves nothing, and neither does a passing commit

`validate_project_for_save` cannot be called from another translation unit and
takes no base document, so it structurally cannot resolve a cross-reference.
Every "the project is still good" clause in this story goes through
`load_project(path)` → `build_project_runtime()`, exactly as MAR-180's do.

---

## 2. Decisions

### 2.1 A new UI-free pair, in `marrow_editor`

```
include/marrow/editor/psd_reimport_commit.hpp
src/editor/psd_reimport_commit.cpp
src/editor/psd_reimport_commit_internal.hpp   // test-only failpoint seam
```

A new pair rather than an addition to `psd_reimport_plan.hpp`, for the same
reason MAR-188 gave for splitting from `psd_import.hpp`: the planner is a pure
function of `const ProjectData&`, while the committer takes a **mutable
`EditorSession&`** and performs I/O. Keeping the read-only half free of the
session is what lets MAR-190 render a plan without linking a committer.

The internal header mirrors `atomic_file_write.hpp`'s convention verbatim — a
private source header carrying a process-global test seam with a documented
HAZARD block.

```cpp
struct PsdReimportCommitOptions {
    std::filesystem::path project_path;   // provenance is relativized against this
    bool update_provenance{true};
};

struct PsdReimportCommitResult {
    bool ok{false};
    std::string error;                              // names the step and the cause
    std::optional<PsdCommitStep> failed_step;
    std::vector<PsdCommitStep> steps_executed;      // the ledger; see §3.3
    bool rolled_back{false};
    std::string rollback_error;                     // §5.3's unrecoverable corner
    std::vector<std::filesystem::path> journal_residue;
    explicit operator bool() const { return ok; }
};

PsdReimportCommitResult commit_psd_reimport(
    EditorSession& session,
    const PsdReimportPlan& plan,
    const PsdReimportCommitOptions& options);
```

The commit takes the **plan**, never a PSD path. It does not re-parse. The
bundle that is committed is the bundle that was reviewed — bit for bit, because
it is the same files on disk.

### 2.2 The planner must stage under the TARGET file names (F4, F5)

Two additions to MAR-188's API, both defaulted so every existing MAR-188 case
compiles and passes unchanged:

```cpp
struct PsdReimportPlanOptions {
    std::filesystem::path psd_path;
    std::filesystem::path staging_root;
    // MAR-189. Default preserves MAR-188's behaviour exactly.
    std::string staged_skeleton_filename{"staged.mskl"};
    std::string staged_atlas_filename{"staged.matl"};
};

struct PsdReimportPlan {
    // …
    std::filesystem::path staged_texture_path;   // MAR-189
};
```

`plan_psd_reimport` sets `import_options.atlas_name` from
`staged_atlas_filename`'s stem rather than the literal `"staged"`, and reports
`staged_texture_path` as the staged atlas path with `.png` — **derived by the
importer and copied out of `PsdImportResult::texture_path`, not recomputed**, so
the two cannot drift.

The committer always passes the target file names. The consequence, and the
whole point: the staged `.matl` says `"image": "player_idle.png"` while it is
still in staging, so placing it is a pure byte copy and the committed atlas
resolves its texture. Inversion **I6** reverts this and is caught by exactly one
clause (§7).

*Alternative rejected:* rewriting the `image` member of the staged atlas text at
commit time. It re-parses and re-serialises a document the packer just wrote,
introduces a second place that knows the atlas schema, and would silently do
nothing if the key were ever renamed. Naming the file correctly on the way out
has no failure mode.

*Measured note:* the atlas document's `"name"` member is parsed
(`atlas.cpp:189-196`) and only ever displayed; region lookup is by region name
(`:280`). Setting it from the target stem is correctness hygiene, not a
dependency.

### 2.3 Validation happens in staging, against the live overlay, before any target moves

AC1: *"validated together with the current project overlay before any target is
changed."* `EditorSession::adopt_runtime_sources` already performs precisely the
right validation — `load_skeleton_document`, every `AtlasLoader::load`,
`auto_extend_explicit_animation_durations` on a project copy, then
`build_project_runtime` — but it reads the project's **resolved** paths
(`session.cpp:1919-1965`), which are the targets. Calling it is therefore
validation *after* the fact.

So `ValidateStagedBundle` mirrors that sequence against the **staged** artefacts:

1. `runtime::load_skeleton_document(plan.staged_skeleton_path)`;
2. `runtime::AtlasLoader::load(plan.staged_atlas_path)` for the atlas being
   replaced, plus every *other* `resolved_atlas_paths()` entry unchanged;
3. a `ProjectData` copy carrying the **proposed** provenance, through
   `auto_extend_explicit_animation_durations`;
4. `build_project_runtime(copy, staged_document)`.

If a project overlay names a bone, slot or attachment the candidate PSD no
longer produces, this is where it is refused — with nothing on disk touched.
The later `AdoptRuntimeSources` step is then a re-run that should not fail; that
it *can* still fail (a concurrent external write) is why it is a journalled step
and not an afterthought.

*This step is not a duplicate of adoption. It is the same computation over
different inputs, which is the only way to have the answer before the inputs
become the project's.*

### 2.4 Replacement is rename-aside, never rewrite-in-place

Per target, two journalled steps:

```
Backup<X>:  rename  <target>            ->  <target>.marrow-journal-<id>.bak
Place<X>:   write   staged bytes        ->  <target>            (temp + rename)
```

Three properties this buys, each of which a rewrite-in-place design loses:

- **Rollback returns the original bytes, not a re-encoding.** The original file
  is never read, never re-serialised, never written. It is *moved back*. The
  byte-for-byte guarantee is therefore a property of the mechanism rather than
  of the fidelity of a writer.
- **The backup rename never crosses a filesystem.** The backup sits in the
  target's own parent directory, exactly as `write_file_atomically` puts its
  temporary in the destination's own directory (`atomic_file_write.cpp:61-70`).
- **Placement is cross-filesystem safe.** The staging root is caller-supplied
  and may be on another volume, so placement reads the staged bytes and goes
  through `write_file_atomically(target, bytes, subject)` — which creates its
  temporary beside the *target* and renames locally. `write_file_atomically`
  takes `std::string_view` and writes it with `fwrite`/`WriteFile` by length
  (`:183`, `:134`), so it is binary-safe for PNG payloads. This is the component
  reuse of MAR-180 that F1 rejected as a *seam* and accepts as a *primitive*.

T1, the layer directory, is a single `rename` of the whole directory aside,
then `create_directories` plus a per-file copy of the staged layers in. Rollback
is `remove_all` of the new directory and a rename of the backup back.

### 2.5 The journal

Written before the first backup, into the project file's own directory:

```
<project_dir>/.marrow-psd-journal-<pid>-<seq>.json
  { "targets": [ { "target": "<abs>", "backup": "<abs>", "kind": "file|directory" }, … ] }
```

Its purpose is not recovery-on-next-launch — nothing in this tree replays a
journal, and pretending otherwise would be a claim no test backs. Its purpose is
that a crash leaves a **discoverable, self-describing** record of exactly which
`.bak` belongs to which target, instead of four orphaned files whose meaning has
to be guessed. `write_file_atomically` explicitly does not fsync (its own doc
comment), so durability across power loss remains a non-goal here too, stated
rather than implied.

`CleanJournal` removes every backup and the manifest. It is the **last** step
and it is past the point of no return: a failure there leaves a correct, fully
committed bundle plus some residue, so it reports `ok = true` with a populated
`journal_residue`. §6.3's sweep encodes that as a per-step expectation, which is
the only reason a uniform `!ok` assumption does not silently pass.

### 2.6 `preserve` is consumed, and deletion is a staged-document edit (F7)

A `Missing` layer with `preserve == true` (the default MAR-188 ships) survives
because the importer merges the candidate onto the existing skeleton
(`psd_import.cpp:1233-1258`), so its slot and attachment are already in the
staged document.

A `Missing` layer with `preserve == false` must **not** survive. The commit
therefore prunes it — from the **staged skeleton document**, in a step
(`PruneUnpreserved`) that runs *before* `ValidateStagedBundle`, so the pruned
result is what gets validated against the overlay. Pruning after validation
would validate a document the project never receives.

Pruning uses the provenance record's own `slot_name` / `attachment_name`, which
is why MAR-188 stored them: the PSD is gone, so the layer's identity is the only
handle left. A `preserve == false` layer whose slot is not present in the staged
document is an error naming the identity, not a silent skip.

### 2.7 Ordering, and where the single irreversible window sits

```
ValidateRequest → ValidateStagedBundle ← (PruneUnpreserved runs between them)
  → OpenJournal
  → BackupLayers → BackupTexture → BackupAtlas → BackupSkeleton
  → PlaceLayers  → PlaceTexture  → PlaceAtlas  → PlaceSkeleton
  → AdoptRuntimeSources
  → UpdateProvenance
  → CleanJournal
```

`AdoptRuntimeSources` is placed **before** `UpdateProvenance` deliberately.
Adoption is the far likelier failure (it does I/O and a full runtime build) and
`EditorSession::adopt_runtime_sources` is documented and measured to leave the
session **entirely unchanged** on failure (MAR-180 S-cases). So the common
failure needs only a file rollback.

The remaining window is a failure at `UpdateProvenance`, i.e. after a successful
adoption. Rollback there is: restore the files first, *then* re-adopt. The
re-adopt reads exactly the bytes the pre-commit session read, because the files
were restored by rename. If it still fails, `rollback_error` names it and the
result is `ok = false` with a distinct message. §9.2 records this as a stated
limitation rather than a solved problem.

### 2.8 The agent operation, reworked in place — the registry does not move

`import.psd_layers` keeps its name, category, flags and handler
(`agent_dispatch.cpp:97`). Behaviour changes:

- **Targets come from the project.** `output` / `atlas_output`, when supplied,
  must normalize to the project's own `resolved_skeleton_path()` and the atlas
  they name; otherwise the op errors with `not_project_bundle`. Omitted, they
  default to the project's bundle. *This is a deliberate narrowing*: the op is
  now the reimport-commit path, and a reimport into `/tmp` is not a reimport of
  anything. The two existing invocations in `agent_dispatch_smoke.cpp:1621` and
  `:4479` pass explicit `/tmp` paths and are updated to the project's bundle
  (§9.4 covers the fixture-mutation hazard this creates).
- **Dry run returns the plan.** It stages into a temp root, returns the ordered
  `(identity, kind, current_*, proposed_*)` rows plus the three counts and the
  staged paths, and then removes the staging root. The existing "dry-run
  immutability" assertions still hold: staging is not a target.
- **The staging root is a write target and is whitelist-checked** through
  `agent_path_allowed`, with its own `forbidden_path` rejection. `/tmp` and
  `/private/tmp` are already allowed (`agent_dispatch.cpp:626-629`).
- **A queued review carries a digest of the reviewed plan** — the ordered row
  list — plus the PSD path. Approval re-plans into a fresh staging root and
  refuses if the digest differs (`psd_changed_since_review`). Holding a staging
  directory open across an unbounded human wait was rejected: it leaks on
  reject, on quit, and on crash, and it makes the review queue own a filesystem
  lifetime it has no way to bound.

### 2.9 Approval executes, through a UI-free function

```cpp
// include/marrow/editor/agent_dispatch.hpp
AgentDispatchResult apply_agent_review(
    EditorSession& session,
    AgentControlState& control,
    std::uint64_t review_id);
```

It checks `request.allowed` (the whitelist verdict recorded at enqueue time),
re-plans, compares the digest, calls `commit_psd_reimport`, and removes the
request from the queue only on success or on a definitive refusal.
`shell_agent_panel.cpp` changes `Acknowledge` back to `Approve` for
`ImportOrPack` and calls this function. **The function is what the tests drive**;
the button is not observable (§9.1).

`AgentReviewRequest` gains two fields — `std::filesystem::path input_path` and
`std::string plan_digest` — both empty for `SaveProject` and `ExportRuntime`.

### 2.10 MCP parity, and the one thing it deliberately does not get

`tools/mcp/tools/editing.py`'s `import.psd_layers` schema gains `staging_root`
and documents the plan-shaped response. `tools/mcp/test_client.py` asserts the
schema properties directly (the pattern MAR-179 established there: a wire-level
call succeeds even when the schema omits a property, so the schema must be
asserted as an object) and asserts a live dry-run returns `plan` with the three
counts.

**MCP gets no approve tool.** Approval stays editor-only, following
`agent.resume`'s precedent — *"only the editor can restore access"*
(`agent_handlers_management.cpp:50-51`). AC5 says the MCP tool exposes the same
*checks*, which it does: dry-run plan, whitelist, and the requirement of review.
A stricter reading — that MCP must be able to approve — would need a 67th
operation and would move eleven guards, two Python assertions and two prose
sites. §9.3 records this as an interpretation, flagged rather than assumed.

### 2.11 What MAR-189 does not do

- No GUI for reviewing a plan or ticking `preserve` — MAR-190.
- No journal replay on next launch (§2.5).
- No fsync; durability across power loss remains MAR-180's stated non-goal.
- No change to the PSD parser, the atlas packer, or the `.marrow` schema beyond
  the provenance MAR-188 already added.
- No fix for the slot-name instability MAR-188 recorded, nor for the unverified
  Photoshop layer-record ordering it flagged to this story. Both are properties
  of the importer; neither is reachable from the commit path.

---

## 3. The failpoint mechanism — designed before the cases

### 3.1 Why `RenameCallback` is not enough (F1)

`atomic_file_write.cpp:212` is the sole consult point. It fires only inside
`write_file_atomically`. Of MAR-189's fifteen steps, **six** perform a rename
through that function (the four `Place*` steps plus the journal manifest write,
and `PlaceLayers` does one per layer file). The other eight — `ValidateRequest`,
`PruneUnpreserved`, `ValidateStagedBundle`, `OpenJournal`'s bookkeeping, the four
`Backup*` steps' direct `std::filesystem::rename`, `AdoptRuntimeSources`,
`UpdateProvenance`, `CleanJournal` — are unreachable from it.

Three further problems, any one of which would be disqualifying:

- The callback is keyed on `(source, destination)`. Distinguishing "after
  `PlaceAtlas`" from "after `PlaceTexture`" means pattern-matching a path, which
  breaks the moment two steps write into the same directory.
- It is **process-global and shared with the settings and project writers** (its
  own HAZARD block says so). A blanket-failing callback also breaks any
  `save_project` the commit path or its test performs.
- It injects *inside* the write, before the rename lands — which is a failure
  **during** a step, not the failure **after** a step that AC3 names.

### 3.2 The seam

```cpp
// src/editor/psd_reimport_commit_internal.hpp
enum class PsdCommitStep {
    ValidateRequest,
    PruneUnpreserved,
    ValidateStagedBundle,
    OpenJournal,
    BackupLayers, BackupTexture, BackupAtlas, BackupSkeleton,
    PlaceLayers,  PlaceTexture,  PlaceAtlas,  PlaceSkeleton,
    AdoptRuntimeSources,
    UpdateProvenance,
    CleanJournal,
};

/// Returns a non-empty string to inject that error AFTER `step` completes.
using CommitFailpoint = std::function<std::string(PsdCommitStep)>;
void set_psd_commit_failpoint_for_testing(CommitFailpoint callback);

/// Every step, in execution order. The ONLY list; see §3.3.
extern const std::array<PsdCommitStep, 15> kAllCommitSteps;
const char* psd_commit_step_name(PsdCommitStep step);
```

Mutex-guarded and RAII-scoped by the caller, exactly as MAR-180's seam is.

### 3.3 One call site, so "declared but unreachable" is impossible

The commit body never touches `steps_executed` or the failpoint directly. Both
live in one private helper:

```cpp
bool advance(PsdCommitStep step);   // append to the ledger, then consult the failpoint
```

called immediately after each step's body succeeds. Two consequences, and they
are the reason this is worth the ceremony:

- **A step that is declared in the enum but never advanced through cannot be
  injected — and also cannot appear in the ledger.** So the hole is not silent:
  case **R0** compares a successful commit's `steps_executed` against
  `kAllCommitSteps` as a full ordered identity list with a set-difference
  message, and the missing step is named.
- **A step that fails on its own is not in the ledger** (advance runs only after
  success), which is what case **R2(b)** asserts. The two clauses together pin
  the ledger's meaning from both sides.

`psd_commit_step_name` is a `switch` with **no `default:`**, so Clang's
`-Wswitch` names any added value at every build. `AGENTS.md` is explicit that
this **warns and does not fail**, and that GCC is silent without `-Wall`, so the
warning is a convenience and **R0's identity assertion is the actual detector**.

**Explicitly rejected:** `static_assert(kAllCommitSteps.size() == kStepCount)`
where `kStepCount` is a literal maintained beside the array. `AGENTS.md` records
that shape as a **proven tautology** that catches nothing. The array is the
list; the identity assertion is the gate.

### 3.4 The failpoint can also *observe*

Because the callback runs inside the commit, it can list the filesystem
mid-flight. That is the only mechanism in this design that can prove the journal
**exists while the commit is in flight** — after it, either outcome has removed
it. Case **R7** uses a failpoint at `BackupSkeleton` that captures a directory
listing and then injects, asserting the manifest and three `.bak` files were
present at that instant *and* absent after the rollback.

---

## 4. Where each piece lands

| File | Change |
|---|---|
| `include/marrow/editor/psd_reimport_plan.hpp` | `staged_skeleton_filename`, `staged_atlas_filename` (defaulted), `staged_texture_path` |
| `src/editor/psd_reimport_plan.cpp` | honour the filenames; set `atlas_name` from the atlas stem; copy `texture_path` out of the import result |
| `include/marrow/editor/psd_reimport_commit.hpp` | **new** — options, result, `commit_psd_reimport` |
| `src/editor/psd_reimport_commit_internal.hpp` | **new** — `PsdCommitStep`, `kAllCommitSteps`, the seam |
| `src/editor/psd_reimport_commit.cpp` | **new** — the fifteen steps, journal, rollback |
| `include/marrow/editor/agent_control.hpp` | `AgentReviewRequest::input_path`, `::plan_digest` |
| `include/marrow/editor/agent_dispatch.hpp` | `apply_agent_review` |
| `src/editor/agent_dispatch.cpp` | carry the two new fields through `enqueue_review` / `review_to_json`; implement `apply_agent_review` |
| `src/editor/agent_handlers_management.cpp` | project-derived targets, dry-run plan, staging whitelist |
| `src/editor/shell_agent_panel.cpp` | `Approve` for `ImportOrPack`, calling `apply_agent_review` |
| `src/samples/psd_import_smoke.cpp` | `validate_mar189_reimport_commit` — R0–R8 |
| `src/samples/agent_dispatch_smoke.cpp` | A1–A6; update the two existing invocations |
| `tools/mcp/tools/editing.py` | schema |
| `tools/mcp/test_client.py` | M1–M3 |
| `CMakeLists.txt` | the new source; register `marrow_psd_import_smoke` as `marrow.psd_import_smoke` |
| `AGENTS.md` | `## Current Validation` entry; `## MAR-189 … Validation Results` |
| `docs/root1/format-spec.md` | none — no schema change (proved by an empty diff, plan Task 8) |

---

## 5. Acceptance criteria → artifacts

| AC | Satisfied by | Proved by |
|---|---|---|
| AC1 staged bundle validated with the overlay before any target changes | §2.3 `ValidateStagedBundle`, §2.6 prune-before-validate | R2(b), I7 |
| AC2 UI-free commit API, journalled renames, success only after every replacement and adoption | §2.1, §2.2, §2.4, §2.5, §2.7 | R0, R1, **R1b**, I8 |
| AC3 parse/build failure and an injected failure **after every step** restore bytes and leave the runtime unchanged | §3 seam + ledger, §2.4 rename-aside | R2(a), R2(b), **R3 (the sweep)**, R6, I2, I3, I4, I12 |
| AC4 success updates provenance and mappings, preserving unrelated overlays | §2.7 `UpdateProvenance` | R4, I9, I10 |
| AC5 op + MCP tool: same dry-run plan, whitelist, approval; execute after approval | §2.8, §2.9, §2.10 | A1–A6, M1–M3, I14, I15, I16, I17 |
| AC6 tests cover success, approval boundaries, preservation/deletion, overlays, every failpoint, rollback bytes, journal cleanup | §6 | R0–R8, A1–A6, M1–M3 |

---

## 6. Test surfaces and the rules every case obeys

### 6.1 The rules

1. **Assert the message, not `!result`.** Every refusal clause names the
   expected text. (`AGENTS.md`: bitten five times in this chain.)
2. **A count is almost never the fact.** Every set comparison is a full sorted
   identity list with a set-difference message.
3. **Author in memory what a round trip would corrupt.** `serialize_project` is
   not bit-exact for 17-significant-digit doubles, and a `.marrow` round trip
   normalises overlay order because `Value::Object` is a `std::map`. R4 compares
   overlay vectors **in memory**, before and after, and never through a file.
4. **`preserved_root` is an empty *object*, not null.** Any clause about it uses
   `is_object() && as_object().empty()`.
5. **A case green before the implementation exists is a witness, not a gate**,
   and is labelled with either the inversion that reddens it or the statement
   that none does.
6. **Assert revisions only where they are supposed to be still.** A successful
   commit *bumps* `runtime_revision()` (adoption does that); asserting it
   unmoved on the success path is a gate that fails on correct code. Failure
   cases assert it unmoved; the success case asserts it **moved**.

### 6.2 The byte map

```cpp
// psd_import_smoke.cpp
std::map<std::string, std::string> bundle_bytes(const ProjectData& project);
```

Keyed by path relative to the project directory, over: every file under the
resolved layers directory (recursive), the texture, the atlas, the skeleton, and
the `.marrow` itself. Values are the **whole file contents**, not a hash — the
bundle is ~20KB and holding the bytes lets a failure message name the first
differing offset instead of only saying "different". Comparison reports, in one
message: paths only in `before`, paths only in `after`, and for each common path
the first differing offset with both byte values.

### 6.3 Cases — `validate_mar189_reimport_commit` (psd_import_smoke)

Run **in this order**; attribution below is by run order.

| # | Case | Load-bearing clauses |
|---|---|---|
| **R0** | Step ledger identity | A successful commit's `steps_executed` equals `kAllCommitSteps` **in order**; `error` empty; `rolled_back == false`; `journal_residue` empty. Set-difference message names any missing or extra step |
| **R1** | Success replaces the bundle | Target skeleton, atlas, texture bytes equal the staged bytes; the layers directory listing equals the staged listing (sorted, with sizes); `load_project(project)` → `build_project_runtime` succeeds; `AtlasLoader::load(target atlas)` succeeds; `session.runtime_revision()` **moved** |
| **R1b** | **The committed atlas resolves its texture** — its own case, deliberately not a clause of R1 | The committed `.matl`'s `"image"` member equals the TARGET png's file name; that file exists beside the atlas; `AtlasLoader::load` on the committed atlas resolves a region to real pixels. **Every byte clause in R1 passes while all three of these fail** (§6.5), so this must not be foldable back into R1 as "one more assertion" |
| **R2(a)** | Parse failure | A corrupt PSD → `plan_psd_reimport` errors; byte map identical; `runtime_revision()` unmoved. **Witness, not a gate** — this is MAR-188 behaviour and no MAR-189 inversion reddens it |
| **R2(b)** | Build failure before any target changes | A project overlay naming a bone the candidate does not produce → commit fails at `ValidateStagedBundle`, message names the step; `ValidateStagedBundle` is **absent** from `steps_executed`; byte map identical; `runtime_revision()` unmoved |
| **R3** | **The failpoint sweep — one iteration per `kAllCommitSteps` entry** | Per step, from a table of expectations: `RollsBack` (steps 1–14) → `!ok`, error names the step, byte map identical to the pre-commit map, `runtime_revision()` unmoved, provenance unchanged, no `*.marrow-journal*` and no `*.tmp.*` residue anywhere in the bundle's directories; `SucceedsWithResidue` (`CleanJournal`) → `ok`, byte map equals the **committed** map, `journal_residue` non-empty and every entry exists |
| **R4** | Overlay preservation and provenance update | A project carrying animation, transform-curve, inherit, constraint, mesh-weight and editor overlays: every one of those `ProjectData` vectors is compared element-by-element **in memory** before/after; provenance layer rows equal the plan's proposed rows as an **ordered** `(identity, slot, attachment, bone, image_file)` list; `source_path` and `layers_directory` are project-relative |
| **R5** | Preservation and deletion | A `Missing` layer with `preserve == true` keeps its slot and attachment in the committed skeleton; one with `preserve == false` loses exactly those and no others (full sorted slot list, set difference); its provenance row is gone and every other row is byte-identical; a `preserve == false` layer whose slot is absent from the staged document errors naming the identity |
| **R6** | Rollback after a successful adoption | Failpoint at `UpdateProvenance`: files restored, `rolled_back == true`, `rollback_error` empty, and `session.runtime_data()` exposes a slot only the **original** skeleton has |
| **R7** | Journal exists in flight, and is gone after | Failpoint at `BackupSkeleton` captures a listing at injection time: manifest present, three `.bak` present. After the rollback: byte map identical and the residue list empty |
| **R8** | Refusals without side effects | A plan carrying an `error`; a plan with an empty `staged_skeleton_path`; a plan whose staging root no longer exists; a session with no project. Each asserted on its message; byte map identical after each |

### 6.4 Cases — `agent_dispatch_smoke`

All A-cases operate on a **temp copy** of the fixture project (§9.4), set on the
harness with `harness.set_project`, the pattern `exercise_mar186_diagnostics`
already uses.

| # | Case |
|---|---|
| **A1** | Dry run returns a plan: `scene_delta` carries `layers` rows and the three counts; the target byte map is unchanged; the staging root no longer exists |
| **A2** | `output` naming a non-project path → `not_project_bundle`, message asserted |
| **A3** | `staging_root` outside the whitelist → `forbidden_path`, message asserted |
| **A4** | Non-dry-run queues one `ImportOrPack` review carrying a non-empty `plan_digest`; nothing on disk changed |
| **A5** | `apply_agent_review` on that id **commits**: byte map equals the staged bundle, provenance updated, the request is gone from the queue |
| **A6** | Approval boundaries: a whitelist-rejected request refuses without committing; an unknown id refuses; a PSD mutated between review and approval refuses with `psd_changed_since_review` |
| **A7** | The tracked fixture under `assets/fixtures/` is byte-identical at the end of the run (extends the existing `project_file_before` snapshot to the whole bundle) |

### 6.5 Byte-identity is the wrong invariant when the bytes encode a path

The rule this story is built on — *compare bytes, never a success flag* — has an
exception, and F5 is it. **A byte-for-byte copy is correct exactly when the
bytes mean the same thing in both locations.** The staged `.matl` carries
`"image": "<stem>.png"`, a *relative reference resolved against the file's own
directory*, so its meaning is a function of where the file sits. Copy it
faithfully into another directory and the copy is byte-perfect and broken.

The general form, for anything committed by copying:

> Before asserting byte-identity between a source and a destination, ask which
> bytes are **location-dependent**. Those bytes need a *semantic* clause — does
> the reference still resolve? — and byte-identity is evidence *against*
> correctness for them, not for it.

In this bundle the audit is small and worth stating in full, because "which
bytes are paths" is the question a reader will want answered rather than
re-derived:

| Artefact | Location-dependent bytes | Handled by |
|---|---|---|
| `.matl` | `$.atlas.image` — bare PNG name, resolved beside the atlas (`atlas.cpp:200`) | §2.2 (stage under the target name) + **R1b** |
| `.matl` | `$.atlas.name` — display only, verified in Task 0's A3 probe | set from the target stem; cosmetic |
| `.mskl` | none measured — attachments carry region *names*, not paths | Task 0 must confirm; if false, R1b grows a clause |
| layer PNGs | none — opaque pixels | byte-identity is sufficient |
| `.marrow` | every path family, but the commit rewrites provenance rather than copying it | §2.7 `UpdateProvenance` + **R4**'s project-relative clause |

**R1b exists because of this table**, and folding it back into R1 would put a
semantic assertion behind a heading that promises byte comparison — which is how
the trap gets re-set for the next reader.

### 6.6 Cases — `tools/mcp/test_client.py`

| # | Case |
|---|---|
| **M1** | `import.psd_layers`'s `inputSchema` declares `staging_root`; registry and MCP names still **66** and still equal as sets |
| **M2** | A live dry run returns `plan` with `added`/`updated`/`missing` |
| **M3** | A non-dry run returns `review.required == true` and a non-empty `plan_digest` |

---

## 7. Inversion register

Every entry names the mutation, the **predicted failure text**, and why nothing
earlier in run order catches it first. Predictions are recorded so the
implementer can report the *actual* text beside them.

| # | Mutation | Predicted first failure | Why nothing earlier |
|---|---|---|---|
| **I1** | `advance()` consults the failpoint but does not append to the ledger | **R0**: `steps_executed must equal kAllCommitSteps; missing: [ValidateRequest, …]` | R0 runs first, and it is the only case that reads the ledger's completeness |
| **I2** | `advance()` appends **before** the step body runs instead of after | **R2(b)**: `ValidateStagedBundle must not appear in steps_executed when it failed` | R0 and R1 are success paths where every step succeeds, so an early append is indistinguishable there |
| **I3** | Rollback restores a placed file by re-writing the **staged** bytes instead of renaming the backup back | **R3 / PlaceSkeleton**: `<project>/player_idle.mskl differs at offset 61: expected 0x22, got 0x5c` | R0–R2 never roll back a *placed* file; R2's failures happen before any placement |
| **I4** | Rollback removes the placed file but never renames the backup back | **R3 / PlaceAtlas**: `only in before: player_idle.matl` | Same as I3; and R3's earlier `Backup*` arms roll back before any placement exists to remove |
| **I5** | `PlaceTexture` is skipped (atlas placed, texture not) | **R1**: `target texture must equal the staged texture; differs at offset 0` | R0 reads only the ledger, and the ledger still lists the step |
| **I6** | Revert §2.2: stage under the hardcoded `staged` stem | **R1b**: `committed atlas references image 'staged.png'; expected 'player_idle.png'` | **Every byte clause still passes, R1 included** — the placed file is byte-identical to the staged one and wrong. R1b is the only case that reddens, which is why it is a case and not a clause. See §6.5 |
| **I7** | `ValidateStagedBundle` builds the runtime against the session's **current** skeleton document instead of the staged one | **R2(b)**: `a staged bundle dropping bone 'spine' must be refused before any target changes; the commit reported success` | R1's candidate is compatible with the overlay, so the wrong document validates identically there |
| **I8** | Omit the `AdoptRuntimeSources` step entirely | **R0**: `steps_executed must equal kAllCommitSteps; missing: [AdoptRuntimeSources]` | R0 is first by run order. R1's runtime clause and R6 are the *semantic* detectors and both also redden; R0 is the attribution |
| **I9** | Write provenance layers in candidate-record order rather than the plan's sorted order | **R4**: `provenance layer order: first difference at index 1` | R1 does not read provenance; R0 does not either. (Mirrors MAR-188's I16 mechanism on a different producer) |
| **I10** | Drop `bone_name` when copying a planned layer into `PsdLayerProvenance` | **R4**: `layer 'torso\|arm_l': bone='' expected 'torso'` | R4's ordered-identity clause passes — identity does not carry the bone. Only the full-tuple clause sees it. (MAR-188's I19, re-required because MAR-189 builds provenance from a different struct) |
| **I11** | Ignore `preserve`: never prune | **R5**: `slot 'ghost' must be removed by a preserve=false Missing layer; it is still present` | Every other case's plan preserves everything, so pruning is never exercised |
| **I12** | A failure after `CleanJournal` rolls the commit back | **R3 / CleanJournal**: `a failure after CleanJournal must still report success; the commit rolled back a completed reimport` | Fourteen of the sweep's fifteen arms *expect* a rollback. Only the per-step expectation table distinguishes the last one — which is precisely why the sweep is a table and not a uniform `!ok` |
| **I13** | Rollback does not delete the backups | **R3 / BackupTexture** (first rolling-back arm reached after a backup exists): `journal residue after rollback: [player_idle.matl.marrow-journal-…bak]` | R0–R2 never create a backup that survives; R1 succeeds and `CleanJournal` removes them on that path |
| **I14** | `apply_agent_review`'s digest comparison always reports equal | **A6**: `a PSD changed since review must be refused; the commit executed` | R-cases never go through the review queue; A5 approves an unchanged PSD, where equal is the correct answer |
| **I15** | `apply_agent_review` ignores `request.allowed` | **A6**: `approving a whitelist-rejected request must refuse; it committed` | A5's request is allowed, so the check's absence is invisible there |
| **I16** | The dry run stages into the target directory instead of a temp root | **A1**: `the target bundle changed during a dry run` (byte map diff naming the layers directory) | R-cases call the committer directly and never the op |
| **I17** | The staging root is not whitelist-checked | **A3**: `expected forbidden_path for a staging_root outside the whitelist; got ok` | A1's staging root is under `/tmp`, which is allowed |

### 7.1 Deliberately uninverted, by name

- **The failpoint seam itself.** It is test-only. Neutering it makes R3 vacuous
  rather than red, which is a property of every test seam and is why R0's ledger
  identity — which does not use the seam — exists.
- **The `Approve` button in `shell_agent_panel.cpp`.** Unobservable; §9.1.
- **`rollback_error` on the post-adoption unrecoverable corner.** Reaching it
  needs a *second* independent failure injected into `adopt_runtime_sources`
  during the rollback of the first. No such seam exists and MAR-189 does not add
  one. §9.2.
- **The journal manifest's contents.** R7 asserts it exists in flight and is gone
  after. Nothing replays it (§2.5), so a mutation of its *fields* changes no
  observable behaviour — a provable no-op of exactly MAR-188's I7 shape, and
  recorded rather than written.

### 7.2 The three degenerate gate shapes, checked against this design

- **Fails on correct code.** Two live traps here: `preserved_root` is an empty
  object (rule 4), and `runtime_revision()` legitimately moves on the success
  path (rule 6). Both are written into §6.1 rather than left to attention.
- **Passes on unchanged code.** **R2(a) is one**, and it is labelled a witness
  rather than quietly counted as coverage. MAR-188 shipped an instance of this
  class after naming it; naming it is evidently not sufficient, so the register
  above states an inversion for *every* other case and states the absence for
  this one.
- **Provably no-op mutation.** I9 was chosen over "read the provenance source
  from the result copy rather than the plan" precisely because the latter is
  MAR-188's I7 shape — two names aliasing one value with no read afterwards.

---

## 8. Facts to MEASURE in Task 0, not assume

| # | Claim | How |
|---|---|---|
| A1 | **F5 is real**: the staged `.matl` says `"image": "staged.png"` | Run MAR-188's planner into a temp root; `grep '"image"'` the staged atlas |
| A2 | `PsdImportResult::texture_path` for a staged plan is `<staging>/staged.png` | Print it from the same probe |
| A3 | The atlas `"name"` member is display-only | `grep` every read of `AtlasInfo::name` outside tests/samples |
| A4 | Baseline: clean all-target build warning count, `ctest -N`, `ctest`, `marrow_agent_dispatch_smoke` `[ OK ]` count, all smokes green | Measured **at the commit's actual parent**, per the story checklist |
| A5 | Registry is 66; the guards are 7/2/2; `agent_dispatch_smoke.cpp` bound line; `test_client.py` lines | Re-derive; do not copy from this document |
| A6 | `write_file_atomically` is binary-safe for a PNG payload | Round-trip the fixture PNG through it and `cmp` |
| A7 | A `rename` of a directory across the project dir and `/tmp` on this machine — same volume or `EXDEV`? | Probe. It decides whether §2.4's "placement copies" note is a nicety or a necessity |
| A8 | `EditTransaction::commit` on a provenance-only edit does not fail against the **old** skeleton document | Probe; if it does, `UpdateProvenance` must move above `AdoptRuntimeSources` and §2.7's window inverts |
| A9 | `marrow_psd_import_smoke` is absent from `ctest -N` | Run it |
| A10 | **F6**: `invert.sh` passes no object paths to `rebuild.sh` | Read both; confirm the empty `"$@"` |

---

## 9. Known limitations, stated rather than hidden

### 9.1 The `Approve` button cannot be tested, and this is one-directional

`CheckFrameBodies.cmake:36` lists `draw_agent_window` as app-only because the
headless smoke never sets `show_agent_panel`. `AGENTS.md` records the general
form: *"A UI-free helper cannot observe a deleted widget. Calling the function a
button calls asserts the handler, not the button."* So A5 proves
`apply_agent_review` commits; **nothing proves the button calls it.** This is the
same shape as MAR-181's `apply_pending_file_action` gap, and the same mitigation
applies: the coverage is one-directional and is recorded, not claimed.

Closing it means either drawing the agent panel in the headless smoke — which
changes the frame-body gate's allowlist and every scenario's window set — or a
frame-body-style script asserting the panel's `ImportOrPack` branch calls
`apply_agent_review`. The second is cheap and is offered as an **optional Task
7b**, not as a requirement, because it is a new gate in a chain that already
carries one.

### 9.2 One irreversible window remains, and it is named

A failure at `UpdateProvenance` rolls files back and re-adopts. If the re-adopt
fails, the session's runtime and its files disagree and no code path can repair
it. `rollback_error` names it; `ok` is false; the message tells the user to
reload the project. It is unreachable by test without a second injection seam
(§7.1) and is therefore reported as designed-for and unproven.

### 9.3 AC5's approval clause is read as editor-only

§2.10. If a reviewer reads AC5 as requiring MCP to approve, this becomes a
67th operation and the eleven guards, `agent_dispatch_smoke.cpp:42`,
`test_client.py:53,55` and the prose sites all move. Flagged before
implementation, not after.

### 9.4 The agent smoke runs against a **tracked** fixture, and this story writes files

`marrow.agent_dispatch_smoke` has `WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}`
(`CMakeLists.txt:1358`) and loads `assets/fixtures/player_idle.marrow`, whose
skeleton and atlas are tracked files. `agent_path_allowed` whitelists the
project directory. **A committing test pointed at that session would overwrite
tracked assets.** Every A-case therefore copies the project and its bundle into
a temp directory first and re-points the harness; **A7** asserts the tracked
bundle is byte-identical at the end of the run. This is a hazard the story
creates, not one it inherits, and it is the single most likely way MAR-189
damages the repository.

### 9.5 MCP tests are not automated here

`test_client.py` is not in CTest and needs a running editor with the agent
socket listening. M1–M3 are real assertions but they are run by hand, exactly as
every existing assertion in that file is. AC6's "MCP tests" is satisfied at that
standard and no higher.

### 9.6 Inherited and untouched

- Photoshop's real layer-record order is unverified and unverifiable in this
  environment (MAR-188 flagged it here). If the real order is the inverse, every
  group-bearing real PSD fails at `psd_import.cpp:734` — pre-existing, and
  MAR-189 does not change the parser.
- Slot names remain unstable across imports (document-global dedup census). A
  user will see `proposed_slot_name` differ from `current_slot_name` on an
  `Updated` row, and after a commit the *new* name is what provenance records.
- Save As still cannot rebase a path stored under an unparsed `preserved_root`
  key. Permanently open; not MAR-189's.

---

## 10. Risks for the implementer

1. **F5 is the one that will bite silently.** Every instinct says "the bytes
   match, therefore the copy is right". Write R1's `"image"` clause first.
2. **Do not let the sweep assume a uniform outcome.** `CleanJournal` succeeds.
   A uniform `!ok` sweep passes fourteen arms and is wrong about the fifteenth
   in the direction that destroys a completed reimport (I12).
3. **Do not test restoration with a success flag.** §6.2 exists because
   "`rolled_back == true`" is compatible with every byte being wrong.
4. **Scope the object deletion**: `find build/CMakeFiles -name '*.o' -delete`.
   The unscoped form rebuilds `libSDL3.a` at 96 bytes and hands you a red run
   with your sources provably untouched.
5. **Name your scratch directory after yourself, not after MAR-189.** The
   scratchpad is shared; MAR-188's implementer found another agent's build tree
   under the story-named path.
6. **`cmp` with absolute paths only.** A relative path follows whatever `cd` ran
   earlier, and a `cmp` of a file against itself always passes — which is how
   four assertions stayed neutered through two inversions in MAR-188.
7. **One commit. Korean subject and body. Both trailers, contiguous, no blank
   line between them.** Three consecutive stories have shipped without
   `Claude-Session:`.
