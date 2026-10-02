# MAR-188 Plan Provenance-Aware PSD Reimports — Implementation Plan

Design: `docs/superpowers/specs/2026-09-01-mar-188-provenance-aware-psd-reimport-planning-design.md`.
Baseline measured for both documents: **`687ed4f`**.
Story: `.agents/tasks/prd-marrow-runtime.json` → `MAR-188`, `dependsOn: ["MAR-187"]`.

---

## Standing rules for this plan

Each of these is here because it has cost this chain a story. They are not style.

1. **Assert the message, never `!result`.** Five stories have shipped a case that
   passed on a different failure than the one it named. Every rejection case
   asserts that `error->format()` contains both the domain message and the JSON
   path.
2. **A count a mutation cannot change is not an assertion.** Every list is asserted
   as a **full sorted identity list with a set-difference message**. Counts appear
   only as redundant clauses, and §B I17 exists to show which clause is actually
   load-bearing.
3. **Order-sensitive claims are asserted as ordered lists.** A set difference is
   order-free and cannot see I16.
4. **Neutering preserves the call.** `if (false && f(...))` is forbidden: `&&`
   short-circuits away the very call whose side effect is the detector. Use
   `(void)f(...)`, or delete the whole block after establishing it shares no state
   with the case being demonstrated.
5. **Every verification build deletes object files and builds every target.**
   `find build/CMakeFiles -name '*.o' -delete` then `cmake --build build -j8`.
   The generator is `Unix Makefiles` on GNU Make 3.81, whose one-second mtime
   granularity produced MAR-184's nine false "did not bite" readings. Both
   directions: a skipped mutation build is a false pass, a skipped restore build
   misattributes the *next* inversion.
6. **Restores are verified by `cmp` against an independently kept pristine copy.**
   No `git checkout`, `git restore`, `git stash` or `git reset` is run against a
   tracked file at any point in this story. Other agents are editing tracked files
   concurrently.
7. **Attribution is by run order**, which is file order, which is the P/Q
   numbering. Authoring order is never used.
8. **A sweep that returns nothing must be shown to have run.** Quote `--include`
   patterns (`--include='*.cpp'`); an unquoted one is zsh-glob-expanded and prints
   a confident `(none)` while `grep` never ran. Echo `$?` beside every "no hits".
9. **Ask what a sweep does not reach before trusting what it returns.** Every wrong
   number in this chain came from a sweep correct within an unexamined scope.
   Widen to the repository, and read `git show HEAD:` blobs rather than the
   worktree.
10. **A passing `save()` proves nothing.** `validate_project_for_save` is inside
    `project.cpp`'s anonymous namespace (opens `:24`, closes `:6862`) and cannot be
    called from another TU. Route through `save_project`; only `load_project(path)`
    materialises.
11. **`serialize_project` is not bit-exact for 17-significant-digit doubles.**
    MAR-188 stores no doubles; the constraint applies to any float anyone is
    tempted to add to `PsdLayerProvenance`. Do not.
12. **`.marrow` keys overlays by animation name and `Value::Object` is a
    `std::map`.** No case may depend on a file's member order. `layers[]` is a JSON
    array and does keep order, but the classification cases collect in memory and
    assert the plan's own sort, so nothing depends on it.
13. **A `static_assert` on a count constant is worthless** if the constant is
    initialised from the same literal. MAR-186 shipped one and it was proven a
    tautology. Do not add one for the rebase families; the detector is §B I5/I6.
14. Do not run anything against the shared worktree that writes to it. Build from
    `git archive HEAD | tar -x` in scratch when a probe is needed before Task 1.

---

## §A. What was verified, what was wrong, and what Task 0 must still gate

### A.1 Errors found in the incoming documents

Full statements in design §0.3. Summary, so the implementer does not re-derive
them:

| # | Source | Wrong claim | Truth |
| --- | --- | --- | --- |
| E1 | brief | `import_sources` "exists nowhere but a doc comment", implying the JSON key does not round-trip | The **typed structure** is absent; the **JSON key already round-trips verbatim** through `preserved_root`. Measured. **This is a gate that PASSES on unchanged code** — the mirror of the "gate that fails on correct code" class (design §0.3.1). Reshapes P2 |
| E2 | brief | `agent_dispatch_smoke.cpp:39` | **`:42`** at HEAD, verified against `git show HEAD:` |
| E3 | brief | MAR-187 "will move `diagnostics.hpp`" | It will not; the header exists at HEAD, carries no `OverlayRecordKey`, and MAR-187 put that key in its own new `safe_fix.hpp` instead. **MAR-187 adds at least four new files**, enlarging the merge surface — design §0.2, gated by Task 0.9 |
| E4 | `AGENTS.md:2337` | `grep -rn "import_sources"` returns nothing | Returns `include/marrow/editor/project.hpp:1053` |
| E5 | `AGENTS.md:2348` | six path-family citations | **All six stale, none names its construct.** Re-derived in design §1.2 |
| E6 | brief | eleven `!= 66U` guards | Correct; split **7 graph / 2 constraints / 2 timeline**. MAR-180's record says ten and predates `shell_smoke_timeline.cpp:4389` |
| E7 | `fixtures.md:183` | the fixture pair is adequate input | It covers **one** case. 4-byte diff, identical layer sets, no depth ≥ 2 anywhere. Forces design §2.8 |

### A.2 Design claims Task 0 must **prove by running**

| # | Claim | Gate |
| --- | --- | --- |
| A1 | `$.editor.import_sources.psd` round-trips today with zero code | 0.4 |
| A2 | `rebase_project_paths` leaves it stale today | 0.4 |
| A3 | Save As into a **parent** directory keeps references relative; into a **sibling** it makes them absolute | 0.4 |
| A4 | `create_minimal_project` has an empty `preserved_root` | 0.4 |
| A5 | The two checked-in PSDs yield exactly the §0.4 P4 identities | 0.5 |
| A6 | A non-PSD input fails with `"PSD files must begin with the 8BPS signature."` and writes no skeleton | 0.5 |
| A7 | `player_idle.marrow` serialises byte-identically before and after (the P1 baseline) | 0.3 |
| A8 | `-Wswitch` fires for a new value of `PsdLayerChangeKind` across **all** targets | 0.7 |
| A9 | Registry 66 and its four hand-edited site families | 0.6 |
| A10 | `docs/root1/format-spec.md` does / does not document `$.editor`'s optional keys | 0.8 |

Every one of A1-A6 was measured by this plan's author in an isolated tree and the
results are quoted in design §0.4. Task 0 re-runs them **against the tree the
implementer is actually building**, because the author's tree was `687ed4f` and
the implementer's may not be.

### A.3 Claims re-verified at `687ed4f` — do not re-litigate, only re-anchor

Every line below was opened and read. Task 0.1 re-resolves each **by symbol**; a
line that no longer names the construct is a blocking finding.

