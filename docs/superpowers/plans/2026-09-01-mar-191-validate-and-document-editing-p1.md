# MAR-191 Validate and Document Editing P1 — Implementation Plan

Companion design: `docs/superpowers/specs/2026-09-01-mar-191-validate-and-document-editing-p1-design.md`.
Written against `b127048`. Depends on **MAR-190**, which depends on MAR-189 —
**both planned, neither implemented.** Every dependency below is prospective and
is marked.

**Read the design first.** This plan does not restate its reasoning; it restates
its *commands*.

---

## 0. Task 0 — measure before anything

**Nothing in this story may be written until Task 0 completes.** Its output is
the baseline every later claim is checked against, and three of its gates are
**blocking**.

### 0.1 The tree

```bash
SCRATCH=<session scratchpad>/plan-<your-agent-name>      # NOT mar191
rm -rf "$SCRATCH" && mkdir -p "$SCRATCH/tree"
git -C /Users/kwon/Workspace/C/Maroow archive HEAD | tar -x -C "$SCRATCH/tree"
git -C /Users/kwon/Workspace/C/Maroow rev-parse HEAD    # record it
cmake -S "$SCRATCH/tree" -B "$SCRATCH/tree/build"
```

`tar -x` **overlays**. If `$SCRATCH` existed, the `rm -rf` above is what saves
you. Name it after yourself: the scratchpad is shared and every agent on a story
reaches for the story's number.

The live worktree is **not** a measurement surface. `impl-mar189b` is writing
MAR-189 into it; this design watched `src/samples/psd_import_smoke.cpp` change
between two probes.

### 0.2 The gates

| # | Gate | Command | Expected at `b127048` | Blocking |
|---|---|---|---|---|
| **G1** | Parent + trailers | `git log -1 --format='%H%n%(trailers)' HEAD` | `b127048…` + both trailers | — |
| **G2** | CTest discovery | `ctest --test-dir "$SCRATCH/tree/build" -N \| tail -1` | `Total Tests: 22` | — |
| **G3** | Labels | same, `-L runtime` then `-L editor` | `4`, `12` | — |
| **G4** | `add_test` census | `grep -c add_test "$SCRATCH/tree/CMakeLists.txt"` | `25`; three inside `if(MARROW_ENABLE_DISPLAY_TESTS)` at `:1163` | — |
| **G5** | Warnings | `find "$SCRATCH/tree/build/CMakeFiles" -name '*.o' -delete && cmake --build "$SCRATCH/tree/build" -j8 2>&1 \| grep -c 'warning:'` | `0` | — |
| **G6** | Dispatch smoke | `cd "$SCRATCH/tree" && ./build/marrow_agent_dispatch_smoke \| grep -c '\[ OK \]'` | **`437`** — **re-measure, do not carry** | — |
| **G7** | Registry | `grep -c '^    {"' "$SCRATCH/tree/src/editor/agent_dispatch.cpp"` | `66` | — |
| **G8** | Guards + bounds | `grep -rn '!= 66U' "$SCRATCH/tree/src/"`; `grep -n 'std::array<OperationExpectation' …`; `grep -n '== 66' …/test_client.py` | 11 (7/2/2); `agent_dispatch_smoke.cpp:42`; `test_client.py:53,55` | — |
| **G9** | Severity sites | `grep -n 'DiagnosticSeverity::' "$SCRATCH/tree/src/editor/diagnostics.cpp"` | assignments at `:203, :456, :504, :544, :596, :629, :764`; `:129` is the tally; `:654/:656` the name switch | **YES** — V1–V7 target these lines |
| **G10** | **MAR-189 delivered?** | `ls "$SCRATCH/tree/include/marrow/editor/psd_reimport_commit.hpp"`; `grep -rn 'commit_psd_reimport\|PsdCommitStep\|kAllCommitSteps' "$SCRATCH/tree/src" "$SCRATCH/tree/include"` | At `b127048`: **absent** | **YES** |
| **G11** | **MAR-190 delivered?** | `grep -rn 'draw_psd_reimport_modal' "$SCRATCH/tree/src/"` | At `b127048`: **absent** | **YES** |
| **G12** | The bullet census (X1) | the script in §0.3 | `74` bullets, `8` sections, per-story 6/12/14/16/9/10/5/2 | **YES** — must run **before** Task 3 |
| **G13** | The six stale strings each match | §0.4 | each `> 0` | **YES** — X2 is meaningless otherwise |
| **G14** | MCP venv | `ls -d tools/mcp/venv` in the **live worktree** | present, and **gitignored** so absent from the archive tree | — |
| **G15** | Worktree cleanliness | `git status --porcelain` | Record it. It will be dirty; know whose | — |

