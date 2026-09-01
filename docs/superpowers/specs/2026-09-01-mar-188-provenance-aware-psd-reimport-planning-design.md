# MAR-188 Plan Provenance-Aware PSD Reimports — Design

Story: `.agents/tasks/prd-marrow-runtime.json` → `MAR-188`, `dependsOn: ["MAR-187"]`.
Baseline commit measured for this document: **`687ed4f`** (`docs: MAR-186 검토 지적 사항 반영`).
Working tree at the time of measurement: clean except the two untracked MAR-187
document files. **MAR-187 has landed no source; `git status --porcelain` shows
only `?? docs/superpowers/{plans,specs}/2026-09-01-mar-187-*`.**

Everything in §0.4 was measured by **building and running**, in an isolated
`git archive HEAD | tar -x` tree under the session scratchpad, never in the shared
worktree. The probe sources and their outputs are quoted verbatim, because three
of the five load-bearing claims in the incoming brief and in `AGENTS.md` turned
out to be wrong and the corrections are the reason this design is shaped the way
it is.

---

## 0. Where this story sits

### 0.1 The story in one sentence

Give a `.marrow` project a **typed, project-relative record of where its PSD came
from and which PSD layer became which runtime target**, make Save As carry that
record with it, and add a **UI-free planning function** that parses a candidate
PSD into a scratch directory and returns a deterministic added/updated/missing
diff — touching nothing on disk outside that scratch directory.

### 0.2 MAR-187 is a declared dependency and **none of it exists yet**

`dependsOn: ["MAR-187"]` is a *sequencing* dependency in the PRD, and this design
treats it as nothing more. Measured: MAR-187 has produced a design and a plan and
**no source**. Every MAR-187 artifact named below is **prospective** and is
described in the future tense.

The important finding is that **MAR-188 has no compile-time dependency on MAR-187
at all.** MAR-188 adds a project-data field, a rebase family, and a planning
function. It consumes no `ProblemsView`, no `SafeFixKind`, no `DiagnosticIssue`,
no `DiagnosticPanel`. It draws no window, so it amends neither frame body.

What MAR-187 will nonetheless deliver first, and why it matters, is purely a
**merge surface**. It is larger than the shared-file list alone, because MAR-187
is also adding **at least four new files of its own**. All of this is
**prospective**: MAR-187 is mid-implementation and its file set may still change,
which is why Task 0.9 re-derives it rather than trusting this table.