| Anchor | Construct |
| --- | --- |
| `include/marrow/editor/project.hpp:575` | `struct ProjectMetadata {` |
| `include/marrow/editor/project.hpp:588` | `ProjectMetadata editor_metadata;` |
| `include/marrow/editor/project.hpp:1024-1062` | `rebase_project_paths` doc comment; `:1033` "Five families are rebased"; `:1052-1054` the MAR-188 paragraph |
| `include/marrow/editor/project.hpp:1007-1016` | `struct MinimalProjectOptions` |
| `src/editor/project.cpp:24` | the anonymous namespace opening (closes `:6862`) |
| `src/editor/project.cpp:877` | `std::optional<LoadError> parse_editor_metadata(` |
| `src/editor/project.cpp:972-999` | the `editor.viewport.onion_skin` optional-object parse — the shape to copy |
| `src/editor/project.cpp:4782` | `build_atlas_pack_definitions_value` |
| `src/editor/project.cpp:5006` | `Value build_project_value(const ProjectData& project) {` |
| `src/editor/project.cpp:5023` | `Value::Object editor_object = preserved_object_member(project.preserved_root, "editor");` |
| `src/editor/project.cpp:5089` | `root["editor"] = make_object_value(std::move(editor_object));` |
| `src/editor/project.cpp:5192 / :5197 / :5203` | `root.erase("constraint_edits") / ("parameter_model") / ("atlas_packs")` — the erase-on-empty idiom |
| `src/editor/project.cpp:5825` | `bool validate_project_for_save(` |
| `src/editor/project.cpp:7762-7797` | `rebase_project_paths`, whole body |
| `src/editor/project.cpp:7953` | `project.preserved_root = document.root;` |
| `src/editor/project.cpp:8317 / :8327` | `save_project` / its `rebase_project_paths` call |
| `src/editor/session.cpp:2041 / :2046` | the "five rebased fields" comment / the history rebase call |
| `include/marrow/editor/psd_import.hpp:10-21 / :30-37 / :48-62 / :69` | `PsdImportedLayer` / `PsdImportOptions` / `PsdImportResult` / `import_psd_to_runtime_bundle` |
| `src/editor/psd_import.cpp:729-796` | the `active_groups` hierarchy walk; `:734` and `:796` its two rejections |
| `src/editor/psd_import.cpp:814-826` | `parse_psd_document`'s document-global slot dedup (there is NO `assign_slot_names` symbol -- Task 0) |
| `src/editor/psd_import.cpp:923-928` | `effective_existing_skeleton_path`'s fallback |
| `src/editor/psd_import.cpp:1041` | `root->erase("skins");` |
| `src/editor/psd_import.cpp:1068-1097` | `write_imported_layers`; `:1076` the `remove_all`; `:1080-1082` the per-layer path |
| `src/editor/psd_import.cpp:1142-1158` | `populate_result_metadata` |
| `src/editor/psd_import.cpp:1190-1195 / :1197` | `PsdImportError::format` / `import_psd_to_runtime_bundle` |
| `src/samples/psd_import_smoke.cpp:21-22 / :27-33 / :153 / :266 / :323-385` | default paths / `expect` / `validate_initial_import` / `validate_reimport` / `main` |
| `src/samples/editor_project_smoke.cpp:6293-6310` | the `mar172_probe` passthrough case |
| `src/samples/editor_project_smoke.cpp:13025-13044` | `ScopedRenameCallback` |
| `src/samples/editor_project_smoke.cpp:17948-17951` | MAR-186's registration — MAR-188's insertion point |
| `CMakeLists.txt:499-521 / :812-819 / :850-858` | `marrow_editor` sources / `marrow_project_smoke` / `marrow_psd_import_smoke` |
| `src/runtime/json.cpp:962 / :968-983 / :985-1015` | the `"<path>: <message>"` concatenation / `require_type` / `require_member` |
| `src/runtime/skeleton_parse.cpp:4804-4822 / :4926-4936` | `build_default_skin` / `parse_skins` |

**Task 0 re-anchor result, measured at `bb3652f`.** The table above was correct at
`687ed4f` and is **left as measured** — a historical measurement is not edited to
match today's tree. What follows is the re-anchored pointer for the implementer.
MAR-187 (`bb3652f`, +6548 lines) moved four of these; one was never valid.

| §A.3 anchor | At `bb3652f` | Severity |
| --- | --- | --- |
| `psd_import.cpp:814-826` "`assign_slot_names`" | **Symbol does not exist repo-wide.** Enclosing function is `parse_psd_document` (`:625`); lines and behaviour correct | **Blocking** — fixed, E9 |
| `editor_project_smoke.cpp:17948-17951` (MAR-186 registration) | **`:19985-19988`** (+2037). MAR-187's own two registrations follow at `:19989-19996`. **MAR-188 registers after `:19996`** | Merge fact |
| `editor_project_smoke.cpp:13025-13044` `ScopedRenameCallback` | class starts **`:13034`** (+9); `:13025` is `TemporaryDirectory& operator=` | Hint drift |
| `CMakeLists.txt:812-819` / `:850-858` | `add_executable(marrow_project_smoke` at **`:814`**, `marrow_psd_import_smoke` at **`:852`** (both +2). `add_library(marrow_editor …)` is now **`:499-523`** — MAR-187 added `problems_model.cpp` (`:512`) and `safe_fix.cpp` (`:513`) | Hint drift |
| `AGENTS.md:2337` / `:2348` (design §1.4, E5) | **`:2719`** / **`:2730`** (+382). Both were correct at `687ed4f`, verified against that blob | Hint drift |
| §B I8's `project.cpp:7773` | `reference.is_absolute()` is at **`:7774`**; the shared lambda spans `:7772-7780` | Hint drift |
| §A.3's `psd_import.cpp:1080-1082` "per-layer path" | assignment is at **`:1085-1086`**; `populate_result_metadata` starts at **`:1135`** (cited `:1142-1158` is its layer-copy loop — right construct) | Hint drift |

**Everything else re-resolved by symbol**, including every `project.hpp` anchor,
every `project.cpp` anchor, design §1.2's ten serialization anchors, `session.cpp`,
all of `psd_import.hpp`, `psd_import.cpp`, `psd_import_smoke.cpp`, `json.cpp`,
`skeleton_parse.cpp` and `editing-gap-analysis.md:430`. **MAR-187 touched none of
this story's core files**: `git diff --stat 687ed4f bb3652f` over `project.cpp`,
`project.hpp`, `psd_import.{hpp,cpp}`, `session.cpp`, `json.cpp`,
`skeleton_parse.cpp` and `psd_import_smoke.cpp` is **empty**. The only two real
merge points are `CMakeLists.txt` (+3 additive source lines) and
`editor_project_smoke.cpp`.

**The plan's own Task 0.1 worked example ships two checks that print `STALE` at
`bb3652f`** — `CMakeLists.txt 850` and `editor_project_smoke.cpp 17948`. Left
uncorrected on purpose: it is a worked example of a checker, and a checker whose
sample output is all `OK` teaches nothing.

---

## §B. Inversion register

Every entry: the mutation, the **predicted failure text**, the case that fails,
and why nothing earlier catches it first. Attribution is **by run order**. The
mechanism column of design §7 is the argument; this is the checklist.