**G10/G11 are the story's fork.** Their answers decide whether AC4 and the PSD
clauses of AC1/AC3 are executable. If both are absent, do not design around it —
report it and execute the plan's documentation and severity work in full.

### 0.3 X1 — the bullet census, taken before the inventory exists

```bash
python3 - "$SCRATCH/tree/AGENTS.md" <<'EOF'
import re, sys
lines = open(sys.argv[1]).read().split('\n')
story = cur = None; counts = {}
for l in lines:
    m = re.match(r'^## (MAR-[\d–\-]+)', l)
    if m: story = m.group(1)
    if re.match(r'^### Not independently covered', l):
        cur = story; counts.setdefault(cur, 0)
    elif re.match(r'^#{2,3} ', l): cur = None
    elif cur and l.startswith('- '): counts[cur] += 1
for k, v in counts.items(): print(k, v)
print('sections', len(counts), 'bullets', sum(counts.values()))
EOF
```

Record the output verbatim. Task 3's inventory is checked against **this**
number, captured before it existed — never against a recount of the inventory
itself (design §3, shape 4).

### 0.4 X2's patterns, proven capable of matching

Run each **now**, before any edit, and record the non-zero count:

```bash
cd "$SCRATCH/tree"
grep -rnF '64 ops'                     docs/root1/editing-gap-analysis.md
grep -rnF '64-operation'               docs/root1/refector.md
grep -rnF 'MAR-175 is the next product milestone' AGENTS.md
grep -rnF 'MAR-172 is the next product milestone' docs/root1/refector.md
grep -rnF '다음 제품 milestone은 MAR-172다'        docs/root1/discription.md
grep -rnF '최종 갱신: 2026-08-20'                  docs/root1/editing-gap-analysis.md
```

`-F` throughout: BSD `grep` reads `{`/`}` as a malformed BRE interval and returns
a confident `0` for a pattern it cannot match. A zero is evidence only once the
pattern is known to match something.

---

## The non-fixing mandate

**MAR-191 records and classifies. It does not repair another story's defect.**

This is a rule of the story, not a matter of judgement, and it is written here
rather than left to §2.11 of the design because the moment it will be tested is
the moment nobody is reading a design document. A validation story holding a
seventy-three-row inventory of real defects is the most fixing-prone position in
this chain: every row names a file, most name a line, and several are one-line
changes. That is exactly why the rule is absolute rather than advisory — the
cheapness of a fix is not evidence that it is in scope.

**The one exception, and it is an assignment rather than a judgement call:**
**MAR-186's severity gap (Task 1)**, which MAR-186 raised, MAR-187 re-recorded,
and both deferred **to MAR-191 by name**. Nothing else in the inventory was
assigned here.

**What to do with a row you are tempted to fix.** Convert it, in this order:

1. give it its `L#` and its class;
2. name its **owner** — the story that should close it, or `unowned` where none
   exists;
3. write the sentence so that resolving it is one command — name the file, the
   symbol, or the grep;
4. move on.

A row with an owner is a backlog entry. A row you fixed is an unreviewed diff in
a story whose reviewers are reading documentation.

### The 22 class-E rows, named — so the boundary is checkable

These are the "known product defect" rows: real, wrong-or-surprising behaviour,
deliberately unfixed by the story that found it. **Every one is out of scope.**
The list is enumerated rather than described so that "is this in scope?" is a
lookup at 2am and not a decision.