**Files MAR-187 adds (new, no collision — listed so the implementer recognises
them and does not mistake them for their own or for someone else's stray work):**
`include/marrow/editor/safe_fix.hpp`, `src/editor/safe_fix.cpp`,
`include/marrow/editor/problems_model.hpp`, `src/editor/problems_model.cpp`, plus
its shell pair `src/editor/shell_problems.{hpp,cpp}`, each with its own
`CMakeLists.txt` source line.

Note where MAR-187's typed overlay record key landed: **`safe_fix.hpp`, not
`diagnostics.hpp`.** MAR-187 was directed to amend MAR-186's header for it and
chose the better route — a new header of its own — which avoids a downstream story
editing an upstream story's header. `include/marrow/editor/diagnostics.hpp` is
therefore expected to stay **unmodified**, and E3 stands.

**Files both stories edit:**

| File | MAR-187's prospective change | MAR-188's change | Collision |
| --- | --- | --- | --- |
| `CMakeLists.txt` | source lines into `add_library(marrow_editor …)` (`:499-521`) for `safe_fix.cpp` and `problems_model.cpp`, and into `add_executable(marrow_editor_shell …)` for `shell_problems.cpp` | **one** line into `add_library(marrow_editor …)` for `src/editor/psd_reimport_plan.cpp` | Same hunk, trivially resolvable; all are additive source lines |
| `src/samples/editor_project_smoke.cpp` | registers `validate_mar187_problems_view` / `validate_mar187_safe_fixes` after MAR-186's call at **`:17948-17951`** | registers `validate_mar188_psd_provenance` after those | Same hunk. MAR-188 appends **after** MAR-187's two registrations |
| `AGENTS.md` | a `## MAR-187 …` section and a `## Current Validation` bullet | a `## MAR-188 …` section and a `## Current Validation` bullet | Adjacent, not overlapping |
| `.agents/tasks/prd-marrow-runtime.json` | flips MAR-187 to `done` | flips MAR-188 to `done` | Different objects |

**If MAR-188 is implemented before MAR-187 lands, nothing breaks.** MAR-188
includes none of MAR-187's headers and calls none of its functions; the only cost
is that MAR-187's author inherits the merge instead of MAR-188's. This is stated
so the implementer does not block on a story that cannot block them.

### 0.3 Errors found in the incoming brief and in the governing documents

Recorded first, because two of them change the shape of the story.

| # | Source | Claim | Measured |
| --- | --- | --- | --- |
| **E1** | The team lead's brief | "PSD provenance rebasing needs an `import_sources` structure that exists **nowhere but a doc comment**" — implying the JSON key does not survive a `.marrow` round trip today | **Half wrong, and the wrong half is decisive.** The *typed structure* is indeed absent. But `$.editor.import_sources.psd` **already round-trips today, verbatim, with zero code**, through `preserved_root`. Measured — see §0.4 P1. The consequence is severe: **an AC1 "provenance round-trip" case written as a text search over `serialize_project()` passes on completely unmodified code.** See §0.3.1 for the class this belongs to. §2.2 and §6.2 P2 are written around it |
| **E2** | The brief | "the hand-edited sites are `agent_dispatch_smoke.cpp:39`" | **`:42`.** `constexpr std::array<OperationExpectation, 66> kExpectedOperations{{` is at `src/samples/agent_dispatch_smoke.cpp:42` at HEAD, verified against `git show HEAD:` and not only the worktree. `:39` is `bool dry_run_supported;`. Non-blocking here (MAR-188 adds no operation, §2.11), but recorded because the same number is copied forward story to story |
| **E3** | The brief | MAR-187 "will move `diagnostics.hpp`" | **It will not, and the reason has since been confirmed upstream.** `include/marrow/editor/diagnostics.hpp` exists at HEAD (MAR-186, 9740 bytes), carries no `OverlayRecordKey`, and MAR-187's design §3 leaves it where it is. MAR-187 was directed to amend that header for a typed overlay record key and instead created **its own** `safe_fix.{hpp,cpp}` for it — a downstream story declining to edit an upstream story's header. It also adds `problems_model.{hpp,cpp}` and `shell_problems.{hpp,cpp}` and amends both frame bodies. Irrelevant to MAR-188 as a compile dependency either way, but it **enlarges the merge surface**, which §0.2 now lists and Task 0.9 re-derives |
| **E4** | `AGENTS.md:2337` (MAR-180) | "`grep -rn "import_sources" src/ include/ assets/` returns nothing" | **Returns one hit today:** `include/marrow/editor/project.hpp:1053`, the doc comment MAR-180 itself added in the same commit. The sentence was true when measured and false by the time it was written down. Harmless; recorded because §1.4's re-derivation of MAR-180's debt has to start from what is true now |
| **E5** | `AGENTS.md:2348` (MAR-180's Task 0 record) | the five serialized path families are at `project.cpp:4501,4521,4725,4729,4745`, `$.atlas_packs` at `:4896-4901`, `save_project`'s stream at `:7866` | **All six citations have drifted and none still names its construct.** Re-derived in §1.2. `:4501` is now a transform-constraint bone push; `:4725` is a slot-color object check; `:4896-4901` is the tail of `find_source_entry_by_id`. Six stale anchors are exactly the class MAR-185's D25 named, and they are why Task 0.1 is a **blocking** gate rather than a formality |
| **E6** | The brief | "eleven `!= 66U` guards across `shell_smoke_graph.cpp`, `shell_smoke_constraints.cpp` **and** `shell_smoke_timeline.cpp`" | **Correct, and the split is 7 / 2 / 2.** Verified: `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`; `shell_smoke_constraints.cpp:147,676`; `shell_smoke_timeline.cpp:3697,4389`. Recorded because MAR-180's record says **ten** and names only `shell_smoke_timeline.cpp:3697` — `:4389` was added afterwards |
| **E7** | `docs/root1/fixtures.md:183` and the shape of the two checked-in PSDs | the fixture pair is what a reimport story tests against | **The pair covers exactly one classification case.** Byte-diff: the two files differ in **4 bytes**, all in `body`'s bounding box. Measured layer sets are **identical** — `shadow` (no group), `arm_l` and `body` (group `torso`). There is no added layer, no removed layer, no renamed layer, no group move, no duplicate name, and **no group nested deeper than one level** anywhere in the repository. Every AC4 and AC6 classification case needs an input that does not exist, and no PSD generator exists either. This is the story's largest single piece of work — §2.8 |
| **E8** | This design, §0.3.1 | P1 "earns its place only through inversion I3" | **Wrong, and self-inflicted in the section that names the defect.** §7 and plan §B both attribute I3 to **P4**. Task 0 measured P1 green on the pristine tree and no inversion of the nineteen turns it red — so §0.3.1 defined "a gate that passes on unchanged code" and then shipped one. P1 is kept and **relabelled a compatibility witness**; §0.3.1 and §6.2 P1 are corrected. Recorded here because the class's first *self-inflicted* instance is a stronger entry than any invented example |
| **E9** | This design, §A.3 of the plan, and §9 | the dedup at `psd_import.cpp:814-826` is `assign_slot_names` | **`assign_slot_names` does not exist.** `git grep assign_slot_names HEAD` returns nothing repo-wide; the enclosing function is `parse_psd_document` (`psd_import.cpp:625`). The lines and the described behaviour are correct — only the symbol was invented. Blocking under the re-anchor gate's own "cite the symbol as primary anchor" rule, and the exact shape by which MAR-185's D23 let a third parser escape a name-based sweep. Corrected in all three sites |
| **E10** | The team lead's brief | MAR-187 "will move `diagnostics.hpp`" was wrong and E3 stands: MAR-187 put `OverlayRecordKey` in its own new `safe_fix.hpp`, leaving `diagnostics.hpp` unmodified | **E3 is falsified at `bb3652f`.** `diagnostics.hpp` was modified **+71**, and `OverlayRecordKey` is at **`diagnostics.hpp:163`** — MAR-187 amended MAR-186's header after all. E3 was a *prediction* about a then-unlanded story, not a measurement, so it is corrected rather than preserved. **No effect on MAR-188**, which includes neither header. The lead measured this independently and sent the correction |
| **E11** | The team lead's brief | all six MAR-180 path-family citations are stale "(drift +288..+303)" | **The staleness re-verifies; the figure was fabricated.** The range +288..+303 appears in **neither** of this story's documents. Measured drifts are **+59 to +512** (4501→5013 +512, 4521→5017 +496, 4725→5033 +308, 4729→4788 +59, 4745→4810 +65). Recorded at the lead's own instruction, because a specific-looking invented number is more dangerous than a vague one: it reads as measured. The plan says only "anchors drift by hundreds of lines", which is the honest form |
| **E12** | The team lead's brief | "your two documents are the only untracked files" | **There was a third**, `build-specrev187/`, another agent's build directory. During Task 0 the shared worktree also acquired ` M src/editor/safe_fix.cpp` and ` M src/editor/shell_problems.cpp` — the MAR-187 reviewer's inversions in flight, which mutate and restore. `HEAD` never moved. Recorded because it is the reason this story builds from an isolated tree for its whole duration, never from the shared worktree |

### 0.3.1 E1's class, named: **a gate that passes on unchanged code**

E1 is the mirror image of a defect this chain has hit three times and has a name
for — **a gate that fails on correct code** (MAR-186's D2 and its incoming
three-vs-four parser-rule error; MAR-187's two case specifications that would have
failed on correct code). E1 is the other direction: **a gate that passes on
unchanged code.**

The pair, stated so future planners check both directions:

| Direction | Symptom | How it is found |
| --- | --- | --- |
| **Fails on correct code** | The case is red before *and* after a correct implementation | Watch it fail, then watch it pass. It gets investigated, because red demands attention |
| **Passes on unchanged code** | The case is green before the implementation exists | **Only** by running the case against the pristine tree first, or by an inversion. Nothing else surfaces it |

The second is the more dangerous of the two, precisely because it is quiet: a
failing gate gets investigated and a passing one does not. A red case is a
question; a green case is an answer nobody asked for.

MAR-188 is unusually exposed to it because the storage layer **pre-arms** it —
`preserved_root` makes the file-level round trip work before a line is written. The
countermeasure is structural, not vigilance: **P2 asserts the typed struct, never
a substring of `serialize_project()`**, and Task 1.2 requires each case's
pre-implementation behaviour to be *recorded*.

**This section shipped an instance of the class it defines, and Task 0 caught it.**
An earlier draft of this paragraph claimed P1 "earns its place only through
inversion I3". It does not: §7 and the plan's §B both attribute **I3 to P4**
("P1-P3, P5-P8 all pass. Only P4 looks at a project whose provenance was
cleared"). Task 0 measured P1 **green on the pristine tree** — `player_idle.marrow`
serialises to 6111 bytes containing no `import_sources` — and no inversion in the
register of nineteen turns it red. So P1 was, verbatim, a gate that passes on
unchanged code, written into the section that names the defect.

P1 is **kept and relabelled**: it is a **compatibility witness**, not a gate. Its
`cmp` against the Task 0 baseline proves that a project which never had provenance
serialises byte-identically after the story — a real and worthwhile claim about
backward compatibility, and the only case that makes it. What it is not is
evidence that any MAR-188 code works. **P1 has no story-owned inversion, by
design, and that is recorded rather than repaired**; manufacturing one would mean
inventing a mutation nobody would make. A future reader must not record "P1 green"
as vindication of anything.

The general rule this produced: **a case that is green before the implementation
exists is a witness, not a gate, and must be labelled as one** — with the
inversion that would catch its subject named explicitly, or its absence stated.

### 0.4 What was measured, by building and running

Isolated tree: `git archive HEAD | tar -x -C <scratch>/t188`, then
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` and
`cmake --build build --target marrow_editor -j8` → exit 0. Two standalone probes
were compiled against `build/libmarrow_editor.a build/libmarrow_runtime.a
build/libmarrow_timeline_model.a -lz`. **No command was run against the shared
worktree and nothing was staged.**

**P1 — `$.editor.import_sources.psd` already survives a round trip.** The probe
injects the key into `player_idle.marrow`'s parsed document, calls
`load_project(document)`, then `serialize_project`:

```
P1 import_sources survives load+serialize: YES
P1 art/hero.psd survives: YES
P1 group_path survives: YES
```

The chain is three lines and there is no unknown-key rejection anywhere in the
loader: `project.preserved_root = document.root;` (`project.cpp:7953`) stores the
**whole** tree; `build_project_value` seeds the editor block from it —
`Value::Object editor_object = preserved_object_member(project.preserved_root, "editor");`
(`project.cpp:5023`) — and re-emits it wholesale at
`root["editor"] = make_object_value(std::move(editor_object));` (`project.cpp:5089`),
overlaying only the known keys. The root-level analogue already has a shipped test
(`editor_project_smoke.cpp:6293-6310`, the `mar172_probe`).

**P2 — and a Save As leaves it stale.** The same probe rebases to a sibling
directory:

```
P2 skeleton after rebase: <abs>/assets/fixtures/player_idle.mskl
P2 art/hero.psd UNCHANGED after rebase (stale): YES
```

`rebase_project_paths` starts with `ProjectData result = project;`
(`project.cpp:7765`), which copies `preserved_root` untouched, and its lambda
walks only the five struct families. So today a cross-directory Save As writes the
**old** project-relative PSD path into the **new** directory, where it resolves to
a different file or to nothing. This is precisely the failure the doc comment at
`project.hpp:1052-1054` predicts, and it is MAR-188's AC2 detector.

**P2b — the relative arm needs a parent-directory destination.** Rebasing the same
project into the fixture directory's **parent**:

```
P2b skeleton after parent-dir rebase: fixtures/player_idle.mskl
P2b export_directory: fixtures/exports
```

Into a **sibling**, the same references come back **absolute** (P2), because
`make_project_relative_path` (`project.cpp:1375-1401`) returns the absolute path
whenever the relative form would need `../`. Both arms are observable; the
*relative* arm is the one that proves identity preservation, and it is only
reachable by saving into an ancestor directory. §6.2 P5/P7 author both.

**P3 — a created project has an empty `preserved_root`.**

```
P3 create_minimal_project preserved_root empty: YES
```

So the accidental passthrough in P1 holds **only** for projects loaded from a
file. A project built by `create_minimal_project`
(`project.hpp:1023`, options struct `project.hpp:1007-1016`) has nothing to
preserve. A typed field is therefore not an optimisation; it is the only way the
data exists at all for a new project.

**"Empty" means an empty OBJECT, not null — measured in Task 0**, which the
`YES` above does not distinguish and which a case would get wrong:

```
A4 create_minimal_project preserved_root empty: YES
A4 preserved_root type is_object=1 is_null=0
```

A case shaped as `preserved_root.is_null()` therefore **fails on correct code** —
the other defect class of §0.3.1, and its fourth instance in this chain. Test
`is_object() && as_object().empty()`, which is also the shape
`build_project_value` itself uses at `project.cpp:5007-5009`.

**P4 — the PSD fixtures' exact identities**, from a probe that runs
`import_psd_to_runtime_bundle` over both checked-in files:

```
--- assets/fixtures/psd_import_sample.psd
  layers=3 bones=2
  layer name='shadow' group=[]       slot='shadow' att='shadow' bone='root'  img='shadow.png' box=(18,40,14,8)
  layer name='arm_l'  group=[torso,] slot='arm_l'  att='arm_l'  bone='torso' img='arm_l.png'  box=(4,20,12,8)
  layer name='body'   group=[torso,] slot='body'   att='body'   bone='torso' img='body.png'   box=(16,12,20,24)
  bone 'root'  parent='<none>' x=0 y=0
  bone 'torso' parent='root'   x=4 y=12
--- assets/fixtures/psd_import_sample_reimport.psd
  ... identical, except: layer 'body' box=(20,14,20,24), bone 'torso' y=14
```

**P5 — the parse-failure shape, and that it writes nothing.**

```
--- parse failure: PSD import failed for '<tmp>/bad.psd': PSD files must begin with the 8BPS signature.
    skeleton written? NO
```

`import_psd_to_runtime_bundle` returns a `PsdImportResult` whose `error` is
engaged; it never throws and never returns a status code
(`psd_import.hpp:59-61`, `psd_import.cpp:1190-1195`).

---

## 1. The measured gap

### 1.1 PSD import leaves no trace in any project, ever

`import_psd_to_runtime_bundle` (`include/marrow/editor/psd_import.hpp:69`,
definition `src/editor/psd_import.cpp:1197`) writes four artifacts — per-layer
PNGs, an atlas PNG, an atlas `.matl`, a skeleton `.mskl` — and returns a
`PsdImportResult` (`psd_import.hpp:48-62`) that **nobody stores**.

- `grep -rn "psd\|Psd\|PSD" src/editor/project.cpp` → **no matches**. The project
  layer has never heard of PSD.
- `import_psd_to_runtime_bundle` has exactly **two callers repo-wide**, both in
  `src/samples/psd_import_smoke.cpp` (`:351`, `:366`). There is **no production
  caller**: the `import.psd_layers` agent operation
  (`agent_dispatch.cpp:97`, handler `agent_handlers_management.cpp:95,118-126`)
  validates a path and enqueues a review; it never reaches the importer.
- `build_imported_atlas` (`psd_import.cpp:1099-1133`) constructs a throwaway
  `ProjectData` (`:1102-1103`) purely to satisfy the atlas packer's signature and
  discards it on return. It is never added to `atlas_pack_definitions`.

So "reimport" today means: run the importer again over the same output paths and
let `build_skeleton_document` (`psd_import.cpp:1009-1052`) merge. That merge
**preserves `animations` and `skeleton.name`** (`:1030-1034`, `:1043-1048`),
**replaces `bones` and `slots` wholesale** (`:1039-1040`) and **erases `skins`**
(`:1041`). There is no diff, no classification, and no way to ask what would
change before it changes.

### 1.2 The five path families, re-derived

MAR-180's citations have all drifted (E5). Re-derived at `687ed4f`:

| # | Struct field | JSON | Rebased at | Serialized at |
| --- | --- | --- | --- | --- |
| 1 | `runtime_assets.skeleton_path` | `$.runtime.skeleton` | `project.cpp:7782` | `project.cpp:5013-5014` |
| 2 | `runtime_assets.atlas_paths[]` | `$.runtime.atlases[]` | `project.cpp:7783-7785` | `project.cpp:5017-5020` |
| 3 | `editor_metadata.export_directory` | `$.editor.export_directory` | `project.cpp:7786-7787` | `project.cpp:5033-5034` |
| 4 | `atlas_pack_definitions[].atlas_path` | `$.atlas_packs[].atlas` | `project.cpp:7791` | `project.cpp:4788-4790` |
| 5 | `atlas_pack_definitions[].sprites[].image_path` | `$.atlas_packs[].sprites[].image` | `project.cpp:7792-7794` | `project.cpp:4810` |

Stable anchors that will not drift the way line numbers do, and which every
citation in this pair is expected to be re-resolved against:
`build_project_value` (`project.cpp:5006`),
`build_atlas_pack_definitions_value` (`project.cpp:4782`),
`parse_editor_metadata` (`project.cpp:877`),
`rebase_project_paths` (`project.cpp:7762`),
`save_project` (`project.cpp:8317`, rebase call `:8327`),
`validate_project_for_save` (`project.cpp:5825`, inside the anonymous namespace
that opens at **`project.cpp:24`** and closes at `:6862`).

Family 5 matters as precedent: it is already a **nested** rebase, one path per
element of a vector inside a vector. MAR-188's family is structurally simpler.

### 1.3 `rebase_project_paths` is a fixed-length call list, and nothing polices it

The body (`project.cpp:7762-7797`) is five assignments and two loops. Adding a
sixth family means adding statements to a list. Per `AGENTS.md`'s
"Adding a value to an enum" section: **`-Wswitch` is on by default and finds every
exhaustive switch; nothing finds a dropped entry from a fixed-length list.** There
is no switch here at all, so the compiler contributes exactly nothing. Worse,
MAR-186's `static_assert` precedent is on record as a proven tautology when the
constant is initialised from the same literal.

The only detector for "one of the two new path fields was not rebased" is a test
that asserts **both fields by name**. §6.2 P5 does, and §7 I5/I6 invert them
**separately** — one inversion per field — because a single inversion cannot
distinguish "the family was added" from "the family was added correctly".

### 1.4 MAR-180's deferred criterion, quoted, and what MAR-188 does to it

`AGENTS.md:2337-2345`, verbatim:

> **One acceptance criterion cannot be satisfied as written.** The story requires
> Save As to rebase a "PSD provenance path". No such field exists:
> `grep -rn "import_sources" src/ include/ assets/` returns nothing, and
> `psd_path` lives only as a transient `PsdImportOptions` field, never persisted.
> `.marrow.editor.import_sources.psd` is scheduled for **MAR-188**
> (`docs/root1/editing-gap-analysis.md:430`). MAR-180 instead makes
> `rebase_project_paths` the single documented place a new project-relative field
> is registered, and its doc comment names MAR-188. Relatedly, paths stored inside
> `preserved_root` are round-tripped opaquely and cannot be reached from there, so
> "rebases every relative path" is satisfiable only for the five known families.

`docs/root1/editing-gap-analysis.md:430` is verified to be the MAR-188 row of the
story table, and it says the same thing in Korean, adding the sequencing:
"이 field를 추가할 때 `rebase_project_paths`(`project.cpp`)에 여섯 번째 family로
등록해야 Save As가 따라간다 — MAR-180이 그 자리를 doc comment로 표시해 뒀다."

**What MAR-188 discharges, exactly.**

1. **The first clause — fully discharged.** AC1 creates
   `$.editor.import_sources.psd` as a typed, parsed, validated project field
   (§2.1, §2.2). AC2 registers it in `rebase_project_paths` as the sixth family
   (§2.5). The proof obligation is AC6's "provenance round-trip" and
   "relative-path rebasing", discharged by §6.2 P2/P5/P6/P7 with inversions I2-I8.
   After MAR-188 the sentence "no such field exists" is false and the criterion
   MAR-180 deferred is executable.

2. **A precision MAR-180 could not state, because it had not measured it.** The
   criterion was unsatisfiable **not** because the data could not be stored — it
   already round-trips (§0.4 P1) — but because it was stored **opaquely**.
   `rebase_project_paths` copies `preserved_root` verbatim and cannot reach inside
   it. MAR-188 discharges the criterion precisely by **moving the key out of the
   opaque copy into a typed field**, which is also why parsing without emitting
   would be a regression rather than a no-op (§2.3).

3. **The last clause — NOT discharged, and to be recorded as PERMANENTLY OPEN.**
   MAR-180 also wrote: *"paths stored inside `preserved_root` are round-tripped
   opaquely and cannot be reached from there, so 'rebases every relative path' is
   satisfiable only for the five known families."* MAR-188 makes that **six known
   families** and **seven rebased fields**. It does not make `preserved_root`
   reachable, and nothing short of a schema-strict loader could: `preserved_root`
   is *defined* as "whatever this code does not understand", so a rule quantified
   over "every relative path" is quantified over a set the program cannot
   enumerate. Any path a future document carries under an unparsed key remains
   unrebased and silently stale on Save As.

   **This is not a MAR-188 deliverable and must not be written up as one.** The
   generalised criterion is closed only by a decision nobody has taken — making
   the loader schema-strict, or declaring unparsed paths unsupported — and each
   such decision is a story in its own right with its own compatibility cost.
   Task 9 records it in `AGENTS.md` as **permanently open with that reasoning**,
   stated plainly rather than quietly rescoped into "six families, done". A
   criterion that cannot be met should say so.

**Net:** MAR-188 discharges MAR-180's deferred criterion in full **as that
criterion is worded** ("Save As rebases the PSD provenance path"), and leaves
MAR-180's own *generalisation* of it ("rebases every relative path") open by
exactly the amount `preserved_root` is opaque.

### 1.5 Identity today is derived, unstable, and unusable as a key

`parse_psd_document`'s document-global slot dedup (`psd_import.cpp:814-826`) computes a slot name from a
**document-global** duplicate census:

```cpp
std::string base_name = duplicate_layer_names[layer.original_name] > 1U
    ? join_path(layer.group_path, layer.original_name)
    : layer.original_name;
layer.slot_name = make_unique_name(base_name, &used_slot_names);
layer.attachment_name = layer.slot_name;
```

So a layer named `body` becomes slot `body` while it is unique and slot
`torso/body` the moment an **unrelated** second `body` appears anywhere in the
document — and `make_unique_name` (`:593-609`) then resolves any remaining
collision positionally with `_2`, `_3`. **A reimport that adds one layer can
therefore rename an untouched slot.** Any provenance keyed on `slot_name` is
keyed on something a future import can change out from under it.

`PsdImportedLayer` (`psd_import.hpp:10-21`) keeps the raw pre-dedup name in
`name` (assigned from `layer.original_name`, `psd_import.cpp:1144`) and the
ancestor folder names in `group_path` (`:1145`). **That pair is the only stable
identity the importer exposes**, and it is what AC1's "PSD group/name identities"
must mean.

Two further facts the identity design has to absorb:

- **Slot ≡ attachment ≡ atlas region, one string.** `build_skeleton_document`
  erases `$.skins` (`psd_import.cpp:1041`), so the runtime synthesises a default
  skin — `parse_skins` (`src/runtime/skeleton_parse.cpp:4926-4936`) →
  `build_default_skin` (`:4804-4822`) fabricates one `Region` per slot whose
  `name` and `region_name` both equal `slots[i].setup_attachment`. There is no
  independent attachment identity to key on.
- **Group nesting is real in the parser and lossy at the API boundary.**
  `parse_psd_document` maintains an `active_groups` stack (`psd_import.cpp:729`),
  a `GroupRecord::parent_group_index` (`:743`) and a cumulative `GroupRecord::path`
  (`:746-749`), supporting arbitrary depth. `populate_result_metadata`
  (`:1142-1158`) then drops `parent_group_index` and never exposes the group list;
  a consumer sees only `PsdImportedLayer::group_path` and
  `PsdBoneHint::parent_name`. `group_path` is sufficient for MAR-188 and it is the
  only thing MAR-188 will read.

### 1.6 The importer is destructive before it is complete

`write_imported_layers` (`psd_import.cpp:1068-1097`) calls
`std::filesystem::remove_all(extracted_layers_directory)` at **`:1076`**, before a
single PNG, the atlas or the skeleton is written. Its only guard is
`is_safe_generated_directory` (`:1054-1066`), which rejects empty, root, `.` and
`..` and **nothing else**.

And `effective_existing_skeleton_path` (`psd_import.cpp:923-928`) makes
`existing_skeleton_path` fall back to `skeleton_output_path` when unset.

These two facts are why AC3 ("entirely in staging … without modifying … target
files") is a **structural** requirement in this design and not a discipline: a
planner that lets a caller supply output paths is one typo away from
`remove_all`-ing a project's real layer directory. §2.6.

### 1.7 The PSD smoke has no case framework and is not in CTest

`src/samples/psd_import_smoke.cpp` (385 lines) is a straight-line `main`
(`:323-385`) with two validators, `validate_initial_import` (`:153-264`) and
`validate_reimport` (`:266-319`), 12 `expect`/`expect_near` guard sites, and an
early `return 1` on the first failure. `expect` (`:27-33`) prints the message and
nothing else — no case name, no file, no line.

`grep -n "marrow_psd_import_smoke" CMakeLists.txt` returns exactly `:850` and
`:854`. **There is no `add_test`.** `ctest -N` will still report 22 after this
story. The binary is invoked by hand, per `AGENTS.md:174`:

```
./build/marrow_psd_import_smoke assets/fixtures/psd_import_sample.psd assets/fixtures/psd_import_sample_reimport.psd
```

Its two default paths (`:21-22`) are **relative**, so an argument-less run works
only from the repo root.

`src/samples/editor_project_smoke.cpp` is the opposite: a large registry of
`bool validate_*(...)` functions called from `main`'s marker-gated `else`, MAR-186
last at `:17948-17951`.

---

## 2. Decisions

### 2.1 The provenance vocabulary

Three new structs in `include/marrow/editor/project.hpp`, immediately above
`ProjectMetadata` (`project.hpp:575`), and one new member inside it.

```cpp
/// @brief One PSD layer's stable identity and the runtime targets it produced.
struct PsdLayerProvenance {
    std::vector<std::string> group_path;  ///< Exact ancestor folder names, outermost first.
    std::string layer_name;               ///< Exact PSD layer name, before slot de-duplication.
    std::string slot_name;
    std::string attachment_name;
    std::string bone_name;
    std::string image_file;               ///< File NAME under `layers_directory`. Never a path.
};

/// @brief Where a project's art came from, and what each layer became.
struct PsdImportProvenance {
    std::filesystem::path source_path;       ///< Project-relative `.psd`.
    std::filesystem::path layers_directory;  ///< Project-relative extracted-layer directory.
    std::vector<PsdLayerProvenance> layers;
};

/// @brief Optional per-format import provenance. Absent in every pre-MAR-188 project.
struct ProjectImportSources {
    std::optional<PsdImportProvenance> psd;
};
```

and in `ProjectMetadata` (`project.hpp:575-583`), after `timeline`:

```cpp
    std::optional<ProjectImportSources> import_sources;
```

`ProjectMetadata` is the correct home: it is what `$.editor` deserialises into,
and `export_directory` — the other `$.editor` path family — already lives there.

The JSON shape:

```json
"editor": {
  "import_sources": {
    "psd": {
      "path": "art/hero.psd",
      "layers_directory": "art/hero_layers",
      "layers": [
        { "group_path": ["torso"], "layer": "body",
          "slot": "body", "attachment": "body", "bone": "torso",
          "image": "body.png" }
      ]
    }
  }
}
```

### 2.2 `image_file` is a bare file name, not a path — and this is a scope decision

**Rejected alternative:** store `PsdImportedLayer::extracted_image_path` per layer
as a sixth *and seventh* project-relative path family, mirroring
`atlas_pack_definitions[].sprites[].image_path`.

**Chosen:** store the bare file name and resolve it against `layers_directory`.

Why, measured: `write_imported_layers` (`psd_import.cpp:1080-1082`) computes every
layer's path as

```cpp
layer.extracted_image_path =
    (extracted_layers_directory / (file_stem + ".png")).lexically_normal();
```

so a layer image is **always** a direct child of `extracted_layers_directory`. A
stored path could therefore only ever restate the directory, once per layer.

The benefit is not brevity, it is **falsifiability**. `project.hpp:1037-1039`
warns that a *partial* rebase is worse than none. With two path fields, "the
family was rebased" is a two-clause assertion that two inversions can separate
(I5, I6). With N+2 path fields, a dropped per-layer rebase in a 40-layer project
is a needle a full-identity assertion still catches but that no small inversion
isolates — and the fixed-length-list blindness of §1.3 applies to *every one of
them*. Reducing the rebase surface to two fields makes the compiler-blind class
small enough to enumerate by hand.

The cost, stated: a hypothetical layer image outside `layers_directory` cannot be
represented. The importer cannot produce one. If MAR-189 or MAR-190 needs that, it
promotes the field and inherits a nested rebase family with the precedent already
in the tree at `project.cpp:7792-7794`.

**A load-time rejection enforces the invariant** rather than trusting it:
`image` must be non-empty and must equal its own `std::filesystem::path(...).filename()`
— i.e. contain no directory separator and no `..`. §2.4.

### 2.3 Parsing without emitting is a regression, not a no-op

This is the trap §0.4 P1 sets. The chain today is: unknown key → `preserved_root`
→ re-emitted from the preserved editor object at `project.cpp:5023`/`:5089`. Once
`parse_editor_metadata` reads `import_sources` into a typed field, **the preserved
copy is still there and still wins unless it is overwritten.** The consequences
run in both directions:

- **Parse but do not emit** → every in-memory edit to the field is silently
  discarded on save, and the *stale* preserved copy is written instead. Save As is
  then worse than before the story, because the rebased typed value is thrown away.
- **Emit but never erase** → clearing the provenance (`import_sources = nullopt`)
  leaves the old block on disk forever.

So serialization must do **both**, following the `root.erase(...)` idiom already in
the file at `project.cpp:5192` (`constraint_edits`), `:5197` (`parameter_model`)
and `:5203` (`atlas_packs`):

```cpp
    if (project.editor_metadata.import_sources.has_value()) {
        editor_object["import_sources"] =
            build_import_sources_value(*project.editor_metadata.import_sources);
    } else {
        editor_object.erase("import_sources");
    }
```

placed inside `build_project_value` between the last `editor_object[...]`
assignment and `root["editor"] = …` (`project.cpp:5089`). The same erase-on-empty
rule applies one level down: an `ProjectImportSources` whose `psd` is `nullopt`
serialises as an **absent** `import_sources`, not as `{}`.

§6.2 P3 and P4 are the two cases that make this observable, and they are the only
cases in the story that can distinguish correct behaviour from the accidental
passthrough that already exists.

### 2.4 Parse rules and rejection messages

Shape copied from `editor.viewport.onion_skin` (`project.cpp:972-999`), which is
the exact structural analogue: an optional object two levels under `$.editor`.
Register 1 messages (lowercase, unpunctuated, no embedded path — the path is
`validation_error`'s separate argument, concatenated at `src/runtime/json.cpp:962`).

| Condition | JSON path | Message |
| --- | --- | --- |
| `import_sources` not an object | `$.editor.import_sources` | *(from `require_type`)* `expected object but found …` |
| `psd` not an object | `$.editor.import_sources.psd` | *(from `require_type`)* |
| `path` missing or not a string | `$.editor.import_sources.psd.path` | *(from `require_member`)* `missing required member` |
| `path` empty | `$.editor.import_sources.psd.path` | `psd source paths must not be empty` |
| `layers_directory` empty | `$.editor.import_sources.psd.layers_directory` | `psd layer directories must not be empty` |
| `layers` present but not an array | `$.editor.import_sources.psd.layers` | *(from `require_type`)* |
| `group_path` element not a string | `$.editor.import_sources.psd.layers[i].group_path[j]` | *(from `require_type`)* |
| `layer` empty | `…layers[i].layer` | `psd layer names must not be empty` |
| `image` empty | `…layers[i].image` | `psd layer image names must not be empty` |
| `image` contains a separator | `…layers[i].image` | `psd layer image names must not contain a directory separator` |
| two entries share an identity | `…layers[i]` | `psd layer identities must be unique` |

`layers` **absent** is legal and means an empty vector: a provenance record that
names a source but has no mapping yet is meaningful (a first import that has not
been committed). `layers` **empty** is likewise legal.

Duplicate identity is a **load** rejection, not a planning one. Precedent:
`"ids must be unique"` (`project.cpp:469`), `"atlas pack output paths must be
unique"` (`:1348`). Rationale in §2.9.

Save-time validation adds nothing. `validate_project_for_save`
(`project.cpp:5825`) exists to catch a project mutated in memory past what the
loader gated, and MAR-172's comment at `:5879-5882` records that intent. There is
a real argument for re-checking the `image` invariant there. **Deliberately not
done**, and recorded as a limitation (§9): every write path to the field in this
story is `make_psd_provenance`, which constructs `image_file` from
`std::filesystem::path::filename()` and structurally cannot produce a separator.
Adding a save-time check would be a rule with no reachable failing input, which
`AGENTS.md` calls "correct by inspection, unreachable by test" (D21).

### 2.5 The sixth rebase family

Inside `rebase_project_paths` (`project.cpp:7762-7797`), after the
`atlas_pack_definitions` loop and before `return result;`:

```cpp
    // The sixth family (MAR-188). Two paths, both project-relative; the per-layer
    // `image_file` is a bare file name and is deliberately not a path (design 2.2).
    if (result.editor_metadata.import_sources.has_value() &&
        result.editor_metadata.import_sources->psd.has_value()) {
        const PsdImportProvenance& source =
            *project.editor_metadata.import_sources->psd;
        PsdImportProvenance& target = *result.editor_metadata.import_sources->psd;
        target.source_path = rebase(source.source_path);
        target.layers_directory = rebase(source.layers_directory);
    }
```

Note it reads from `project` and writes to `result`, matching the existing five —
the lambda captures `project` by reference precisely because `result.source_path`
was already overwritten at `:7766`.

Three prose amendments ship with it, and each is a place where a stale comment
would mislead the next reader:

1. `project.hpp:1033` — "Five families are rebased, and they are exactly the five
   that are serialized from struct fields" → six, with the new family named.
2. `project.hpp:1052-1054` — the "When MAR-188 adds `$.editor.import_sources.psd`
   as a real project-relative field, it must be registered in this function"
   paragraph → rewritten to say it **is** registered, keeping the
   `preserved_root` limitation sentence, which remains true (§1.4).
3. `src/editor/session.cpp:2041` — "the five rebased fields are all serialized, so
   a cached string left alone" → six.

Item 3 is the one a scoped sweep misses. It is in a different file, in a comment,
and `grep -rn "five" src/` is the sweep that finds it. MAR-185's D25 is the reason
it is written down rather than trusted to attention.

### 2.6 The planning API, and why the caller cannot name an output path

New header/source pair, `include/marrow/editor/psd_reimport_plan.hpp` and
`src/editor/psd_reimport_plan.cpp`, one line added to `add_library(marrow_editor …)`
(`CMakeLists.txt:499-521`). A new pair rather than an addition to `psd_import.hpp`,
because the planner needs `ProjectData` and `psd_import.hpp` deliberately does not
depend on the project layer.

```cpp
enum class PsdLayerChangeKind { Added, Updated, Missing };

struct PsdPlannedLayer {
    std::vector<std::string> group_path;
    std::string layer_name;
    std::string identity;               ///< Escaped, sortable. See 2.7.
    PsdLayerChangeKind change{PsdLayerChangeKind::Added};

    // What the project has today. Empty for Added.
    std::string current_slot_name, current_attachment_name, current_bone_name, current_image_file;
    // What committing this plan would produce. Empty for Missing.
    std::string proposed_slot_name, proposed_attachment_name, proposed_bone_name, proposed_image_file;

    bool preserve{true};                ///< AC5. Meaningful only for Missing.
};

struct PsdReimportPlanError {
    std::filesystem::path path;
    std::string message;
    std::string format() const;         ///< "PSD reimport planning failed for '<path>': <message>"
};

struct PsdReimportPlan {
    std::filesystem::path source_path;          ///< Candidate, as given.
    std::filesystem::path staging_root;
    std::filesystem::path staged_skeleton_path; ///< All three are under staging_root.
    std::filesystem::path staged_atlas_path;
    std::filesystem::path staged_layers_directory;
    std::vector<PsdPlannedLayer> layers;        ///< Lexicographic by identity.
    std::size_t added_count{0}, updated_count{0}, missing_count{0};
    std::optional<PsdReimportPlanError> error;
    explicit operator bool() const { return !error.has_value(); }
};

struct PsdReimportPlanOptions {
    std::filesystem::path psd_path;      ///< Candidate PSD. Required.
    std::filesystem::path staging_root;  ///< Required. Every write goes under here.
};

PsdReimportPlan plan_psd_reimport(const ProjectData& project,
                                  const PsdReimportPlanOptions& options);

/// @brief Converts a completed import into storable provenance.
PsdImportProvenance make_psd_provenance(const PsdImportResult& result,
                                        const std::filesystem::path& project_path,
                                        const std::filesystem::path& psd_path);
```

**The caller supplies a staging *root*, never an output path.** The planner
derives all three:

```cpp
    plan.staged_skeleton_path    = options.staging_root / "staged.mskl";
    plan.staged_atlas_path       = options.staging_root / "staged.matl";
    plan.staged_layers_directory = options.staging_root / "staged_layers";
```

This is the structural answer to §1.6. `write_imported_layers` `remove_all`s
whatever directory it is handed; if the API cannot be handed a directory, it
cannot be handed the wrong one. `staging_root` empty is a rejection
(`"staging root must not be empty"`), and so is a `staging_root` that already
exists and is non-empty (`"staging root must be empty or absent"`) — a planner
that inherits somebody else's files cannot tell its own output from theirs.

`existing_skeleton_path` is set **explicitly** to `project.resolved_skeleton_path()`
so the staged merge behaves like a real reimport. It is read and never written
(`psd_import.cpp:1232-1240`). Setting it explicitly also defuses the
`effective_existing_skeleton_path` fallback (`:923-928`): left unset, the importer
would silently read the *staging* skeleton, which does not exist, and produce a
different merge than a real reimport would.

`plan_psd_reimport` takes `const ProjectData&`. It never touches `EditorSession`,
history, or the runtime. AC3's "without modifying project data … or history" is
therefore true by signature; what a test can still catch — and what §6.3 Q8
catches — is the *file-system* half.

### 2.7 Identity: exact names, escaped, and no inference anywhere

The identity of a PSD layer is `(group_path, layer_name)` — exactly the strings
Photoshop stored, taken from `PsdImportedLayer::group_path` and `::name`, both of
which are populated before slot de-duplication (`psd_import.cpp:1144-1145`).

Serialised for sorting and comparison as

```
<esc(group[0])>|<esc(group[1])>|…|<esc(layer_name)>
```

where `esc` replaces `\` with `\\` and `|` with `\|`, matching MAR-186's scheme.

**Escaping is not defensive here; the collision is constructible.** `AGENTS.md`'s
"An identity collision needs a token whose neighbours are unconstrained" observes
that MAR-186 could build a collision in exactly one family, because everywhere
else arity was fixed or tokens were closed enums. Here **every token is a free
Photoshop string and the arity is variable** — a layer at depth 1 and a layer at
depth 2 produce 2-token and 3-token identities from the same alphabet. Unescaped,
group `["a|b"]` + layer `c` and group `["a"]` + layer `b|c` are the same string,
and a de-duplicating or `std::map`-keyed comparison **deletes one of them**. §6.3
Q5 builds that pair and asserts two entries survive; I13 removes the escaping and
watches it collapse to one.

Comparison is `==` on the exact token vectors. **There is no normalisation of
case, whitespace, Unicode, or path separators, and no edit-distance, prefix, or
similarity matching of any kind.** AC4 says the plan "never infers renames", and
the way to make that falsifiable is not a comment: §6.3 Q2 renames one layer and
asserts the plan contains **one Added and one Missing and zero Updated** for the
pair, and Q3 does the same for a layer moved between groups. Either case fails the
moment any fuzzy matcher is introduced, because a fuzzy matcher's whole purpose is
to turn that pair into one Updated.

### 2.8 Classification, and where the inputs come from

| Identity is in | Kind | `current_*` | `proposed_*` | `preserve` |
| --- | --- | --- | --- | --- |
| provenance **and** candidate | `Updated` | from provenance | from the staged import | *(unused)* |
| candidate only | `Added` | empty | from the staged import | *(unused)* |
| provenance only | `Missing` | from provenance | **empty** | **`true`** |

A project **without** provenance plans every candidate layer as `Added` and zero
`Missing` — a first import. This is the case that makes AC1's word "Optional"
honest, and it is §6.3 Q11.

Order is lexicographic by `identity`, ascending, over the union. Counts are
derived from the vector, never accumulated separately — **a count a mutation
cannot change is not an assertion**, so every classification case asserts the full
ordered `(identity, kind)` list with a set-difference message, and the three
counts are asserted only as a redundant clause.

**The inputs do not exist, and this is the story's biggest task.** Per E7 the two
checked-in PSDs cover one case (`Updated`, same names, moved box). Two options
were considered:

- **(a) Hand-author more binary fixtures.** Six new ~19KB opaque binaries, no
  generator, no way for a reviewer to see what a fixture contains without running
  something. Rejected.
- **(b) Synthesise PSDs in the test.** A ~150-line writer emitting 8BPS v1, RGB,
  8-bit, **raw-compression** documents from a declarative layer/group tree, into
  the smoke's temp directory. Nothing is checked in; every case's input is
  readable beside its assertions; depth-2 nesting becomes one line. **Chosen.**

The obvious risk of (b) is that a synthesiser written by reading the parser
encodes the parser's assumptions and proves nothing. The mitigation is a gate, not
a hope: **Q0 synthesises a document with the same layer tree as
`psd_import_sample.psd` and asserts `import_psd_to_runtime_bundle` returns the
identical layer and bone report** — the exact strings measured in §0.4 P4. Until
Q0 passes, no synthesised input is trusted, and Q0 runs first in file order so a
synthesiser regression is attributed to Q0 rather than to whichever case notices.

The synthesiser is a static helper inside `src/samples/psd_import_smoke.cpp`. It
has one consumer and does not belong in a library.

**One thing the synthesiser deliberately does not settle.** The parser walks layer
records in file order and its `active_groups` stack (`psd_import.cpp:729-796`)
requires each group's `lsct` 1/2 header **before** its children and the `lsct` 3
divider **after**. Both checked-in fixtures are authored that way. Whether a file
saved by Photoshop is also ordered that way is **not measured here and cannot be**:
there is no Photoshop-authored PSD in the repository and none can be produced in
this environment. The synthesiser follows the order the parser accepts and the
fixtures use. If the real-world order is the inverse, every group-bearing PSD from
Photoshop fails at `psd_import.cpp:734` with
`"PSD folder end marker appeared without an open folder."` — a pre-existing
property of `psd_import.cpp`, **not** something MAR-188 introduces or is scoped to
fix. It is recorded in §9 and flagged to MAR-189/190, which own the real import
path.

### 2.9 Duplicate identities are refused, in both directions

Two distinct duplicate situations, and AC6 names both under one word.

- **In stored provenance** — two `layers[]` entries with the same
  `(group_path, layer_name)`. Refused at **load** (§2.4), because a project whose
  provenance cannot key itself is malformed, and because the file-format layer is
  where the repo already refuses this class (`project.cpp:469`, `:1348`).
- **In the candidate PSD** — Photoshop permits two layers with the same name in
  the same folder, and the importer resolves them positionally
  (`make_unique_name`, `psd_import.cpp:593-609`) rather than by identity. Refused
  at **planning**, with `"psd layers must have unique group and name identities"`
  and the colliding identity in `PsdReimportPlanError::path`'s companion message.

Refusal, rather than pairing, is forced by AC4. Given two provenance entries and
two candidate entries sharing an identity, **any** pairing between them is an
inference — positional pairing most of all, since PSD record order is exactly what
a user reorders in Photoshop without meaning anything by it. A story whose
criterion is "never infers renames" cannot resolve duplicates by guessing, so it
declines and says why.

### 2.10 Zero mutation, and how the half a signature cannot prove is proved

`plan_psd_reimport(const ProjectData&, …)` cannot modify project data, history or
the session; that half is a signature, not a test. The half that needs a test is
the file system, and the mutation that would break it is realistic: pointing a
staged output at a real project path and letting `remove_all`
(`psd_import.cpp:1076`) run.

§6.3 Q8 therefore asserts, before and after a successful plan:

1. `serialize_project(project)` is **byte-identical** — the string, not a flag.
2. The project file on disk is byte-identical.
3. The skeleton file is byte-identical.
4. Every atlas file is byte-identical.
5. A **full sorted recursive listing** of the project directory, with sizes, is
   identical — set difference reported by name, so a *created* file fails as
   loudly as a deleted one.
6. Every path in `plan.staged_*` has `staging_root` as an ancestor
   (`lexically_normal`, then prefix check), asserted individually by field name.

Clauses 1-5 also run on the **failure** paths (Q6, Q7): a rejected plan must be as
inert as an accepted one, and a parse failure that had already `remove_all`ed
something would otherwise be invisible.

### 2.11 The registry does not move, and the proof is a diff

MAR-188 adds no agent operation and no MCP tool. `plan_psd_reimport` is a C++ API;
the story says "UI-free planning API" and no acceptance criterion mentions an
operation. Following MAR-180's precedent, the proof is a **zero-line diff**, not an
assertion:

```
git diff --stat src/editor/agent_dispatch.cpp src/editor/agent_handlers_*.cpp tools/   # empty
git diff -U0 -- src/ tools/ CMakeLists.txt | grep -E '^[+-]' | grep -E '\b(64|65|66|67)\b'   # 0 lines
```

Untouched, and re-verified rather than assumed: registry **66**; the **eleven**
`operation_count_before != 66U` guards at `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`,
`shell_smoke_constraints.cpp:147,676` and `shell_smoke_timeline.cpp:3697,4389`;
`std::array<OperationExpectation, 66>` at `agent_dispatch_smoke.cpp:42` (**not**
`:39`, E2); `tools/mcp/test_client.py:53` and `:55`.

Formats do not move either: no `.mskl`, `.mbin` or C ABI change, so
`docs/root1/format-spec.md` stays byte-identical. `.marrow` **does** gain an
optional key, which `docs/root1/format-spec.md` documents if and only if it
already documents `$.editor`'s optional keys — checked in Task 0, not assumed.

### 2.12 What MAR-188 does not do

Deliberate boundaries, each with the story that owns it:

- **Nothing calls the importer in production, and MAR-188 does not change that.**
  `make_psd_provenance` converts a `PsdImportResult` into provenance; wiring an
  actual import command to persist it is MAR-189/190's. Adding a caller here would
  mean designing the commit path, which is MAR-189's whole story.
- **No commit, no journaling, no rollback.** MAR-189.
- **No preview, no confirmation dialog, no missing-layer checklist.** MAR-190.
- **No rename detection, ever.** AC4, and §2.7 makes it falsifiable.
- **No change to `psd_import.{hpp,cpp}`.** The planner consumes the importer's
  existing API. A diff over `src/editor/psd_import.cpp` and
  `include/marrow/editor/psd_import.hpp` is expected to be **empty**, and Task 7
  asserts it.

---

## 3. Where each piece lands

| File | Change |
| --- | --- |
| `include/marrow/editor/project.hpp` | `PsdLayerProvenance`, `PsdImportProvenance`, `ProjectImportSources` above `ProjectMetadata` (`:575`); one `std::optional<ProjectImportSources> import_sources;` member inside it; doc-comment amendments at `:1033` and `:1052-1054` |
| `src/editor/project.cpp` | `parse_import_sources` beside `parse_editor_metadata` (`:877`); `build_import_sources_value` beside `build_atlas_pack_definitions_value` (`:4782`); the assign-or-erase block in `build_project_value` before `:5089`; the sixth family in `rebase_project_paths` (`:7762-7797`) |
| `src/editor/session.cpp` | one comment, `:2041`, "five" → "six" |
| `include/marrow/editor/psd_reimport_plan.hpp` | **New.** The §2.6 vocabulary and two function declarations |
| `src/editor/psd_reimport_plan.cpp` | **New.** `plan_psd_reimport`, `make_psd_provenance`, the identity escaper, the staging derivation |
| `src/samples/editor_project_smoke.cpp` | `validate_mar188_psd_provenance(...)`, P1-P8; registered after MAR-186's call at `:17948-17951` (and after MAR-187's, if it has landed) |
| `src/samples/psd_import_smoke.cpp` | the PSD synthesiser; `validate_mar188_reimport_planning(...)`, Q0-Q11; called from `main` (`:323-385`) after the existing two scenarios |
| `CMakeLists.txt` | **one** line: `src/editor/psd_reimport_plan.cpp` in `add_library(marrow_editor …)` (`:499-521`). No new target, no `add_test` |
| `docs/root1/format-spec.md` | `$.editor.import_sources` documented, iff `$.editor`'s optional keys are already documented there |
| `docs/root1/fixtures.md` | a note at `:176-183` that classification inputs are synthesised, not checked in |
| `AGENTS.md`, PRD | plan §9 |

**Not touched, and asserted:** `src/editor/psd_import.cpp`,
`include/marrow/editor/psd_import.hpp`, `src/editor/agent_dispatch.cpp`, every
`agent_handlers_*.cpp`, `tools/`, `docs/root1/format-spec.md`'s `.mskl`/`.mbin`
sections, and every shell source.

---

## 4. Acceptance criteria → artifacts

| AC | Artifact | Proof |
| --- | --- | --- |
| **AC1** — optional `.marrow.editor.import_sources.psd` stores project-relative provenance and stable group/name → layer/target mappings | §2.1 vocabulary, §2.4 parser, §2.3 serializer | **P2** (round trip asserted through the **typed field**, full sorted identity list, never a text search — §0.4 P1 is why), **P3** (the typed value beats the preserved copy), **P4** (erase on empty), **P8** (eleven rejections, each on its message) |
| **AC2** — old projects without provenance still load; Save As rebases through MAR-180's rules | §2.5 sixth family | **P1** (`player_idle.marrow` loads, field is `nullopt`, no `"import_sources"` in the output), **P5** (relative arm, both fields, resolving to the same absolute files), **P6** (absolute arm survives byte-identical), **P7** (sibling arm turns both absolute), all routed through `save_project` → `load_project` |
| **AC3** — planning parses and builds entirely in staging, modifying no project data, runtime source, history or target file | §2.6 derived staging, `const ProjectData&` | **Q8** (six clauses: serialization string, project file, skeleton, atlases, full recursive directory listing, staged-path containment by field name), and clauses 1-5 repeated on the failure paths **Q6**, **Q7** |
| **AC4** — added / updated / missing by exact group and layer names; never infers renames | §2.7, §2.8 | **Q1** (full ordered `(identity, kind)` list), **Q2** (rename → Added + Missing, zero Updated), **Q3** (group move → Added + Missing), **Q4** (depth-2 nesting preserved end to end), **Q5** (escaped identities do not collide) |
| **AC5** — missing defaults to preservation; the plan exposes stable identities and proposed target changes | `PsdPlannedLayer::preserve`, the `proposed_*` fields | **Q9** (`preserve == true` and empty `proposed_*` for every Missing; no Missing proposes a deletion), **Q10** (two independent plans are identical, and the order is the full asserted lexicographic list), **Q1** (`proposed_*` populated for Added and Updated) |
| **AC6** — project and PSD tests cover round-trip, rebasing, nested groups, classification, duplicate identities, parse failure, zero-mutation planning | Two binaries, §6 | round trip → P2/P3/P4; rebasing → P5/P6/P7; nested groups → Q4 (and Q0's depth-1 gate); classification → Q1/Q2/Q3/Q11; duplicates → **P8(k)** (stored) and **Q6** (candidate); parse failure → **Q7**; zero-mutation → **Q8** |

---

## 5. Test surfaces

Two binaries, matching AC6's own words "Project and PSD tests".

- **`marrow_project_smoke`** (`src/samples/editor_project_smoke.cpp`) — everything
  about the `.marrow` field: parse, serialize, erase, rebase, reject. It already
  owns the project layer and MAR-180's Save As cases, and the pre-existing
  detector at `editor_project_smoke.cpp:1681-1687` lives there.
- **`marrow_psd_import_smoke`** (`src/samples/psd_import_smoke.cpp`) — everything
  that needs a PSD: the synthesiser, classification, duplicates, parse failure,
  zero mutation. It already links `marrow_editor` (`CMakeLists.txt:854-858`), so
  `psd_reimport_plan.cpp` is reachable without a CMake change beyond the library
  line.

**No third binary, and no `add_test`.** `ctest -N` stays at **22**. Adding CTest
registration for `marrow_psd_import_smoke` is a real improvement and an
out-of-scope one: it changes the CTest count that eleven other stories' validation
records quote, for a binary whose default arguments are repo-root-relative
(`psd_import_smoke.cpp:21-22`). Recorded in §9.

The PSD smoke's first-failure-aborts style is kept — new cases use the existing
`expect` helper (`psd_import_smoke.cpp:27-33`) — but every new case is a named
`bool validate_mar188_*` function whose failure messages **begin with the case
label** (`"Q3: …"`), because `expect` prints nothing else and a bare message in a
385-line straight-line `main` is unattributable.

---

## 6. Cases

Numbering is the **run order**, which is also the file order, which is how every
inversion in §7 is attributed. Authoring order is irrelevant and is never used.

### 6.1 The rule every case obeys

- Assert **the message**, never `!result`. Five stories in this chain have been
  bitten by a case that passed on a different failure than the one it named.
- Assert **full sorted identity lists** with set-difference messages, never counts
  alone. A count a mutation cannot change is not an assertion.
- A round trip goes through **`save_project` → `load_project(path)`**, never
  `save()` alone: `validate_project_for_save` is in `project.cpp`'s anonymous
  namespace (opens `:24`, closes `:6862`) and cannot be called from another TU, and
  only `load_project(path)` materialises.
- **Never author a double needing 17 significant digits and round-trip it.**
  `serialize_project` is not bit-exact for those (`AGENTS.md`, "`serialize_project`
  is NOT bit-exact…"). MAR-188 stores no doubles, so this is a constraint on the
  *fixture bounding boxes*, which are integers, and it is satisfied by
  construction — recorded so nobody adds a float later without noticing.
- **Never rely on vector order across a round trip.** `Value::Object` is a
  `std::map` and `.marrow` normalises order. `layers[]` is a JSON **array** and so
  does keep order — but the classification cases collect in memory anyway and
  assert the plan's *sorted* order explicitly, so no case depends on a file's
  array order.

### 6.2 Project cases — `validate_mar188_psd_provenance`

**P1 — an old project loads and gains nothing. COMPATIBILITY WITNESS, NOT A
GATE.** `load_project(player_idle.marrow)` succeeds;
`project->editor_metadata.import_sources` is `std::nullopt`; `serialize_project`
contains no `"import_sources"`; and the serialization is **byte-identical to the
same call before the story** (captured in Task 0 as a baseline file, 6111 bytes,
and compared with `cmp`).

**P1 is green on the pristine tree and no inversion in §7 turns it red** — Task 0
measured both. It is kept because the backward-compatibility claim is real and
nothing else makes it, and it is labelled here so that "P1 green" is never
recorded as evidence that MAR-188 works. See §0.3.1: this case is the class's
first self-inflicted instance in this chain.

**P2 — the round trip, through the typed field.** Author a document carrying
`import_sources` with two layers, one at depth 0 and one at depth 2. `load_project`
→ assert `source_path`, `layers_directory`, and the **full sorted list of
`(group_path joined, layer_name, slot, attachment, bone, image_file)` tuples**,
reported as a set difference. Then `serialize_project` → `parse_document` →
`load_project` again and assert the same list. **The assertion is on the struct,
never on the text**, because a text assertion passes on unmodified code (§0.4 P1)
and would be the sixth uncatchable case in this chain.

**P3 — the typed value beats the preserved copy.** Load a project with provenance;
set `project.editor_metadata.import_sources->psd->source_path = "art/other.psd"`
in memory; `serialize_project`; reload; assert the reloaded `source_path` is
`art/other.psd`. Fails if the serializer omits the assign, because the preserved
copy still holds `art/hero.psd`.

**P4 — erase on empty, both levels.** (a) From a project with provenance, set
`import_sources = std::nullopt`, serialize → no `"import_sources"` substring, and
the reload has `nullopt`. (b) Set `import_sources` present with `psd == nullopt` →
same result: an absent key, not `{}`.

**P5 — Save As into a parent directory rebases both paths and keeps them
relative.** Fixture project in `<tmp>/proj/art/x.marrow` with
`path = "hero.psd"`, `layers_directory = "hero_layers"`; `save_project` to
`<tmp>/proj/up.marrow`; `load_project` that; assert **by field name**:
`source_path == "art/hero.psd"`, `layers_directory == "art/hero_layers"`, and that
both `weakly_canonical`-resolve to the same absolute files as before. §0.4 P2b is
the measurement that says a parent-directory destination is the one that keeps
references relative.

**P6 — an absolute provenance path survives byte-identical.** `source_path`
absolute (and inside the Save As destination, so a relative form is always
representable and the case cannot be masked by temp-directory layout — MAR-180's
S3 learned this); after Save As it is still `is_absolute()` and string-equal.

**P7 — Save As into a sibling turns both absolute, and they still resolve.** The
other arm of §0.4 P2/P2b. Both fields `is_absolute()`, both resolving to the same
files.

**P8 — eleven rejections, each asserted on its message, each leaving
`serialize_project()` byte-identical.** (a) `import_sources` a string;
(b) `psd` an array; (c) `path` absent; (d) `path` empty; (e) `layers_directory`
empty; (f) `layers` an object; (g) a `group_path` element a number; (h) `layer`
empty; (i) `image` empty; (j) `image` = `"sub/body.png"`; (k) two entries sharing
`(group_path, layer_name)`. Each asserts `loaded.error->format()` **contains** the
§2.4 message and the JSON path, and that a control project serialised in the same
scope is unchanged.

### 6.3 PSD cases — `validate_mar188_reimport_planning`

**Q0 — the synthesiser gate.** Synthesise a document with the tree of
`psd_import_sample.psd` — `shadow` at root; `arm_l` and `body` inside `torso`, with
the §0.4 P4 boxes — import it, and assert the layer report and bone report equal
the checked-in fixture's **exactly**: three layers with those names, groups,
slots, attachments, bones, **`image_file` names** and boxes; two bones `root` and
`torso(4,12)`. Runs first. Nothing downstream is trusted until it passes.

**Q0's four clauses, and why a report match alone is not enough.** Task 0
established that the report is a *lossy projection* of the file: it carries no
pixel data, no channel count, no depth and no colour mode. A synthesiser with a
wrong pixel encoding — wrong channel order, wrong row padding — reproduces the
whole report while every extracted PNG is wrong, which would silently corrupt Q1's
`proposed_image_file` clause and everything MAR-189 stages on top. So:

1. **The report clause.** As above, including `image_file` — named explicitly,
   because an omitted field in an assertion list is the same defect shape as an
   omitted term in a fixed-length sum, and §1.3 records that this tree has
   shipped one of those.
2. **The pixel clause.** Assert the extracted PNG for at least one layer decodes
   to the exact RGBA the synthesiser wrote, pixel for pixel, at a known
   coordinate. Nothing in the report can see this. Note the parser drops fully
   transparent layers (`psd_import.cpp:757-758`), so an all-zero synthesiser fails
   loudly — it is the *partially* wrong encoding this clause exists for.
3. **The header clause.** Assert the synthesised file's 8BPS signature, version,
   channel count, bit depth and colour mode equal the checked-in fixture's own
   header bytes, read from `psd_import_sample.psd` at run time rather than
   hard-coded. Cheap, and it catches a whole class of wrong encodings the report
   cannot. Record how close the synthesised file's size comes to the fixture's
   **19,636** bytes; report-match plus header-match is materially stronger
   evidence than report-match alone.
4. **Q0b — the duplicate-name branch.** `parse_psd_document:814-826` picks
   `layer.original_name` when the document-global census is 1 and
   `join_path(group_path, name)` (separator `/`, `psd_import.cpp:574-591`) when it
   is greater. Q0's document has no duplicates, so **the `join_path` branch is
   ungated**. Q0b synthesises two layers named `body`, one in `torso` and one at
   root, and asserts the resulting slot names are exactly `torso/body` and `body`.

   **Corrected after review.** This paragraph originally justified Q0b by saying
   "Q5 and Q6 are the two cases that live in that branch and have no other gate".
   **Neither does.** Q5's layer names (`b|c` and `c`) are distinct, so the
   document-global census is 1 for each and the branch is never entered — Q5
   exercises the planner's `|`-escaping, a different mechanism entirely. Q6 reaches
   the branch at import time but asserts on `build_identity(group_path, name)`,
   which reads the original layer name and never `slot_name`. **Q0b is therefore
   the branch's only gate anywhere**, which is a stronger justification than the
   one first given. The decision was right for the wrong reason.

**Q1 — classification, full list.** Provenance from the fixture import.
Candidate = synthesised: `shadow` unchanged, `torso/body` moved, `torso/arm_l`
**deleted**, `torso/hand_r` **added**. Assert the plan's full ordered
`(identity, kind)` list is exactly
`[("shadow", Updated), ("torso|arm_l", Missing), ("torso|body", Updated), ("torso|hand_r", Added)]`,
by set difference; then the three counts as a redundant clause; then that every
Updated and Added entry has non-empty `proposed_slot_name`/`_attachment_name`/
`_bone_name`/`_image_file`.

**Q2 — a rename is two events, never one.** Candidate identical to the fixture
except `torso/arm_l` → `torso/arm_left`. Assert **exactly one Added
(`torso|arm_left`) and exactly one Missing (`torso|arm_l`)**, and that **no** entry
whose identity is either of those has kind `Updated`. Fails on any similarity
matcher.

**Q3 — a group move is two events.** `torso/body` → root `body`. Assert
`("body", Added)` and `("torso|body", Missing)`; `shadow` and `torso/arm_l` still
Updated.

**Q4 — depth-2 nesting, end to end.** Candidate adds `torso/upper/hand`. Assert
the planned entry's `group_path` is exactly `{"torso", "upper"}` (element-wise, not
the joined string), its identity is `torso|upper|hand`, and its
`proposed_bone_name` is what the importer produced. **This is the first coverage
depth ≥ 2 has ever had** — `parse_psd_document`'s `:746-748` inheritance and
`build_bones_value`'s parent-relative offsets (`:962-972`) are unexercised at HEAD.

**Q5 — escaped identities do not collide.** Candidate contains group `["a|b"]`
layer `c` and group `["a"]` layer `b|c`. Assert the plan has **two** entries with
**distinct** identities, listed. I13 removes the escaping and collapses them.

**Q6 — a duplicate identity in the candidate is refused.** Two layers named `body`
in `torso`. Assert `!plan` **and** that `plan.error->format()` contains
`"psd layers must have unique group and name identities"` and the identity
`torso|body`; then Q8's clauses 1-5 (nothing mutated).

**Q7 — a parse failure is an error, not a throw, and writes nothing outside
staging.** Candidate is a text file. Assert `!plan`, that `format()` contains
`"PSD files must begin with the 8BPS signature."` and the candidate's path
(measured in §0.4 P5), then Q8's clauses 1-5, and that `staged_skeleton_path` does
not exist.

**Q8 — zero mutation.** The six clauses of §2.10, on a successful plan.

**Q9 — missing defaults to preservation.** Over Q1's plan: every `Missing` entry
has `preserve == true` and all four `proposed_*` strings empty; no entry of any
kind carries a proposed deletion. Asserted per entry, by identity, not as a count.

**Q10 — determinism.** Load the same project twice into two independent
`ProjectData`s, plan twice into two different staging roots, and assert the two
plans' full `(identity, kind, current_*, proposed_*, preserve)` tuples are
element-wise equal and in the same order, and that the order is exactly the
ascending sort of the identities.

**Q11 — no provenance means a first import.** A project with
`import_sources == nullopt`, candidate = the fixture: every layer `Added`, zero
`Missing`, zero `Updated`, and `current_*` empty on every entry.

---

## 7. Inversions, and the mechanism that makes each observable

The register with predicted failure texts and per-case attribution is in the
plan's §B. What belongs here is the **mechanism** — why each mutation is invisible
to everything except the case named, which is a claim about the code.

| Mutation | Why nothing else notices |
| --- | --- |
| **I1** Loader ignores `import_sources` entirely | Every *text*-level round trip still passes, because `preserved_root` re-emits the key (§0.4 P1). Only **P2**, which reads the typed field, can fail. This inversion is the reason P2 is written the way it is |
| **I2** Serializer parses but never assigns `editor_object["import_sources"]` | P2's first half passes (parse works) and P2's second half **also** passes, because the preserved copy is byte-identical to what was authored. Only **P3**, which edits the field in memory first, sees the stale copy win |
| **I3** Serializer assigns but never erases when `nullopt` | P1-P3, P5-P8 all pass. Only **P4** looks at a project whose provenance was cleared |
| **I4** `psd == nullopt` serialises as `"import_sources": {}` | P4(a) passes — the outer optional is empty in that arm. Only **P4(b)** distinguishes them |
| **I5** Sixth family rebases `source_path` only | P5's `source_path` clause passes. Only P5's **`layers_directory` clause by name** fails. Separate from I6 because one inversion cannot tell "the family exists" from "the family is complete" — §1.3 |
| **I6** Sixth family rebases `layers_directory` only | The mirror. P5's `source_path` clause fails, its `layers_directory` clause passes |
| **I7** Sixth family reads `result` instead of `project` | Both fields are rebased against a `source_path` already overwritten at `project.cpp:7766`, so both resolve to the *new* directory and P5's `weakly_canonical` identity clauses fail while the string clauses may still look plausible. The identity clauses are the detector, which is why P5 has them |
| **I8** The `is_absolute()` early return is removed from the shared lambda | P6 fails. Note this mutates code the other five families share, so P5 and MAR-180's S3 fail too — attribution is by **run order**, and P5 runs first. Recorded rather than hidden |
| **I9** `plan_psd_reimport` accepts caller-supplied output paths and the test passes the project's real layers directory | Nothing in the plan's *content* changes: the classification is identical. Only **Q8 clause 5**, the recursive directory listing, sees the `remove_all`. This is the inversion that justifies clause 5 existing |
| **I10** `existing_skeleton_path` left unset (the `psd_import.cpp:923-928` fallback) | Q1's Added/Missing sets are unchanged, because classification reads the *candidate*, not the merge. The `proposed_bone_name` of an Updated layer changes, so **Q1's proposed-target clause** fails. Without that clause the inversion is invisible — which is why Q1 asserts targets and not only kinds |
| **I11** Classification falls back to `slot_name` when the identity misses | Q1 passes: in Q1 every slot name equals its layer name. Only **Q3**, where a group move changes the identity but not the slot name, turns two events into one |
| **I12** A Levenshtein-threshold matcher is added | Q1 and Q3 pass. Only **Q2**, whose two names differ by four characters, collapses to one Updated |
| **I13** Identity escaping removed | Q1-Q4 pass; no fixture there contains `\|`. Only **Q5** collapses two entries to one |
| **I14** Candidate duplicates are paired positionally instead of refused | Q1-Q5 pass. Only **Q6** has a duplicate |
| **I15** `preserve` defaults to `false` | Q1's kinds and counts are unchanged. Only **Q9** reads the flag |
| **I16** The plan is emitted in candidate-record order instead of sorted | Q1's set-difference clause passes (a set is order-free). Only **Q10's** ordered element-wise comparison, and Q1's *ordered* list clause, fail. Both are written as ordered lists for this reason |
| **I17** Counts accumulated in a parallel loop rather than derived | Every list clause passes. The counts drift only under a specific edit, so this inversion is expected to be **caught by Q1's redundant count clause and nothing else** — and is recorded as the demonstration that the count clause is redundant, not decorative |
| **I18** The synthesiser writes `lsct` 3 before the group's children | **Q0** fails immediately with `"PSD folder end marker appeared without an open folder."` — the §2.8 ordering property, made observable rather than assumed |

### 7.1 Deliberately uninverted, by name

- **The `require_type`-sourced rejections in P8** (a, b, f, g). Those messages come
  from `src/runtime/json.cpp:968-983` and inverting them means mutating the JSON
  layer, which is shared by every parser in the tree. The domain rejections (d, e,
  h, i, j, k) are inverted.
- **`make_psd_provenance`'s field-by-field copy.** It is a six-assignment list with
  the same compiler blindness as §1.3, and Q1's full-tuple assertion is its
  detector; inverting each of six assignments separately would be six inversions
  proving one thing. **One** inversion is run — dropping `bone_name` — and the
  other five are recorded here as covered by the same clause.
- **`session.cpp:2041`'s comment.** A comment cannot be inverted. It is verified by
  reading, and by Task 7's `grep -rn "five" src/editor/session.cpp`.

---

## 8. Facts to MEASURE in Task 0, not assume

1. Every `file:line` in §1, §2, §3 re-resolved by **symbol**, against
   `git show HEAD:` blobs and not the worktree (MAR-185 D25). A citation that no
   longer names its construct is a **blocking finding**, recorded in the errors
   table, never silently adjusted.
2. The P1 baseline: `serialize_project(load_project(player_idle.marrow))` captured
   to a file **before any code**, for P1's `cmp`.
3. That `docs/root1/format-spec.md` does or does not document `$.editor`'s optional
   keys — this decides whether §3's format-spec row is work or a no-op.
4. `ctest -N` = 22; `marrow_agent_dispatch_smoke` `[ OK ]` count; the registry
   66 and its four hand-edited site families (§2.11), each at its own line.
5. That `marrow_psd_import_smoke` and `marrow_project_smoke` both pass **before**
   any change, from the repo root, with the `AGENTS.md:174` arguments.
6. Whether MAR-187 has landed. If it has, the `editor_project_smoke.cpp`
   registration point and the `CMakeLists.txt` library hunk have moved; re-derive
   both. If it has not, MAR-188 registers directly after MAR-186's `:17948-17951`.
7. The `-Wswitch` baseline: pristine warning count, so `PsdLayerChangeKind`'s
   arrival can be measured. Per `AGENTS.md`: **delete every object file and build
   every target**, `find build/CMakeFiles -name '*.o' -delete` then
   `cmake --build build -j8 2>&1 | grep Wswitch`. A single-target probe
   understates. A missed arm **warns and does not stop the build**.
8. `git status --porcelain` before and after, to confirm no other agent's tracked
   file was touched.

---

## 9. Known limitations, stated rather than hidden

- **`preserved_root` is still opaque, and MAR-180's generalised criterion stays
  PERMANENTLY OPEN.** MAR-188 makes it six known families out of an unbounded
  document. Any relative path a future story leaves unparsed is still silently
  stale on Save As. Closing it requires a schema-strict loader or a declaration
  that unparsed paths are unsupported — neither is MAR-188's to take. §1.4 point 3.
- **Photoshop's real record order is unverified.** §2.8. No Photoshop-authored PSD
  exists in the repository and none can be produced here. If the real order is the
  inverse of the fixtures', every group-bearing real-world PSD fails at
  `psd_import.cpp:734` — a pre-existing property, flagged to MAR-189/190.
- **`marrow_psd_import_smoke` is still not in CTest.** §5.
- **No save-time validation of `image_file`.** §2.4 — every write path in this
  story constructs it from `filename()`, so the rule would have no reachable
  failing input.
- **Slot names remain unstable across imports.** §1.5: adding a layer can rename an
  untouched slot, because `parse_psd_document` (`psd_import.cpp:814-826`) dedups slot names
  from a document-global census.
  MAR-188 *records* what the names were and keys on something else; it does not
  make the importer's naming stable. A user whose slots get renamed by a reimport
  will see it in the plan's `proposed_slot_name` differing from
  `current_slot_name` on an `Updated` row — which is the honest surface for it —
  but MAR-188 does not refuse or repair it. MAR-189 owns whether that is allowed to
  commit.
- **A crash mid-plan leaves the staging directory behind.** Nothing removes it, by
  design: the staging root is the caller's to inspect and to clean. Consistent
  with MAR-180's recorded orphan-temporary limitation.
- **Missing layers are only *defaulted* to preservation.** MAR-188 exposes the
  flag; nothing consumes it. The checklist that lets a user opt into deletion is
  MAR-190's.

---

## 10. Risks for the implementer

1. **The round-trip test that cannot fail.** §0.4 P1. If P2 is written as a text
   search it passes on an empty commit. This is the single most likely way to ship
   a story that measures nothing, and it is pre-armed by the storage layer.
2. **The synthesiser is the schedule risk.** §2.8. It is the only new mechanism in
   the story and eight cases depend on it. Q0 gates it; if Q0 cannot be made to
   pass, every classification case is blocked and the fallback (hand-authored
   binaries) is worse. Build it first, in Task 3, before any classification code.
3. **`remove_all` at `psd_import.cpp:1076`.** A planner that accepts output paths
   can destroy a user's layer directory. §2.6 removes the possibility from the API
   shape; do not "helpfully" add an overload that takes them.
4. **Two rebased fields, no compiler help.** §1.3. Assert both by name; invert both
   separately.
5. **Anchors drift by hundreds of lines in this file.** §0.3 E5: six of six MAR-180
   citations are stale. Re-resolve by symbol, every time.