| # | Mutation | Predicted first failure | Uniquely attributed to | Why nothing earlier catches it |
| --- | --- | --- | --- | --- |
| **I1** | Delete the `parse_import_sources` call from `parse_editor_metadata` | `P2: provenance must survive a round trip through the typed field (source_path: expected art/hero.psd, got <empty>)` | P2 | P1 asserts *absence*; a loader that parses nothing still produces absence correctly. Every text-level assertion still passes because `preserved_root` re-emits the key |
| **I2** | Remove the `editor_object["import_sources"] = …` assignment, keep the parse | `P3: an in-memory provenance edit must survive serialization (source_path: expected art/other.psd, got art/hero.psd)` | P3 | P2 authors and reads back the *same* value, which the preserved copy reproduces exactly. Only an edit before serialization separates them |
| **I3** | Remove the `editor_object.erase("import_sources")` else-branch | `P4(a): clearing provenance must remove the key (serialization still contains "import_sources")` | P4 | P1-P3 never clear the field |
| **I4** | Serialise `psd == nullopt` as `"import_sources": {}` | `P4(b): an import_sources with no psd must serialise as an absent key` | P4(b) | P4(a) clears the **outer** optional, which the else-branch still handles |
| **I5** | Sixth family rebases `source_path` only | `P5: Save As must rebase layers_directory (expected art/hero_layers, got hero_layers)` | P5, `layers_directory` clause | Nothing in the tree reads `layers_directory`; the project still opens, the skeleton still resolves, and P2/P3/P4 never save across directories |
| **I6** | Sixth family rebases `layers_directory` only | `P5: Save As must rebase source_path (expected art/hero.psd, got hero.psd)` | P5, `source_path` clause | The mirror of I5. Run separately: one inversion cannot distinguish "family added" from "family complete" |
| **I7** | Sixth family reads `result.editor_metadata…` instead of `project.editor_metadata…` | `P5: the rebased source_path must resolve to the same absolute file (expected <old>/art/hero.psd, got <new>/art/hero.psd)` | P5, `weakly_canonical` clause | The string clauses can still look plausible; only resolution identity sees the double-rebase. This is why P5 has both |
| **I8** | Delete `reference.is_absolute()` from the shared lambda (`project.cpp:7773`) | `P5: the rebased source_path must resolve to the same absolute file` — **P5 fails before P6** | P5 by run order; P6 is the case it exists for | Mutates code the five pre-existing families share, so MAR-180's S2/S3 also fail. Recorded rather than hidden; attribution is by run order and P5 runs first. Re-run with P5 `(void)`-neutered to observe P6 directly |
| **I9** | Give `PsdReimportPlanOptions` explicit output paths and pass the project's real `layers_directory` | `Q8: planning must not change the project directory (missing: art/hero_layers/body.png, art/hero_layers/arm_l.png, …)` | Q8, clause 5 | The plan's content is byte-identical; classification never reads the destination. Only the recursive directory listing sees `remove_all` (`psd_import.cpp:1076`). **This is the inversion that justifies clause 5** |
| **I10** | Leave `existing_skeleton_path` unset, taking the `psd_import.cpp:923-928` fallback | `Q1: an Updated layer must carry proposed targets (torso\|body: proposed_bone_name expected torso, got <empty>)` | Q1, proposed-target clause | Added/Missing/Updated **kinds** are computed from the candidate alone and are unchanged. Only the targets, which come from the staged merge, move |
| **I11** | Fall back to `slot_name` when the identity misses | `Q3: a group move must produce one Added and one Missing (unexpected Updated: body)` | Q3 | In Q1 and Q2 every slot name equals its layer name, so the fallback is indistinguishable from the identity |
| **I12** | Add a Levenshtein-threshold matcher before classification | `Q2: a rename must never be inferred (expected Added torso\|arm_left + Missing torso\|arm_l, got Updated torso\|arm_left)` | Q2 | Q1's names are unrelated; Q3's differ in group, not spelling. Only two names four edits apart trip a threshold |
| **I13** | Remove `\` / `\|` escaping from the identity builder | `Q5: escaped identities must not collide (expected 2 entries, got 1; missing: a\|b\\\|c)` | Q5 | No other case's fixture contains `\|` or `\\`. Design §2.7 records that the collision **is** constructible here, unlike MAR-186's families |
| **I14** | Pair candidate duplicates positionally instead of refusing | `Q6: duplicate candidate identities must be refused (expected message "psd layers must have unique group and name identities", got a successful plan)` | Q6 | Q1-Q5 have no duplicate |
| **I15** | `PsdPlannedLayer::preserve{false}` | `Q9: a Missing layer must default to preservation (torso\|arm_l: preserve expected true, got false)` | Q9 | Kinds, counts and targets are all unchanged; nothing else reads the flag |
| **I16** | Emit the plan in candidate-record order | `Q10: two independent plans must be element-wise identical in order (index 0: expected shadow, got torso\|body)` | Q10, and Q1's ordered clause | Q1's set-difference clause is order-free and passes. Both cases assert ordered lists for exactly this reason |
| **I17** | Accumulate `added_count` in a parallel loop that misses one arm | `Q1: plan counts must match the classified list (added_count expected 1, got 0)` | Q1, redundant count clause | Every list clause passes. Recorded as the demonstration that the count clause is redundant but not decorative |
| **I18** | Synthesiser writes the `lsct` 3 divider before the group's children | `Q0: the synthesiser must reproduce the checked-in fixture (import failed: PSD folder end marker appeared without an open folder.)` | Q0 | Q0 runs first, so every downstream case is blocked rather than misattributed. Makes design §2.8's ordering property observable instead of assumed |
| **I19** | Drop `bone_name` from `make_psd_provenance`'s copy | `P2: provenance must survive a round trip through the typed field (torso\|body: bone expected torso, got <empty>)` — via the provenance Q1 stores | Q1's full-tuple clause | The six-assignment copy is the compiler-blind class of design §1.3. One inversion is run; §B.1 records the other five as covered by the same clause |

### B.1 Deliberately uninverted — record verbatim in `AGENTS.md`

- **P8(a), (b), (f), (g)** — the `require_type`-sourced rejections. Their messages
  come from `src/runtime/json.cpp:968-983`, shared by every parser in the tree;
  inverting them mutates the JSON layer. The six domain rejections (d, e, h, i, j,
  k) **are** inverted, one each, as part of Task 1.
- **Five of `make_psd_provenance`'s six assignments.** I19 inverts `bone_name`;
  `layer_name`, `group_path`, `slot_name`, `attachment_name` and `image_file` are
  covered by the identical clause in Q1's full-tuple comparison. Inverting each
  would be five inversions proving one thing.
- **`session.cpp:2041`'s comment.** A comment cannot be inverted. Verified by
  reading and by Task 7's `grep`.
- **`project.hpp:1033` and `:1052-1054`.** Same reason.
- **The `staging_root` non-empty rejection.** It is a guard against a caller
  mistake, not a behaviour any case depends on; it is asserted directly (Task 4)
  and not inverted.

---

## Task 0 — Measure, before any code

### 0.1 Re-anchor gate — every cited line, before anything else

**Blocking.** For **every** anchor in §A.3 and every `file:line` in the design,
resolve the **symbol** and confirm the cited line still names the described
construct. "A line exists" is not the check.

```bash
# Read the committed blob, not the worktree -- other agents are editing tracked files.
check() { # check <file> <line> <expected-substring>
  local got; got=$(git show "HEAD:$1" | sed -n "$2p")
  case "$got" in *"$3"*) printf 'OK    %s:%s\n' "$1" "$2";;
                     *) printf 'STALE %s:%s  want[%s]  got[%s]\n' "$1" "$2" "$3" "$got";; esac
}
check include/marrow/editor/project.hpp 575  'struct ProjectMetadata'
check src/editor/project.cpp             877  'parse_editor_metadata'
check src/editor/project.cpp            5023  'preserved_object_member(project.preserved_root, "editor")'
check src/editor/project.cpp            5089  'root["editor"] = make_object_value'
check src/editor/project.cpp            7762  'ProjectData rebase_project_paths('
check src/editor/project.cpp            7953  'project.preserved_root = document.root;'
check src/editor/project.cpp            8327  'rebase_project_paths(project, path)'
check src/editor/psd_import.cpp         1076  'remove_all(extracted_layers_directory'
check src/samples/editor_project_smoke.cpp 17948 'validate_mar186_project_diagnostics'
check CMakeLists.txt                     850  'add_executable(marrow_psd_import_smoke'
# ... repeat for EVERY anchor in §A.3. The list above is a worked example, not the set.
```

Every `STALE` line is a **blocking finding**: record it in the document-errors
table with the symbol's real location, then update the design and this plan.
Never adjust silently. MAR-185's gate found 16, MAR-186's 3, MAR-187's 4 — two of
MAR-186's were case specifications that would have failed on correct code.

Widen the checker to the whole file and to `git show HEAD:` blobs, per MAR-185
D25 — a section-scoped checker missed a stale anchor two sections away and a
worktree-scoped one reads another agent's edits.

### 0.2 Baseline build and inventory

```bash
cmake -S . -B build
find build/CMakeFiles -name '*.o' -delete
cmake --build build -j8 2>&1 | tee /tmp/mar188-baseline-build.log
grep -c warning: /tmp/mar188-baseline-build.log          # the pristine warning count (A8)
ctest --test-dir build -N | tail -1                       # expect: Total Tests: 22
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_psd_import_smoke assets/fixtures/psd_import_sample.psd \
                                assets/fixtures/psd_import_sample_reimport.psd