| # | Defect | Recorded by | Owner |
|---|---|---|---|
| E-1 | **A PSD reimport erases every skin** — `psd_import.cpp:1041` `root->erase("skins")`, with `bones` and `slots` replaced wholesale above it | MAR-188; MAR-189 refuses to build on it | unowned — needs a story |
| E-2 | Slot names unstable across imports; a document-global dedup census renames untouched slots | MAR-188, 189 §9.6, 190 §9.5 | unowned |
| E-3 | `normalize_mesh_weights` **creates** its target instead of validating it; `AttachmentSelection` and `MeshWeightTarget` are transposed | MAR-187 durable entry | unowned |
| E-4 | `serialize_project` is not bit-exact for doubles needing 17 significant digits | MAR-186 durable entry | unowned |
| E-5 | The runtime accepts a negative first inherit key time, and a `.mskl` may carry inert `curve` data on an inherit key | MAR-184, 186, 187 | unowned |
| E-6 | The empty-edit hazard is fixed only for the inherit family; siblings still emit `"attachment": []` | MAR-184, 186, 187 | unowned |
| E-7 | An inherit lane can be pruned out of existence and cannot be re-created from the dopesheet | MAR-185 | unowned |
| E-8 | An agent-driven `animation.rename` leaves the GUI clipboard stale | MAR-185 | needs a rename-aware session signal |
| E-9 | macOS case duplicates of a **missing** file remain two recent-project entries | MAR-183, 186, 187 | unowned |
| E-10 | A settings write failure is reported once and then forgotten; the session behaves as if it persisted | MAR-183 | unowned |
| E-11 | An oversized on-disk recent list stays oversized until the next real mutation | MAR-183 | by design; revisit only with E-10 |
| E-12 | Up to `kRecentProjectLimit` `stat` calls per frame while the submenu is open; a dead network mount can block one | MAR-183 | unowned |
| E-13 | A native close during a live authoring gesture re-raises the prompt | MAR-183 | deliberate; a design decision for a later story |
| E-14 | `AwaitingSave` ignores a close request rather than queueing it | MAR-182 | deliberate |
| E-15 | The New form keeps the stale-`opened` hole the chooser lost | MAR-182 | unowned |
| E-16 | New writes `active_animation: "idle"` for a rig that may have no `idle` clip | MAR-181 | needs an animation/skin picker in the New form |
| E-17 | `weights.uncanonicalizable` has no repair, by design | MAR-186 | by design |
| E-18 | The orphan-weight-target supersession hides real weight problems until the orphan is fixed | MAR-186 | deliberate; pinned by G9(e) |
| E-19 | Fixing an orphan overlay can **create** a stale-preview issue | MAR-186 | deliberate; asserted by G9(b) |
| E-20 | The largest orphan class is unreachable — an overlay naming a missing bone or slot makes the project **unopenable** and is reported by nobody | MAR-186, by AC2 | unowned; needs partial/degraded open |
| E-21 | No `fsync`; a crash between the temporary and the rename leaves one orphan `*.tmp.*`, beside the project and beside `editor-settings.json` alike | MAR-180, 183, 190 §9.7 | deliberate non-goal |
| E-22 | A New project created over an existing file, then saved, is recorded in Recents — the arm keys on the create, not on the file's prior absence | MAR-183 | deliberate; both polarities asserted |

**Re-derive this list from the sweep rather than copying it.** It is this
design's classification and it may be wrong at the margins — E-11, E-13, E-14,
E-17 through E-19, E-21 and E-22 are all arguably "deliberate decision" rather
than "defect", and a re-derivation that moves some of them into their own class
is a better answer, not a worse one. What must **not** change is the count's
role: if your sweep produces materially more or fewer than 22, say so and say
why, rather than adjusting to match.

---

## Task 1 — the severity inversion register (V1–V7)

**The story's primary new evidence.** Closes the debt MAR-186 raised, MAR-187
re-recorded, and both deferred to MAR-191 by name.

### 1.1 Baseline

```bash
cd "$SCRATCH/tree"
cmake --build build --target marrow_project_smoke -j8
./build/marrow_project_smoke assets/fixtures/player_idle.marrow   # must pass
cp src/editor/diagnostics.cpp "$SCRATCH/diagnostics.cpp.baseline"
```

### 1.2 One inversion, seven times

For each of the seven sites in design §2.3 — **only** the `make_issue` severity
argument at `:203, :456, :504, :544, :596, :629, :764`, never `:129`:

```bash
SITE=203                                          # then 456, 504, 544, 596, 629, 764
BASE="$SCRATCH/diagnostics.cpp.baseline"          # ABSOLUTE
TGT="$SCRATCH/tree/src/editor/diagnostics.cpp"    # ABSOLUTE

python3 - "$TGT" "$SITE" <<'EOF'
import sys
p, n = sys.argv[1], int(sys.argv[2])
ls = open(p).readlines()
a, b = 'DiagnosticSeverity::Error', 'DiagnosticSeverity::Warning'
line = ls[n-1]
assert (a in line) ^ (b in line), f'line {n} is not a single severity literal: {line!r}'
ls[n-1] = line.replace(a, '@@') .replace(b, a).replace('@@', b)
open(p, 'w').writelines(ls)
EOF

find "$SCRATCH/tree/build/CMakeFiles" -name '*.o' -delete
cmake --build "$SCRATCH/tree/build" -j8 > "$SCRATCH/v$SITE.build.log" 2>&1 \
  || { echo "V$SITE: BUILD FAILED -- NOT a result"; }
cd "$SCRATCH/tree" && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  > "$SCRATCH/v$SITE.run.log" 2>&1; echo "V$SITE exit=$?"

cp "$BASE" "$TGT"
cmp "$BASE" "$TGT" && echo "V$SITE: restore verified"   # ABSOLUTE paths, both sides
```

**Four things here are load-bearing.**

- The `assert` in the mutation script is what stops a stale line anchor from
  silently editing the wrong line. G9 re-derived the seven; this re-checks each
  at the moment of mutation.
- `/CMakeFiles` in the delete path. The unscoped form takes vendored SDL3's
  objects, rebuilds `libSDL3.a` at 96 bytes, and hands you a red run with your
  sources provably untouched.
- A failed **build** is not a result. Record it as "did not compile" and fix the
  mutation.
- The `cmp` names two absolute paths. Do **not** delegate this to
  `tools/inversion/invert.sh`: its restore check is `cmp -s … && echo`, with no
  `set -e` and `rebuild.sh` as the function's last statement, so a **failed
  restore prints nothing and changes no exit status** (design E8). Do not fix
  that script here — MAR-189/190 may be using it.

### 1.3 What to record, per site

| Field | Rule |
|---|---|
| First reddening case | By **run order**. This is the attribution |
| Every other reddening case | Recorded as over-determination, not folded in |
| The exact failure text | Verbatim from the run log. **A summary is not a result** |
| Which clause reddened | A *severity* or *count* clause is a bite. A **message** clause reddening means the mutation was over-broad — report it, do not credit it |
| No bite | A **legitimate outcome.** Record it as a new measured class-B inventory row naming the site. **Do not invent an assertion to close it** |

**V6 (`:629`, `PreviewStaleSkin`) is flagged: no per-issue detector for it was
found by reading.** If it does not bite, that is this task's most interesting
result.

**Forbidden output**: "seven inversions, seven bites" without the seven texts. A
claim over a whole register must be measured as such or worded as an inference.

### 1.4 Exit

Seven rows, seven restores each verified by an absolute-path `cmp`, and
`cmp "$BASE" "$TGT"` clean at the end. `git diff --stat src/editor/diagnostics.cpp`
in the **live** worktree must be untouched by this task.

---

## Task 2 — O1, the importer group-order measurement

The only new test code in the story. Design §2.5.

1. Read MAR-188's PSD synthesiser in `src/samples/psd_import_smoke.cpp` — the
   one gated by Q0/Q0b on reproducing the fixture's layer report, bone report,
   extracted pixels **and** header.
2. Add `O1`: synthesise a document whose group is written in the **inverse**
   record order — `lsct` 3 divider first, then the children, then the `lsct` 1/2
   folder record — and assert `parse_psd_document` refuses it with exactly
   `"PSD folder end marker appeared without an open folder."`
3. Add the control arm: the same layers in the **accepted** order still parse and
   still produce the same layer report. Without it, a synthesiser that emits
   nothing parseable would pass O1 for the wrong reason.
4. Prove O1 is a gate: it is red today because it does not exist. Then run
   **I-O1** — make the record walk treat a pop on an empty `active_groups` as a
   no-op — and confirm O1 reddens while every pre-existing PSD case stays green.
   Restore.

**Do not claim O1 tells anyone what Photoshop emits.** It measures the importer.
The inventory row stays class **G** and stays open.