./build/marrow_agent_dispatch_smoke | grep -c '\[ OK \]'
git status --porcelain > /tmp/mar188-status-before.txt     # rule 6/14
```

### 0.3 The P1 baseline (A7)

Capture, **before any code**, the exact string P1 will `cmp` against:

```bash
# A five-line probe linking libmarrow_editor: load_project(path) -> serialize_project -> stdout.
./scratch/serialize_probe assets/fixtures/player_idle.marrow > /tmp/mar188-p1-baseline.txt
wc -c /tmp/mar188-p1-baseline.txt
```

If this file is not captured now, P1 degenerates into "it contains no
`import_sources`", which I3 does not fail.

### 0.4 A1-A4 gate — the passthrough, the staleness, and the two rebase arms

**The most consequential step in Task 0.** Build the design §0.4 probe and run it.
Expected, exactly:

```
P1 import_sources survives load+serialize: YES
P1 art/hero.psd survives: YES
P1 group_path survives: YES
P2 art/hero.psd UNCHANGED after rebase (stale): YES
P2b skeleton after parent-dir rebase: fixtures/player_idle.mskl
P3 create_minimal_project preserved_root empty: YES
```

**If `P1 … YES` does not reproduce, stop and re-plan P2/P3/P4.** The whole shape
of the AC1 cases depends on the passthrough being real; if it is not, P3 and P4
lose their reason to exist and P2 may be written more cheaply. If `P2 … stale:
YES` does not reproduce, AC2 has no detector and §2.5 needs rethinking.

### 0.5 A5/A6 gate — the fixture identities and the failure shape

Build the design §0.4 P4/P5 probe. Assert the three layer rows and two bone rows
character for character against design §0.4, and that a non-PSD input yields
`PSD files must begin with the 8BPS signature.` with **no** skeleton written.
These strings are hard-coded into Q0, Q1 and Q7; a drift here is a silent
mis-specification of three cases.

### 0.6 A9 gate — the registry non-effect baseline

```bash
grep -rn '!= 66U' src/ | wc -l            # 11
grep -n 'std::array<OperationExpectation' src/samples/agent_dispatch_smoke.cpp   # :42, NOT :39 (E2)
grep -n '== 66' tools/mcp/test_client.py  # :53, :55
```

Record the eleven guard positions by file:line. MAR-188 adds no operation, so the
final proof is a **zero-line diff** (Task 7), not an assertion.

### 0.7 A8 gate — prove the compiler is not silent for the new enum

After Task 4 creates `PsdLayerChangeKind`, add a throwaway fourth value, then:

```bash
find build/CMakeFiles -name '*.o' -delete
cmake --build build -j8 2>&1 | grep Wswitch | tee /tmp/mar188-wswitch.txt
wc -l /tmp/mar188-wswitch.txt
```

Read the list; every site is a switch MAR-188 must handle. Then remove the value,
delete objects, rebuild, and confirm the count returns to the 0.2 baseline.

**Both "all"s are load-bearing**: delete **all** objects, build **all** targets. A
single-target probe understates its own checklist — MAR-185's measured 24 instead
of 25. **Check the number against arithmetic**, not only against a second build.
And note: a missed arm **warns and does not stop the build**; GCC needs `-Wall`
for even that. `CMakeLists.txt` carries no warning flags; Clang enables `-Wswitch`
by default.

**Then sweep the compiler-blind class by hand.** Any `if/else` chain or
fixed-length per-family list over `PsdLayerChangeKind` or over the rebase families
is invisible to the compiler. There is exactly one such list in this story —
`rebase_project_paths` — and §B I5/I6 are its only detector.

### 0.8 A10 gate — what `format-spec.md` already documents

**MEASURED IN TASK 0. Decided: `format-spec.md` IS touched.** `format-spec.md:685-707`
carries an `### editor` section listing `$.editor`'s common fields, including
`preview_skins` (`:691`) and `export_directory` (`:692`), plus `viewport`'s
`onion_skin` (`:702`). By this section's own rule `import_sources` joins them, so
**Task 9.2 is real work and must not be recorded as a no-op.**

```bash
grep -n 'editor' docs/root1/format-spec.md | head -40
grep -n 'onion_skin\|export_directory\|preview_skins' docs/root1/format-spec.md
```

If `$.editor`'s optional keys are documented there, `import_sources` joins them
(Task 9). If they are not, the file is **not touched** and that is recorded, not
improvised.

### 0.9 The MAR-187 landing gate

```bash
git log --oneline -5
ls docs/superpowers/{specs,plans} | grep 187
# MAR-187's new files. Prospective and NOT stable -- re-derive, do not trust the list.
ls -la include/marrow/editor/{safe_fix,problems_model}.hpp \
       src/editor/{safe_fix,problems_model,shell_problems}.cpp 2>&1
grep -n 'safe_fix\|problems_model\|shell_problems' CMakeLists.txt
grep -n 'validate_mar187' src/samples/editor_project_smoke.cpp
# diagnostics.hpp must still be UNMODIFIED (E3). If it is not, MAR-187 changed route.
git diff --stat -- include/marrow/editor/diagnostics.hpp
```

- **Not landed** (the state at `687ed4f`): register MAR-188's project case
  directly after MAR-186's at `editor_project_smoke.cpp:17948-17951`.
- **Landed**: re-derive that registration point and the
  `add_library(marrow_editor …)` hunk, both of which MAR-187 moves. Its new files
  are additive and collide with nothing MAR-188 writes; recognise them so they are
  not mistaken for stray work and are **not** reverted or staged. Nothing else in
  MAR-188 changes — design §0.2.
- **Landed with a different file set than design §0.2 lists**: that list is
  prospective. Record the actual set in the document-errors table and move on; it
  is a merge fact, not a blocking finding, because MAR-188 includes none of those
  headers.

### 0.10 The inversion harness

```bash
# /tmp/mar188-rebuild.sh -- delete the objects, then build. NEVER touch.
set -e
find build/CMakeFiles -name '*.o' -delete
cmake --build build -j8
```

Keep pristine copies of every file this story edits, outside the worktree, and
verify every restore with `cmp` (rule 6).

### Task 0 exit criteria

- [ ] Every §A.3 anchor resolves by symbol, or is recorded as a blocking finding.
- [ ] A1-A4 reproduce exactly (0.4). A5/A6 reproduce character for character (0.5).
- [ ] `/tmp/mar188-p1-baseline.txt` exists and is non-empty.
- [ ] Pristine warning count, `ctest -N` = 22, `[ OK ]` count, registry 66 and its
      four site families all recorded.
- [ ] `format-spec.md`'s `$.editor` coverage decided (0.8).
- [ ] MAR-187's landing state recorded and the registration point derived (0.9).
- [ ] `git status --porcelain` captured; no tracked file touched by this task.

---

## Task 1 — The provenance vocabulary, parser and serializer

**Files:** `include/marrow/editor/project.hpp`, `src/editor/project.cpp`,
`src/samples/editor_project_smoke.cpp`.

### 1.1 Test first — P1, P2, P3, P4, P8

Write `validate_mar188_psd_provenance(const ProjectLoadResult&, const std::filesystem::path&)`
in `editor_project_smoke.cpp` and register it at the 0.9 point. Cases exactly as
design §6.2. Every failure message begins `"P<n>: "`.

The three that carry the story:

- **P2 asserts the typed struct**, never a substring of `serialize_project()`.
  Design §0.4 P1 is why: a text assertion passes on an empty commit. Assert
  `source_path`, `layers_directory`, and the full sorted tuple list with a
  set-difference message.
- **P3** mutates `source_path` in memory before serializing. It is the only case
  that can see I2.
- **P8** authors eleven malformed documents, asserts `loaded.error->format()`
  contains both the design §2.4 message **and** the JSON path, and asserts a
  control project's `serialize_project()` is byte-identical in the same scope.

### 1.2 Watch them fail

Build and run. **Record the actual failure text of each of P1-P4 and all eleven
P8 arms.** A case whose pre-implementation failure was not observed is a case
that has not been proven falsifiable.

**P1 is expected to PASS before implementation** — `player_idle.marrow` has no
provenance and the field does not exist. Task 0 measured exactly that: the
baseline is 6111 bytes and contains no `import_sources`.

**P1 is a COMPATIBILITY WITNESS, not a gate, and it has no story-owned inversion.**
The earlier claim that it "earns its place through I3" was wrong — §7 and §B both
attribute I3 to **P4** — and design §0.3.1 now records this as the class's first
self-inflicted instance (E8). Record P1's pre-implementation pass as what it is:
evidence that a pre-MAR-188 project's bytes do not move, and evidence of nothing
else. Do not read "P1 green" as vindication at any later point in the story.

### 1.3 Implement

1. `PsdLayerProvenance`, `PsdImportProvenance`, `ProjectImportSources` above
   `ProjectMetadata` (`project.hpp:575`); the `std::optional<ProjectImportSources>
   import_sources;` member inside it.
2. `parse_import_sources` beside `parse_editor_metadata` (`project.cpp:877`),
   copying the shape of `editor.viewport.onion_skin` (`:972-999`). All eleven
   rejections, messages exactly per design §2.4.
3. `build_import_sources_value` beside `build_atlas_pack_definitions_value`
   (`:4782`). Paths via `.generic_string()`, matching `:5014`, `:5018`, `:5034`.
4. The assign-or-erase block in `build_project_value`, **before**
   `root["editor"] = …` (`:5089`), following the `root.erase(...)` idiom at
   `:5192` / `:5197` / `:5203`. Both levels: `import_sources` absent, and
   `import_sources` present with `psd` absent, both serialise as an absent key.

### 1.4 Verify + inversions

```bash
bash /tmp/mar188-rebuild.sh
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

Run **I1, I2, I3, I4**, and one inversion per domain rejection (P8 d, e, h, i, j,
k). Each: mutate → delete objects → build → run → record the **actual** text →
restore → delete objects → build → re-run → `cmp` against the pristine copy.

---

## Task 2 — The sixth rebase family

**Files:** `src/editor/project.cpp`, `include/marrow/editor/project.hpp`,
`src/editor/session.cpp`, `src/samples/editor_project_smoke.cpp`.

### 2.1 Test first — P5, P6, P7

Design §6.2. All three route through **`save_project` → `load_project(path)`**
(rule 10). P5 saves into the **parent** directory (A3: that is the only
destination that keeps references relative). P6 places the absolute `.psd`
**inside** the Save As destination so a relative form is always representable and
temp-directory layout cannot mask the case — MAR-180's S3 is the precedent. P7
saves into a sibling and asserts both fields come back absolute and still resolve.

P5 asserts **both fields by name** and **both `weakly_canonical` identities**.
Design §1.3: the compiler contributes nothing to a fixed-length call list, so the
assertion is the only detector.

### 2.2 Watch them fail

Expected: P5 fails on `source_path` (the field is copied verbatim — A2), P6
passes (verbatim copy happens to be right for an absolute path — **record this**,
it is why I8 exists), P7 fails.

### 2.3 Implement

The block from design §2.5, after the `atlas_pack_definitions` loop
(`project.cpp:7792-7794`) and before `return result;`. Reads from `project`,
writes to `result`.

Then the three prose amendments:

1. `project.hpp:1033` — "Five families" → six, naming the new one.
2. `project.hpp:1052-1054` — rewrite the MAR-188 paragraph to say it **is**
   registered; **keep** the `preserved_root` limitation sentence, which is still
   true (design §1.4 point 3).
3. `src/editor/session.cpp:2041` — "the five rebased fields" → six.

Item 3 lives in another file, in a comment. `grep -rn 'five' src/editor/session.cpp`
is the sweep that finds it; Task 7 re-runs it.

### 2.4 Verify + inversions

Run **I5, I6, I7, I8**, separately. For I8, first record that P5 fails, then
re-run with P5's block `(void)`-neutered (rule 4) to observe P6 directly, and
record that MAR-180's S2/S3 also fail — the mutation is in shared code and hiding
that would be a false claim of uniqueness.

---

## Task 3 — The PSD synthesiser, and its gate

**File:** `src/samples/psd_import_smoke.cpp`.

**Do this before any planning code.** Eight cases depend on it (design §10 risk 2).

### 3.1 The synthesiser

A file-static helper emitting 8BPS **version 1**, RGB, 8-bit, **raw**-compression
documents from a declarative tree:

```cpp
struct SynthLayer { std::vector<std::string> group_path; std::string name;
                    int left, top, width, height; };