**Staging**: `src/samples/psd_import_smoke.cpp` is held by MAR-189's implementer.
`git apply --cached` your hunks; never `git add` the file.

---

## Task 3 — the limitation inventory

Design §2.2. **Runs after G12, never before.**

1. Extract every bullet from the eight `### Not independently covered` sections,
   plus MAR-181's `### Two gaps recorded rather than half-closed`, MAR-180's
   fsync and orphan-temporary paragraphs, MAR-182's detector table, the
   limitation-shaped entries in `## Repo facts that outlive their story`, and
   MAR-189's and MAR-190's `## 9 Known limitations`.
2. De-duplicate by the design's rule: **same artefact and same mechanism = one
   row**. Expect `shell_main.cpp`'s frame body to collapse **7 → 1** and
   `commit_path_choice` **5 → 1**.
3. Classify into the eight classes (A–H). A row whose class is unclear is a row
   whose sentence is unclear — rewrite the sentence.
4. Write `## Editing P1 Limitation Inventory` into `AGENTS.md`, immediately after
   `## Repo facts that outlive their story`.
5. Check the result against **G12's** number, not against a recount of your own
   table. Report `74 bullets → N rows` with the collapse accounted for.

**Pre-registered prediction: ~73 rows (A 12, B 11, C 9, D 4, E 22, F 3, G 5,
H 7).** It is a prediction. A materially different count means the granularity
rule was read differently — report the disagreement; do not adjust your sweep to
hit the number.

6. Add the **prospective** rows that no `AGENTS.md` section states yet: MAR-189's
   and MAR-190's `## 9` entries, and — flagged as **relayed, not verified here** —
   MAR-189's three **validate-only** rollback arms (`ValidateRequest`,
   `PruneUnpreserved`, `ValidateStagedBundle`), which touch nothing in the target
   bundle and therefore stay green under a byte-map clause **with rollback
   entirely broken**. Word it as *a row that should exist*: MAR-189 does not
   exist yet, its implementer has been asked to label those arms, and MAR-191
   must **re-derive the row against the delivered code** rather than inherit this
   sentence. A row carried forward unverified is how an unmeasured claim enters a
   document.

**Do not edit a single `## MAR-NNN` section.** Not to correct, not to
cross-reference, not to mark closed. The inventory is the maintainable copy.

**Do not fix a row.** See **The non-fixing mandate** above; the 22 class-E rows
are enumerated there so the boundary is a lookup rather than a judgement.

---

## Task 4 — the durable entry on historical sentences

Design §2.4, E9. One entry in `## Repo facts that outlive their story`, citing
both measured instances: `discription.md:59-60`'s dated bullet carrying "64" and
`AGENTS.md:456`'s narrated "22/22" inside a durable entry. State the widened
criterion — **anchored by heading, date, or narrated event** — and leave both
originals untouched.

`AGENTS.md`'s own rule authorises this: lift the general form once a second
instance appears in a second place. Two instances are named so the entry is
self-checking.

---

## Task 5 — AC5, the documentation synchronisation

Design §2.4. Every change is **overwrite a present-tense claim** or **append a
parenthetical to an anchored one**. Deletion is not available.

### 5.1 Overwrite (present tense, measurably wrong)

| File:line | From | To |
|---|---|---|
| `AGENTS.md:6` | "MAR-154 through MAR-174 … MAR-175 is the next product milestone" | The tip at your parent, and the next open story |
| `editing-gap-analysis.md:3` | "최종 갱신: 2026-08-20 …" | Today's date and the current basis |
| `editing-gap-analysis.md:47` | the stale "다음 제품" pointer | current |
| `editing-gap-analysis.md:95` | "이제 정확히 64 ops다" and "64 ops 그대로다" | **66** (both) |
| `editing-gap-analysis.md:224` | "다음 직접 제품 milestone은 MAR-171이다" | current |
| `editing-gap-analysis.md:288` | "완료된 MAR-170에 의존하는 MAR-171부터" | current |
| `editing-gap-analysis.md:455` | "현재 64 ops다" | **66** |
| `discription.md:61` | "MAR-175는 … 다음 제품 milestone이다" | current |
| `discription.md:892` | "다음 제품 milestone은 MAR-172다" | current |
| `refector.md:20` | "the **current** registry … 64-operation" | **66** |
| `refector.md:333` | "MAR-172 is the next product milestone" | current |

**Re-derive every line number.** These are this design's, at `b127048`.

### 5.2 Parenthetical (anchored, must not be overwritten)

`discription.md:59` and `:60` — both open *"MAR-182는 2026-08-30에 … 완료했다"* /
*"MAR-183은 2026-08-30에 …"* and then state "64-operation Agent/MCP surface는
모두 그대로다". The date anchors the sentence. Append the parenthetical naming
today's 66; do not touch the 64.

The eleven `22`-valued sites in `AGENTS.md` (`:3498, :3669, :3798, :3909, :4011,
:4105` and the Task-0 rows) are all inside `## MAR-NNN` sections. **Leave every
one.**

### 5.3 Additive

- `AGENTS.md ## Current Validation`: command lines for MAR-189's, MAR-190's and
  MAR-191's cases — **only for what actually shipped**.
- `.agents/tasks/prd-marrow-runtime.json`: MAR-189/190/191 `status`.
- `editing-gap-analysis.md`'s story table: MAR-189/190/191 rows marked complete
  with their real scope.
- `format-spec.md`: **only** if MAR-189 or MAR-190 changed the schema. If not,
  say so in the record — "untouched, verified by `git diff`" is a result.

### 5.4 X2 — the mirrored zero-result gate

Re-run §0.4's six commands. **Every one must now return zero**, and each was
proven capable of matching in Task 0. Grepping for `66` after writing `66` proves
only that `sed` ran; the falsifiable direction is the disappearance of the old
string.

Then **I-X2**: revert one edit, confirm X2 goes non-zero, restore.

---

## Task 6 — AC2's format non-change, proved by diff

AC2 forbids changing `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1
and the `.marrow` schema. MAR-180's technique: a **zero-line diff**, not an
assertion.

```bash
cd /Users/kwon/Workspace/C/Maroow
git diff --stat -- src/runtime/ include/marrow/runtime/ include/marrow/marrow.h
git diff -U0 -- src/editor/project.cpp | grep -E '^[+-]' | grep -E 'root\[|emplace\(|find_member|->erase'
```

Both empty for MAR-191's own hunks. Record the commands and the emptiness.

---

## Task 7 — conditional: register `marrow_psd_import_smoke` in CTest

Design §2.10. **Skip unless MAR-189's tracked-asset guard exists** (its §9.4 A7).
Not registering is the safe default.

If taken:

```cmake
add_test(
    NAME marrow.psd_import_smoke
    COMMAND marrow_psd_import_smoke
            assets/fixtures/psd_import_sample.psd
            assets/fixtures/psd_import_sample_reimport.psd
)
set_tests_properties(marrow.psd_import_smoke PROPERTIES LABELS "editor")
# WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} -- the smoke's default inputs are
# relative (psd_import_smoke.cpp:25-26), exactly like marrow.agent_dispatch_smoke
```

Then, and this is the gate rather than the change:

```bash
ctest --test-dir build --output-on-failure
git status --porcelain assets/          # MUST be empty
```

Run that `git status` **once against a planted modification first** and confirm
it reports it, then revert the plant. A gate that has never returned non-zero has
not been shown capable of it.

`ctest -N` moves **22 → 23**, `grep -c add_test` **25 → 26**. Measured prose
cost: **none** — every `22` in `AGENTS.md` is historical and the
`## Current Validation` line carries no number. Re-verify that before relying on it.

---

## Task 8 — AC6, the full validation run

Run every command, at the tip, in the **live worktree** (the MCP venv is
gitignored and exists only there). Record each result **verbatim, with its
counts**.