// Writes header, colour-mode, image-resource and layer-and-mask sections; emits
// each group as an lsct 1 header BEFORE its children and an lsct 3 divider AFTER
// them, which is the order psd_import.cpp:729-796 accepts and the order both
// checked-in fixtures use (design 2.8).
bool write_synthetic_psd(const std::filesystem::path&, int canvas_w, int canvas_h,
                         const std::vector<SynthLayer>&);
```

Raw compression only — the PackBits decoder (`psd_import.cpp:251-274`) is the
parser's, not the writer's problem, and a synthesiser that also implements RLE is
a second thing to debug.

### 3.2 Q0 — the gate, four clauses, plus Q0b

**Task 0 found the single-clause Q0 too loose and it was tightened.** The report
is a lossy projection of the file — no pixels, no channel count, no depth, no
colour mode — so a wrong pixel encoding reproduces the whole report while every
extracted PNG is wrong. Design §6.3 carries the full statement. The four clauses:

1. **Report** — three layers `shadow` / `arm_l` / `body` with those groups, slots,
   attachments, bones, **`image_file` names** and boxes; two bones `root` and
   `torso(4,12)`. `image_file` is named explicitly: an omitted field in an
   assertion list is the same defect shape as an omitted term in a fixed-length
   sum (§1.3).
2. **Pixels** — the extracted PNG for at least one layer decodes to the exact RGBA
   written, at a known coordinate. Nothing in the report sees this.
3. **Header** — signature, version, channel count, bit depth and colour mode equal
   the checked-in fixture's own header bytes, **read at run time** from
   `psd_import_sample.psd` rather than hard-coded. Record how close the
   synthesised size comes to the fixture's **19,636** bytes.
4. **Q0b — the duplicate-name branch.** `parse_psd_document:814-826` takes
   `join_path(group_path, name)` (separator `/`) only when the document-global
   census exceeds 1, and Q0's document has no duplicates — so that branch is
   **ungated** while Q5 and Q6 are the only cases that live in it. Q0b synthesises
   two layers named `body`, one in `torso` and one at root, and asserts the slot
   names are exactly `torso/body` and `body`.

Q0 runs **first** in `validate_mar188_reimport_planning`, so a synthesiser
regression is attributed to Q0 rather than to whichever case notices. Q0b runs
immediately after it, for the same reason.

### 3.3 Verify + inversion

`./build/marrow_psd_import_smoke <the two fixtures>`. Run **I18**: reorder the
divider emission and confirm Q0 fails with
`"PSD folder end marker appeared without an open folder."`

---

## Task 4 — The planning API, staging, and the failure paths

**Files:** `include/marrow/editor/psd_reimport_plan.hpp` (new),
`src/editor/psd_reimport_plan.cpp` (new), `CMakeLists.txt`,
`src/samples/psd_import_smoke.cpp`.

### 4.1 Test first — Q7, Q8, Q11

- **Q7** — a text file as the candidate: `!plan`, `format()` contains
  `"PSD files must begin with the 8BPS signature."` **and** the candidate path,
  `staged_skeleton_path` does not exist, and Q8 clauses 1-5 hold.
- **Q8** — the six clauses of design §2.10 on a successful plan. Clause 5 is a
  **full sorted recursive listing with sizes**, set-differenced by name so a
  created file fails as loudly as a deleted one.
- **Q11** — a project with `import_sources == nullopt`: every candidate layer
  `Added`, zero `Missing`, zero `Updated`, `current_*` empty everywhere.

Q8 needs a real project on disk. Build one: import the fixture into a temp
directory, `create_minimal_project` (`project.hpp:1007-1023`) pointing at the
emitted `.mskl` and `.matl`, `save_project`, then `load_project(path)` (rule 10).

### 4.2 Watch them fail

Everything fails to compile first (the header does not exist). Then, with empty
implementations, record each case's actual text.

### 4.3 Implement

1. The header, exactly design §2.6. `PsdReimportPlanError::format()` →
   `"PSD reimport planning failed for '<path>': <message>"`, mirroring
   `PsdImportError::format` (`psd_import.cpp:1190-1195`).
2. `plan_psd_reimport`: validate `psd_path` and `staging_root` (non-empty;
   `staging_root` empty-or-absent), **derive** all three staged paths from
   `staging_root` — the caller can never name one (design §2.6, the structural
   answer to `remove_all` at `psd_import.cpp:1076`) — set
   `existing_skeleton_path = project.resolved_skeleton_path()` **explicitly**,
   defusing the `psd_import.cpp:923-928` fallback, run the importer, and return the
   importer's error verbatim on failure.
3. `make_psd_provenance`: six assignments per layer; `image_file` from
   `extracted_image_path.filename().generic_string()`; `source_path` and
   `layers_directory` relativised against `project_path` through the same house
   rule the rebase uses.
4. `CMakeLists.txt`: **one** line, `src/editor/psd_reimport_plan.cpp`, in
   `add_library(marrow_editor …)` (`:499-521`). No new target, no `add_test`.

### 4.4 Verify + inversions

Run **I9** — the inversion that justifies Q8 clause 5. Give the options struct
explicit output paths, point the layers directory at the project's real one, and
confirm the plan's content is unchanged while clause 5 fails. Then **restore the
API shape**, not just the test.

---

## Task 5 — Classification

**Files:** `src/editor/psd_reimport_plan.cpp`, `src/samples/psd_import_smoke.cpp`.

### 5.1 Test first — Q1, Q2, Q3, Q4, Q9, Q10

Design §6.3. Q1's full ordered `(identity, kind)` list is
`[("shadow", Updated), ("torso|arm_l", Missing), ("torso|body", Updated), ("torso|hand_r", Added)]`,
asserted by set difference **and** in order, then the three counts as a redundant
clause, then every Updated/Added entry's four `proposed_*` strings non-empty.

Q4 asserts `group_path` **element-wise** (`{"torso","upper"}`), not the joined
string — a joined-string assertion cannot distinguish depth-2 nesting from a layer
literally named `upper/hand`. This is the first coverage depth ≥ 2 has ever had.

### 5.2 Watch them fail

Record each. Q9 and Q10 depend on Q1's plan; confirm they fail for their own
reason and not because Q1's plan is empty — run them against a hand-built plan
first if necessary.

### 5.3 Implement

Union the provenance identities and the candidate identities, classify per design
§2.8, **derive** the three counts from the vector, sort ascending by identity.
No normalisation, no similarity matching, no fallback to `slot_name`.

### 5.4 Verify + inversions

Run **I10, I11, I12, I15, I16, I17, I19**. I12 is the AC4 inversion: add a real
Levenshtein threshold, confirm Q2 fails and Q1/Q3 pass, then remove it.

---

## Task 6 — Identity escaping and duplicates

**Files:** `src/editor/psd_reimport_plan.cpp`, `src/editor/project.cpp`,
`src/samples/psd_import_smoke.cpp`, `src/samples/editor_project_smoke.cpp`.

### 6.1 Test first — Q5, Q6 (and P8(k), if it was deferred from Task 1)

- **Q5** — group `["a|b"]` layer `c` against group `["a"]` layer `b|c`: two
  entries, distinct identities, both listed.
- **Q6** — two `body` layers in `torso`: `!plan`, message
  `"psd layers must have unique group and name identities"` plus the colliding
  identity, and Q8 clauses 1-5.
- **P8(k)** — stored duplicates rejected at **load** with
  `"psd layer identities must be unique"` at `$.editor.import_sources.psd.layers[i]`.

### 6.2 Implement

`escape_identity_token`: `\` → `\\`, then `|` → `\|`; join with `|`. Duplicate
detection over the escaped identity in both the loader and the planner.

### 6.3 Verify + inversions

Run **I13** and **I14**. Design §2.7 records why the collision is constructible
here and was not in MAR-186 — every token is a free Photoshop string and the arity
is variable — so Q5 is a real case and not a ritual.

---

## Task 7 — Prove the scope did not leak

Every sweep below is quoted (rule 8) and its `$?` echoed. Every one names what it
does **not** reach (rule 9).

```bash
# 1. The importer is untouched. Expect an EMPTY diff.
git diff --stat -- src/editor/psd_import.cpp include/marrow/editor/psd_import.hpp; echo "rc=$?"

# 2. The registry did not move. Expect an EMPTY diff and ZERO lines.
git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_handlers_*.cpp tools/
git diff -U0 -- src/ tools/ CMakeLists.txt | grep -E '^[+-]' | grep -E '\b(64|65|66|67)\b' | wc -l

# 3. The rebase families are SIX, and every prose site agrees. Read the output.
sed -n '/ProjectData rebase_project_paths(/,/^}/p' src/editor/project.cpp | grep -n 'rebase('
grep -rn 'five\|Five' include/marrow/editor/project.hpp src/editor/session.cpp

# 4. The planner is UI-free: no shell, sokol or imgui include anywhere in it.
grep -n 'include' src/editor/psd_reimport_plan.cpp include/marrow/editor/psd_reimport_plan.hpp \
  | grep -Ei 'shell|sokol|imgui|widgets'; echo "rc=$? (1 == no hits, and grep RAN)"

# 5. plan_psd_reimport has no production caller and psd_import.cpp has no new one.
grep -rn --include='*.cpp' --include='*.hpp' 'plan_psd_reimport\|make_psd_provenance' src/ include/

# 6. No path field escaped the two rebased ones.
grep -n 'std::filesystem::path' include/marrow/editor/project.hpp | sed -n '/Psd/,+6p'

# 7. Nobody else's work was reverted.
diff /tmp/mar188-status-before.txt <(git status --porcelain)
```

Sweep 4's scope: it reaches the two new files only. It does **not** reach a
transitively-included shell header, which is why sweep 4 is paired with the fact
that `psd_reimport_plan.cpp` compiles into `marrow_editor`, a target that does not
link the shell — a shell include there fails to compile rather than slipping
through. (MAR-186's D-note records that `#include "shell_state.hpp"` in
`diagnostics.cpp` in fact **fails to compile**; the grep is still the right
mechanism because a dependency-free shell header would slip through silently.)

Sweep 3's scope: it reads the function body and two files of prose. It does
**not** reach `docs/`, which Task 9 covers separately.

---

## Task 8 — Full verification

```bash
rm -rf build && cmake -S . -B build && cmake --build build -j8 2>&1 | tee /tmp/mar188-final.log
grep -c warning: /tmp/mar188-final.log      # compare to the Task 0.2 baseline
cmake --build build --target marrow_verify_third_party
cmake --build build --target marrow_constraint_warning_check

# Project layer -- P1-P8, inside the standing invocation
./build/marrow_project_smoke assets/fixtures/player_idle.marrow

# PSD layer -- Q0-Q11
./build/marrow_psd_import_smoke assets/fixtures/psd_import_sample.psd \
                                assets/fixtures/psd_import_sample_reimport.psd

# Model layers -- unchanged by this story, run as regression witnesses
./build/marrow_unit_tests && ./build/marrow_windowing_tests && ./build/marrow_pen_input_tests
./build/marrow_preference_tests && ./build/marrow_selection_tests
./build/marrow_viewport_interaction_tests && ./build/marrow_timeline_model_tests
./build/marrow_timeline_graph_model_tests && ./build/marrow_agent_socket_tests

# Runtime and importers -- these exercise write_text_file/write_binary_file and the
# atlas-pack path families, the same reason MAR-180 listed them
./build/marrow_bootstrap && ./build/marrow_c_smoke && ./build/marrow_parameter_project_smoke
./build/marrow_atlas_packer_smoke && ./build/marrow_spine_import_smoke
./build/marrow_fixture_smoke   # both invocations

# Shell -- regression witness only; MAR-188 adds no shell code
MARROW_CONFIG_HOME=$(mktemp -d) ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2

# Agent / MCP -- regression witnesses; MAR-188 adds no operation
./build/marrow_agent_dispatch_smoke | grep -c '\[ OK \]'   # identical to Task 0.2

# CTest
ctest --test-dir build --output-on-failure                 # 22/22
ctest --test-dir build -L runtime && ctest --test-dir build -L editor
```

**Preference isolation.** Every `marrow_editor_shell` invocation sets
`MARROW_CONFIG_HOME` to a scratch directory. Check that
`$HOME/Library/Application Support/Marrow` does not exist, before and after. This
story does not touch the preference writer, but the check is cheap and the shared
`RenameCallback` seam (`atomic_file_write.hpp:22-24`) is process-global.

**Re-run every inversion against the from-scratch tree** and compare the messages
to the first-run recordings (rule 5).

### Full verification checklist

- [ ] Warning count identical to Task 0.2.
- [ ] `ctest -N` still 22; `ctest` 22/22.
- [ ] `[ OK ]` count identical to Task 0.2.
- [ ] Registry proof is a **zero-line diff**, not an assertion.
- [ ] Every I1-I19 re-run, restored, `cmp`-verified, message recorded.
- [ ] Every case's pre-implementation failure text recorded (P1 excepted, with its
      reason — §Task 1.2).