```bash
cmake -S . -B build && cmake --build build
cmake --build build --target marrow_verify_third_party
cmake --build build --target marrow_constraint_warning_check
cmake --build build --target marrow_frame_body_check

ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -L runtime
ctest --test-dir build --output-on-failure -L editor

./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
    --export-runtime /tmp/mar191_export.mskl --export-binary /tmp/mar191_export.mbin
./build/marrow_inspect --compare /tmp/mar191_export.mbin /tmp/mar191_export.mskl
./build/marrow_agent_dispatch_smoke
./build/marrow_psd_import_smoke \
    assets/fixtures/psd_import_sample.psd assets/fixtures/psd_import_sample_reimport.psd

MARROW_CONFIG_HOME=/tmp/mar191-cfg \
  ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2

tools/mcp/venv/bin/python -m py_compile \
    tools/mcp/server.py tools/mcp/test_client.py \
    tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
# then, by hand: shell with --agent-port 9876, and
tools/mcp/venv/bin/python tools/mcp/test_client.py

./build/marrow_renderer_sample --skip-render \
    assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl

git diff --check
```

**Three rules for the write-up.**

- `-L runtime` (4) + `-L editor` (12) = 16 ≠ 22. **Six tests carry neither
  label.** Present the labels as a subset check, never as a partition.
- The MCP client half is **manual** and is reported as manual. AC6 is met at that
  standard, the same one MAR-180 and MAR-185 met.
- `--skip-render` is what AC6 names. This host cannot create a Metal device for
  the headless renderer; say so rather than implying a rendered frame.

**`MARROW_CONFIG_HOME` on every shell invocation.** `$HOME/Library/Application
Support/Marrow` must not exist before or after — check both.

---

## Task 9 — the MAR-191 validation section

Write `## MAR-191 Validate and Document Editing P1 Validation Results` into
`AGENTS.md`, in the same shape as its neighbours:

- **What was measured before any code was written** — Task 0's fifteen gates.
- **Result** — one row per acceptance criterion, each labelled **gate**,
  **witness**, or **prospective**. Four of six are witnesses; say so.
- **Inversions run** — V1–V7 with their **actual** failure texts beside this
  design's predictions, plus I-O1 and I-X2.
- **Document errors found** — this design's E1–E9 plus whatever Task 0 adds.
- **Not independently covered** — this story's own, and a pointer to the
  inventory for everything else. **This is the last such section the chain
  writes**; the inventory is what the next story reads.

---

## Task 10 — commit

One commit. Korean subject and body.

```bash
git status --porcelain            # know whose hunks are whose
git apply --cached <your.patch>   # HUNKS, not files, on every shared path
git diff --cached --stat          # verify before committing
```

`src/samples/psd_import_smoke.cpp` is shared with MAR-189's implementer.
`AGENTS.md` may be shared with anyone. `git add <path>` stages the whole file
including another agent's uncommitted work — that is how `3acbf58` carried
`plan-mar190`'s entries under a MAR-188 message.

Both trailers, **contiguous, no blank line between them**:

```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: <copied from `git log -1 --format='%(trailers)' HEAD`>
```

A blank line makes git parse only the last. **Three consecutive stories shipped
without `Claude-Session:` and each needed an amend.**

---

## Appendix A — order and dependencies

```
Task 0  ──┬─→ Task 1 (V1–V7)          independent
          ├─→ Task 2 (O1)             independent
          ├─→ Task 3 (inventory)      needs G12
          ├─→ Task 4 (durable entry)  independent
          └─→ Task 5 (AC5 docs)       needs G13
Task 3, 4, 5 ─→ Task 6 (AC2 diff) ─→ Task 7 (conditional) ─→ Task 8 (full run)
Task 1, 2, 8 ─→ Task 9 (write-up) ─→ Task 10 (commit)
```

Tasks 1, 2 and 5 touch disjoint files and may run in any order. Task 8 runs
**last** among the verification tasks, in the live worktree, so its numbers are
the ones the write-up reports.

## Appendix B — the five things most likely to go wrong

1. **Fixing something in the inventory.** The 22 class-E rows are real defects
   and every one is another story's work — they are named in **The non-fixing
   mandate** precisely so this is checkable rather than remembered. Add rows with
   owners; fix nothing. The one assigned exception is Task 1.
2. **Reporting the severity register as a total** instead of seven texts.
3. **Copying this design's 437 / 66 / 22 / line numbers** instead of re-deriving
   them at your parent. A carried number and a re-measured number can be equal
   and are not the same claim.
4. **`git add`-ing a shared file.** `psd_import_smoke.cpp` is being written by
   another agent right now.
5. **Trusting `invert.sh`'s restore message.** It is silent on failure. Do your
   own `cmp` with two absolute paths.