- [ ] `diff /tmp/mar188-status-before.txt <(git status --porcelain)` shows only
      this story's files.

---

## Task 9 — Documentation and commit

### 9.1 `AGENTS.md`

Add `## MAR-188 Plan Provenance-Aware PSD Reimports Validation Results` **above**
MAR-186's section (newest first), and one `## Current Validation` bullet naming
the two binaries and what they now cover.

The section must record, at minimum:

- The **MAR-180 discharge**, with MAR-180's wording quoted and design §1.4's
  three-point answer: first clause fully discharged, the precision MAR-180 could
  not state, and the last clause **not** discharged. Write that third point as
  **permanently open**, with the reason — `preserved_root` is defined as "whatever
  this code does not understand", so a rule quantified over "every relative path"
  is quantified over a set the program cannot enumerate — and say what closing it
  would take (a schema-strict loader, or declaring unparsed paths unsupported),
  neither of which is MAR-188's to take. Do **not** write it up as "six families,
  done".
- **The passthrough finding (E1)**, as a `## Repo facts that outlive their story`
  entry, not only inside this story's section: *"an unknown key under `$.editor`
  already round-trips through `preserved_root`; a round-trip test written as a
  text search over `serialize_project()` passes on unmodified code."* Measured, with
  the three-line chain (`project.cpp:7953` → `:5023` → `:5089`) and the note that
  `create_minimal_project` has an empty `preserved_root`, so the passthrough holds
  only for loaded projects. **This is the entry the next story will need and this
  story's section is not where they will look.**
- **The class E1 belongs to, named** — a second `## Repo facts` entry, or a
  `## Methodology hazards worth recording` one: **a gate that PASSES on unchanged
  code** is the mirror of the already-recorded **gate that FAILS on correct code**
  (MAR-186 D2, MAR-187's two case specifications). State the pair and that the
  passing direction is the more dangerous, because a failing gate gets
  investigated and a passing one does not — so the only way to find it is to run
  every case against the pristine tree before implementing, or to invert. Design
  §0.3.1 has the table to lift.
- **The Save-As rebase arms (A3)**, also as a repo fact: a Save As into a **parent**
  directory keeps references relative; into a **sibling** or a subdirectory it makes
  them absolute, because `make_project_relative_path` returns absolute rather than
  emit `../`. Measured both ways. Anyone writing a rebase case needs this before
  they choose a destination.
- The **document-errors table** (E1-E7 plus everything Task 0.1 found), each row
  naming the source, the claim and the measurement.
- The **inversion outcomes**, actual not predicted, including any that did not bite
  as written — replaced rather than waived, with the replacement's measurement.
- **B.1's deliberately-uninverted list, verbatim.**
- The §9 limitations, especially the **unverified Photoshop record order** and that
  `marrow_psd_import_smoke` is still not in CTest.

### 9.2 `docs/root1/`

- `fixtures.md:176-183` — a note that MAR-188's classification inputs are
  **synthesised in the smoke**, not checked in, and why (design §2.8).
- `format-spec.md` — `$.editor.import_sources` documented **iff** Task 0.8 found
  `$.editor`'s optional keys documented there. Otherwise untouched, and that is
  recorded.
- `editing-gap-analysis.md:430` — the MAR-188 row's "여섯 번째 family로 등록해야"
  becomes a statement of fact rather than an instruction.

### 9.3 The PRD

`.agents/tasks/prd-marrow-runtime.json` → MAR-188 `status: "done"`. Nothing else in
the file changes.

### 9.4 Commit

**One** commit. Korean subject and body. Trailers **contiguous** — a blank line
between them makes git parse only the first:

```
feat: MAR-188 PSD 재임포트 provenance 계획 수립

.marrow에 선택적 editor.import_sources.psd를 추가해 PSD 원본 경로와
group/layer 이름 기반 매핑을 프로젝트 상대 경로로 저장한다. Save As는
rebase_project_paths의 여섯 번째 family로 이를 함께 재기준화하며,
provenance가 없는 기존 프로젝트는 그대로 열린다.

UI 없는 plan_psd_reimport가 후보 PSD를 staging에서만 파싱·빌드해
added/updated/missing을 정확한 group/layer 이름으로 분류한다. rename은
추론하지 않고, 중복 identity는 저장 시점과 계획 시점 모두에서 거부하며,
missing은 기본적으로 보존된다.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: <session id>
```

Stage **only** this story's files. Other agents are editing tracked files
concurrently; `git add -A` would sweep their work into this commit.

---

## Risks

1. **The round-trip test that cannot fail.** Design §0.4 P1 / §10 risk 1. If P2 is
   written as a text search it passes on an empty commit and AC1 is unmeasured.
   Task 1.1 states the rule; Task 1.2's I1 run is where it is proven.
2. **The synthesiser is the schedule risk.** Eight cases depend on Task 3. If Q0
   cannot be made to pass, classification is blocked and the fallback —
   hand-authored binaries — is worse. Build it before any planning code.
3. **`remove_all` at `psd_import.cpp:1076`** destroys a directory. The API shape
   (design §2.6) is the mitigation; I9 is the proof. Do not add an overload that
   accepts output paths.
4. **Two rebased fields, no compiler help.** Design §1.3. I5 and I6 must be run
   separately; a single inversion cannot tell "added" from "complete".
5. **Anchors drift by hundreds of lines in `project.cpp`.** Six of six MAR-180
   citations are stale (E5). Task 0.1 is blocking for this reason.
6. **MAR-187 may land mid-story** and move two of MAR-188's four edit sites.
   Task 0.9 derives them; re-derive if MAR-187 lands after Task 0.
7. **Photoshop's real record order is unverified** (design §9). If a reviewer
   raises it, the honest answer is that it is a pre-existing `psd_import.cpp`
   property, out of MAR-188's scope, and flagged to MAR-189/190 — not that it was
   checked.

---

## Dependency graph

```
Task 0  (measure, re-anchor, gates)
  |
  +--> Task 1  (vocabulary, parser, serializer)      P1-P4, P8   I1-I4 + 6 rejections
  |      |
  |      +--> Task 2  (sixth rebase family)          P5-P7       I5-I8
  |
  +--> Task 3  (PSD synthesiser + gate)              Q0          I18
         |
         +--> Task 4  (planning API, staging)        Q7, Q8, Q11 I9
                |
                +--> Task 5  (classification)        Q1-Q4,Q9,Q10 I10,I11,I12,I15,I16,I17,I19
                       |
                       +--> Task 6  (escaping, dups) Q5, Q6, P8(k) I13, I14
                              |
                              +--> Task 7 (scope sweeps)
                                     |
                                     +--> Task 8 (full verification)
                                            |
                                            +--> Task 9 (docs, commit)
```

Tasks 1-2 and Tasks 3-6 are independent up to Task 7. Task 6 needs Task 1's loader
for P8(k) and Task 5's planner for Q5/Q6, which is why it sits after both.
