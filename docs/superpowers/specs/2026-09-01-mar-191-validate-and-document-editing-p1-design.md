# MAR-191 Validate and Document Editing P1 — Design

Story: **MAR-191 — Validate and document Editing P1**
Depends on: **MAR-190**, which depends on MAR-189, which depends on MAR-188 (`71465db`).
Date: 2026-09-01. Written against the tree at **`b127048`**.

Companion plan: `docs/superpowers/plans/2026-09-01-mar-191-validate-and-document-editing-p1.md`.

**Every number in this document was measured by this design's author, in an
isolated `git archive HEAD | tar -x` tree at `b127048`, on 2026-09-01.** None was
copied from a brief or from another story's section. Where a figure is another
story's historical measurement it is labelled as one and its commit is named.

---

## 0. Where this story sits

### 0.1 The story in one sentence

MAR-168–190 built the Editing P1 surface across twenty-three stories, each with
its own validation section and its own list of what it did **not** cover;
MAR-191 runs the whole suite at the chain's tip, closes the one evidence gap
three stories deferred to it by name, consolidates twenty-three scattered
limitation lists into one inventory that can be maintained, and brings five
documents back into agreement with the code.

### 0.2 Two jobs no other story in the chain had

Every other story added a surface and proved it. MAR-191's acceptance criteria
are largely about **verifying and documenting what the other twenty-three
built**, and that produces two obligations nothing in the chain has had to meet.

**First: the limitation inventory has to become one thing.** Each story wrote a
`### Not independently covered` section. Measured at `b127048`: **74 bullets
across 8 such sections** (MAR-188 6, MAR-187 12, MAR-186 14, MAR-185 16,
MAR-184 9, MAR-183 10, MAR-182 5, MAR-181 2), plus MAR-181's separately-headed
`### Two gaps recorded rather than half-closed`, MAR-180's fsync and
orphan-temporary paragraphs, MAR-182's detector table, and MAR-189/190's
prospective `## 9 Known limitations`. Eleven of those entries are the *same*
limitation restated: `shell_main.cpp`'s interactive frame body appears **seven**
times, `commit_path_choice` **five**. Restatement is what `AGENTS.md`'s own rule
about historical sentences makes inevitable — a lesson recorded only inside
story sections can never be generalised in place, so it accumulates as instances
and the *n*th reads as a fresh finding. §2.2 is the consolidation, and it is
deliberately **additive**: the story sections are not edited.

**Second: one explicitly deferred debt closes here.** MAR-186 recorded that its
severity **assignments** are load-bearing but uninverted; MAR-187 re-recorded it,
verified in the artefact rather than relayed, and deferred it **to MAR-191 by
name**. §2.3 closes it. It is the only place in this story where new evidence —
as opposed to re-run evidence — is produced.

### 0.3 Errors found in the incoming brief and in the source documents

Recorded rather than silently corrected, per `AGENTS.md`'s rule that a
correction naming an artefact must be resolved **in the artefact** before it is
applied. Each row below names the command that settled it.

| # | Where | Claim | Finding |
|---|---|---|---|
| **E1** | The brief | *"MAR-189 records 'AC3 is satisfied in form by 15 arms and in evidence by 12'"* | **That sentence exists in neither MAR-189 document.** `grep -rn 'in form\|in evidence'` over both MAR-189 and MAR-190 files returns **nothing**. The **15** is real but is something else: it is the `std::array<PsdCommitStep, 15>` bound in the design's §3.2. **Origin, supplied by the team lead after this design was written: it was a sentence MAR-189's Task 0 was asked to put in the record, and it was lost mid-story before reaching Task 1** — so a *planned* artefact was quoted as an existing one. That is a sharper hazard than a stale line anchor, and worth its own line in the durable record: **a stale anchor fails to resolve and announces itself; a quotation of something that was never written resolves to nothing and invites a search.** The countermeasure is the one `AGENTS.md` already names — a correction must name a checkable artefact, and a claim that resolves to nothing is a finding to report rather than an instruction to follow. The finding *underneath* the quotation is real and survives as a prospective inventory row: §2.6. |
| **E2** | MAR-189's own design | The commit step count | **Internally inconsistent, off by one.** §3.2 lists **15** enum values and declares `std::array<PsdCommitStep, 15>`; the plan's Task list says "15 values". But the design's prose says **fourteen** at `:47` ("fourteen commit steps"), `:406` ("fourteen steps") and `:501` ("the fourteen steps"), and **I12** says *"Thirteen of the sweep's fourteen arms"*. Enumerating §3.2: ValidateRequest, PruneUnpreserved, ValidateStagedBundle, OpenJournal, Backup{Layers,Texture,Atlas,Skeleton}, Place{Layers,Texture,Atlas,Skeleton}, AdoptRuntimeSources, UpdateProvenance, CleanJournal = **15**. Reported to MAR-189's implementer as a blocking arithmetic check, not resolved here — MAR-191 cannot know which number was intended. |
| **E3** | The brief | *"**EXDEV is unprovable** — `/tmp`, the repo and scratch are one volume"* | **True about the environment, and it understates the code.** `stat -f '%d'` gives device `16777232` (`/dev/disk3s5`) for `/tmp`, the repository and the scratchpad alike, so the environmental half holds. But `atomic_file_write.hpp`'s own contract says *"Creates the temporary in the destination's own directory so the rename never crosses a filesystem boundary."* EXDEV is therefore **structurally unreachable on the production path**, not merely unmeasurable here — a second volume would not produce it. And the `RenameCallback` seam can inject `std::errc::cross_device_link` for message coverage at will. The limitation is much narrower than stated, and §2.7 records it in the corrected form. |
| **E4** | The brief | *"`rebuild.sh` with no object arguments deletes everything under `<build-dir>/CMakeFiles`"* | Effectively right, literally not: it deletes `*.o` (`find "${build_dir}/CMakeFiles" -name '*.o' -delete`). Immaterial to the hazard, corrected so the next reader's `grep` resolves. |
| **E5** | `AGENTS.md:6` (`## Project State`) | *"MAR-122 through MAR-128, MAR-154 through MAR-174 … are complete. **MAR-175 is the next product milestone** and depends on MAR-174."* | **Stale present tense.** MAR-175 through MAR-188 have shipped; the tip is `b127048`. Found by this design, not relayed. An **AC5 target** — see §2.4's table. |
| **E6** | `docs/root1/editing-gap-analysis.md:95, :455` | *"registry는 이제 정확히 64 ops다 … registry는 64 ops 그대로다"* and *"현재 64 ops다"* | **Stale present tense, three occurrences on two lines.** Measured: `grep -c '^    {"' src/editor/agent_dispatch.cpp` = **66**. Both lines say "이제"/"현재" (now / currently), so both are the correctable kind. An **AC5 target**. |
| **E7** | `docs/root1/discription.md:61, :892`; `editing-gap-analysis.md:3, :47, :224, :288`; `refector.md:333` | Seven "the next product milestone is …" pointers | All stale, naming MAR-171, MAR-172 or MAR-175. `editing-gap-analysis.md:3` additionally declares itself *"최종 갱신: 2026-08-20"*. **AC5 targets**, enumerated in §2.4. |
| **E8** | `tools/inversion/invert.sh:21-23` | The harness's restore verification | **A failed restore is completely silent.** `restore()` is `cp …; cmp -s "$copy" "${root}/${file}" && echo "  restore verified by cmp"; rebuild.sh …`. With no `set -e` (the script sets `-uo pipefail` only) and `rebuild.sh` as the last statement, a failing `cmp` prints **nothing** and changes **no** exit status. The harness reports success by silence, which is the one thing a restore check must not do. §2.8 states MAR-191's countermeasure and declines to edit shared tooling mid-flight. |
| **E9** | `AGENTS.md`'s present-tense/historical rule | *"if it sits under a `## MAR-NNN` heading it is almost certainly a measurement"* | **The criterion is necessary and not sufficient, measured on three instances.** `discription.md:59-60` carry *"64-operation Agent/MCP surface는 모두 그대로다"* inside dated completion bullets under **no** `MAR-NNN` heading; `AGENTS.md:456` carries *"restored it to 250 objects and 22/22"* inside the **durable** `## Repo facts` section. Both are historical by their own dated or narrated anchor and neither is reachable by the stated criterion. §2.4 proposes the widening, and `AGENTS.md`'s own rule — *lift the general form once a second instance appears* — is what authorises it. |

**Everything else in the brief resolved.** `ctest -N` **22** against `grep -c
add_test` **25**, the three extra inside `if(MARROW_ENABLE_DISPLAY_TESTS)` at
`CMakeLists.txt:1163` (`add_test` at `:1164`, `:1173`, `:1182`); the array bound
at `src/samples/agent_dispatch_smoke.cpp:**42**`; **eleven** `!= 66U` guards
split **7 / 2 / 2** across `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`,
`shell_smoke_constraints.cpp:147,676` and `shell_smoke_timeline.cpp:3697,4389`;
**two** `== 66` at `tools/mcp/test_client.py:53,55`;
`cmake/CheckFrameBodies.cmake` wired POST_BUILD on `marrow_editor_shell` at
`CMakeLists.txt:933-938` with the regex `draw_[a-z_]+windows?\(`, which no modal
name matches; `tools/inversion/` first committed at `f3a3768` with its guards
wired at `23b326e`; `validate_project_for_save` at `project.cpp:6077` and absent
from every header under `include/`; `EditTransaction::commit()`
(`session.cpp:2898`) delegating to `impl_->commit()` and touching no path;
`psd_import.cpp:1041` `root->erase("skins")` with `bones` and `slots` replaced
wholesale two lines above; `marrow_psd_import_smoke` built at
`CMakeLists.txt:853` and **not** in CTest, and `tools/mcp/test_client.py`
referenced nowhere in `CMakeLists.txt`.

**And the number the brief told me to re-measure rather than carry.**
`marrow_agent_dispatch_smoke` prints **437** `[ OK ]` at `b127048`, built and run
in an isolated tree. That is numerically identical to MAR-188's baseline and
epistemically different: it is this story's own measurement at this story's own
parent, and it is recorded that way.

### 0.4 MAR-189 and MAR-190 are planned and **neither is implemented**

Both stories have a design and a plan and **no code**. Every dependency on them
in this document is marked **prospective**. §2.9 lists what each must deliver
before MAR-191's corresponding clause is executable, and what MAR-191 does
instead if it is not there.

The live worktree is not a safe measurement surface for this reason and one
other: `impl-mar189b` is writing MAR-189 into it **now**. Between two probes in
this design's own research, `src/samples/psd_import_smoke.cpp` gained an
uncommitted `mar189_naming` scratch root; a `grep` run minutes earlier against
the same path had shown a file without it. Every figure above was therefore
re-taken from the archive tree. This is `AGENTS.md`'s *"while a review is live it
mutates and restores tracked files continuously"* entry, observed a second time,
and §2.8 makes the isolated tree mandatory rather than advisory.

---

## 1. The measured baseline

Everything below was measured at `b127048` in the isolated tree, and is what
Task 0 must re-measure at MAR-191's **actual** parent.

| Quantity | Value at `b127048` | How |
|---|---|---|
| `ctest --test-dir build -N` | **22** | Configured archive tree |
| `-L runtime` / `-L editor` | **4** / **12** | Six of the 22 carry neither label |
| `grep -c add_test CMakeLists.txt` | **25** | Three gated on `MARROW_ENABLE_DISPLAY_TESTS` |
| `marrow_agent_dispatch_smoke` | **437** `[ OK ]`, `PASSED` | Built and run in the archive tree |
| Registry | **66** | `grep -c '^    {"' src/editor/agent_dispatch.cpp` |
| `!= 66U` guards | **11**, split 7/2/2 | `grep -rn '!= 66U' src/` |
| Severity assignments in `diagnostics.cpp` | **7** | `:203, :456, :504, :544, :596, :629, :764` |
| `### Not independently covered` bullets | **74** across **8** sections | Script over `AGENTS.md` |
| `AGENTS.md` | **5226** lines | `wc -l` |

**Six tests carry neither label**, so `-L runtime` + `-L editor` = 16 ≠ 22. AC6
names "full CTest **plus** runtime/editor labels", which is the right shape — the
labels are a subset check, not a partition, and a report that presents 4 + 12 as
covering the suite would be an inference dressed as a measurement.

---

## 2. Decisions

### 2.1 What MAR-191 adds, and what it only verifies

MAR-191 is overwhelmingly a **verification and documentation** story. Its
production-source footprint is intended to be **zero**. Three things are new:

1. **Seven severity inversions** (§2.3) — mutations applied and reverted, no
   committed source change.
2. **One measurement of the PSD importer's group-record ordering** (§2.5) — a
   new case in `psd_import_smoke.cpp`, and the only new test code in the story.
3. **One optional `add_test`** registering `marrow_psd_import_smoke` (§2.10),
   conditional on a gate MAR-189 must pass first.

Everything else is: run the suite, record the results, write the inventory,
synchronise the documents. **This is stated up front because a validation story
that starts writing product code has misread its own acceptance criteria**, and
because the temptation to "just fix" an entry in a 73-item limitation inventory
is the single largest scope risk in this story (§10.1).

### 2.2 The limitation inventory — where it lives, its shape, and its rule

**Where.** A new `## Editing P1 Limitation Inventory` section in `AGENTS.md`,
placed immediately **after** `## Repo facts that outlive their story` and before
`## MAR-192–210`. Not inside any story section, because a story section may not
be edited; not in `docs/root1/`, because the reader who needs it is the next
agent, who reads `AGENTS.md`.

**Shape.** One table, one row per distinct limitation, with an `L`-prefixed
stable id and four columns:

| Column | Contains |
|---|---|
| `L#` | Stable id. Referenced from future stories instead of restating the entry |
| Class | One of the eight in the taxonomy below |
| Limitation | One sentence, naming the artefact — file, symbol, or command — so that resolving it is one command |
| Recorded by | Every story section that states it, in ascending story order. **Seven** entries for L-frame-body, **five** for L-commit-path-choice |

**The rule that makes it maintainable, and it is the whole point.** The story
sections stay **untouched**. The inventory is the maintainable copy; a story
section is the record of what that story saw. When a later story closes an entry,
it edits the **inventory row** — adding "closed by MAR-NNN" — and adds nothing to
and removes nothing from any `## MAR-NNN` section. This is `AGENTS.md`'s existing
durable-lifting rule (*"lift its general form … citing both instances; leave the
originals untouched"*) applied to limitations rather than to lessons, and it is
the direct answer to why `shell_main.cpp`'s frame body needed writing seven times.

**The taxonomy — eight classes, chosen so that every class implies a different
kind of work.**

| Class | Meaning | Who can close it |
|---|---|---|
| **A. Coverage gap** | A surface exists and no test reaches it | A story with a test budget |
| **B. Evidence gap** | Covered in form; no mutation proves the cover bites | An inversion; cheap |
| **C. Unreachable** | Written, correct by inspection, no input reaches it | Nobody — record, do not "fix" |
| **D. Compiler blindness** | A list or chain the compiler cannot police | A consumer-side test, or `-Werror=switch` |
| **E. Known product defect** | Wrong or surprising behaviour, deliberately unfixed | A product story |
| **F. Permanently open** | Quantified over a set the program cannot enumerate | A decision nobody has taken |
| **G. Human/hardware required** | Needs Photoshop, an interactive host, a Metal GPU, a second volume | Not an agent |
| **H. Prospective** | Stated by MAR-189/190, neither implemented | Those stories |

**The de-duplication rule, stated so a re-derivation lands in the same place.**
Two bullets are the same entry when they name the same **artefact** and the same
**mechanism**. MAR-183's *"Orphan `*.tmp.*` files beside `editor-settings.json`"*
and MAR-180's orphan temporary beside the project are **one** entry — one
mechanism, one writer, two destinations — and the row names both. MAR-186's
*"`-Wswitch` warns but does not stop the build"* and MAR-185's *"the build still
has no `-Wall`/`-Wextra`"* are **one** entry. MAR-184's *"P4 and the equivalence
half of P12 have no story-owned inversion"* and MAR-186's *"G4 and G5 are
witnesses"* are **two**, because the artefacts differ.

**Pre-registered size: 73 rows in 8 classes**, from a draft built during this
design (A 12, B 11, C 9, D 4, E 22, F 3, G 5, H 7). **This number is a
prediction, not a measurement, and the implementer must re-derive it from the
sweep rather than copy the draft.** Its purpose is to be falsified: a
re-derivation landing on 60 or on 90 means the granularity rule above was read
differently, and that disagreement is a finding worth a line in the story's
record. Copying a table and reporting its length as a sweep result would be
exactly the failure `AGENTS.md` names — *an inference written in the grammar of a
measurement*.

**The five entries this story considers most significant**, by the standard "how
badly would a reader be misled by not knowing it":

1. **A PSD reimport erases every skin.** `psd_import.cpp:1041` is
   `root->erase("skins")`, with `(*root)["bones"]` and `(*root)["slots"]`
   replaced wholesale immediately above. MAR-189 refuses to ship on top of this
   rather than papering over it; the importer behaviour is **unfixed** and the
   inventory says so in the sentence a user would recognise. Class E.
2. **MAR-180's "rebases every relative path" is permanently open.**
   `preserved_root` is by definition what the code does not understand, so the
   clause quantifies over a set the program cannot enumerate. `AGENTS.md` says in
   as many words that it must not be written up as "six families, done". The
   inventory row repeats the prohibition rather than the count. Class F.
3. **Photoshop's real layer-record order is unverifiable here and requires
   Photoshop.** Class **G**, not a macOS limitation — the distinction matters
   because a G-class entry cannot be closed by any future agent story, and three
   stories have carried it as though it might be. §2.5 extracts the half that
   *is* measurable.
4. **MAR-186's seven severity assignments have zero inversions.** Class B, and
   the one this story closes (§2.3).
5. **`shell_main.cpp`'s interactive frame body is reachable from no test**, seven
   times restated, with `commit_path_choice` five times behind it. Class A, and
   the pair that motivates the inventory's existence.

### 2.3 Closing MAR-186's deferred severity gap — seven sites, seven flips

**The gap, verified in the artefact rather than relayed.**
`src/samples/editor_project_smoke.cpp:16306-16314` recomputes `errors` and
`warnings` by walking `report.issues` and testing `issue.severity ==
DiagnosticSeverity::Error`; `:16315` then compares those totals against
`report.error_count` / `report.warning_count`. Both sides read the same field, so
a **misassignment** moves them together and `check_invariants` cannot see it.
MAR-186 and MAR-187 both said so; both are right.

**The population is seven, and it is exactly seven.** Every severity literal
handed to `make_issue` in `src/editor/diagnostics.cpp`:

| Site | Code | Assigned | The comment beside it |
|---|---|---|---|
| `:203` | `OverlayOrphanAnimation` | **Error** | *"unlike every other family here, this one corrupts what ships"* |
| `:456` | `OverlayOrphanWeightTarget` | Warning | *"this is dead data. Nothing downstream ever sees it"* |
| `:504` | `WeightsUncanonicalizable` | **Error** | — |
| `:544` | `WeightsNonCanonical` | Warning | *"the runtime normalizes at load"* |
| `:596` | `PreviewStaleAnimation` | Warning | *"the preview falls back silently"* |
| `:629` | `PreviewStaleSkin` | Warning | — |
| `:764` | `ProjectUnsavedChanges` | Warning | — |

`:129` is `finalize`'s tally and `:654/:656` are `diagnostic_severity_name`'s
switch; neither is an assignment. **Seven codes, seven assignments, one flip
each** — so the register is **complete over the class**, not a sample. That
completeness is the reason this closure is worth doing at all: a sampled severity
inversion would be theatre.

**The register.** Seven inversions, `V1`–`V7`, each flipping one literal to its
opposite, each rebuilt with objects deleted, each run against
`./build/marrow_project_smoke assets/fixtures/player_idle.marrow`, each restored
and re-verified. Predicted first failures, from reading the smoke:

| # | Site | Flip | Predicted first failure | Candidate detectors read in the source |
|---|---|---|---|---|
| **V1** | `:203` | Error → Warning | G1's per-issue severity check | `:16567` per-issue `!= Error`; `:16597` absolute `7/0`; MAR-187 `:18100` group[0] is Error |
| **V2** | `:456` | Warning → Error | a G-case per-issue Warning check | `:17007` |
| **V3** | `:504` | Error → Warning | G10's absolute `1 Error / 2 Warnings` | `:17147` per-issue `!= Error`; `:17532`, `:17585` |
| **V4** | `:544` | Warning → Error | a G-case per-issue Warning check | `:17197` |
| **V5** | `:596` | Warning → Error | a G-case per-issue Warning check | `:17339` |
| **V6** | `:629` | Warning → Error | **uncertain** — no per-issue Warning check was found reading only `PreviewStaleSkin` | `:17339` or `:17654` if either covers it; otherwise G10's absolutes |
| **V7** | `:764` | Warning → Error | G10's dirty-session absolute `1/3` | `:17654`; `:17632` |

**V6 is flagged deliberately.** Its predicted detector could not be established
by reading, and that is a *result to measure*, not a hole to fill with a guess.
If V6 does not bite, the correct output is a row in the inventory reading
"`PreviewStaleSkin`'s severity has no detector" — **not** a new assertion invented
to make the register look complete. Inventing one would be the non-biting theatre
`AGENTS.md`'s H4 names, and would additionally convert a measurement into a
self-satisfying gate (§3, shape 4): a test written after seeing which mutation
survives is a test that tests the mutation, not the code.

**What the seven results are permitted to say.** Three outcomes are legitimate
and each is written up differently:

- **bites, named** — the entry moves from class B to closed, with the case name
  and the exact failure text recorded;
- **bites, over-determined** — two or more cases redden; record all of them, and
  record which one is the *attribution* by run order;
- **does not bite** — the assignment has **no** detector. Record it as a new,
  measured class-B entry naming the site, and state plainly that MAR-191
  measured the absence rather than closed the gap.

**The one thing forbidden**: reporting "seven inversions, seven bites" without
the per-site failure text. That is the coverage-row failure `AGENTS.md` records —
*a claim over the whole register must be measured as such or worded as an
inference* — and this chain already spent three passes unwinding one instance.

### 2.4 The documentation synchronisation — five documents, two kinds of sentence

AC5 names *"the roadmap, editing-gap analysis, architecture description, format
specification, and AGENTS.md validation commands"*. Resolved to files:

| AC5 name | File | Status measured at `b127048` |
|---|---|---|
| roadmap | `.agents/tasks/prd-marrow-runtime.json` | MAR-189/190/191 `"status": "open"`; MAR-188 `done` |
| roadmap (secondary) | `docs/root1/refector.md` | `:20` *"the **current** registry … 64-operation"* → **66**; `:333` *"MAR-172 is the next product milestone"* |
| editing-gap analysis | `docs/root1/editing-gap-analysis.md` | `:3` header dated 2026-08-20; `:47`, `:224`, `:288` stale "next milestone"; `:95` "64 ops" ×2; `:455` "현재 64 ops다" |
| architecture description | `docs/root1/discription.md` | `:61` "MAR-175는 … 다음 제품 milestone이다"; `:892` "다음 제품 milestone은 MAR-172다" |
| format specification | `docs/root1/format-spec.md` | **Already synchronised for MAR-188** — `:685-742` documents `import_sources.psd` in full, including the sixth rebase family and the `image` bare-name rule. Needs a MAR-189/190 delta only if either changes the schema |
| `AGENTS.md` validation commands | `AGENTS.md` `## Current Validation` | Add MAR-189/190/191 command lines; `## Project State` at `:6` stale (E5) |

`refector.md` is listed because `editing-gap-analysis.md:466` names it as a
roadmap source. Whether AC5's "roadmap" means the PRD, `refector.md`, or both is
a genuine ambiguity; MAR-191 resolves it by **updating all three**, which is
strictly safer than choosing.

**The classification rule, and its measured widening.** `AGENTS.md`'s existing
rule is *"if it sits under a `## MAR-NNN` heading it is almost certainly a
measurement"*, and every one of the eleven `22`-valued sites in `AGENTS.md`
(`:3498, :3669, :3798, :3909, :4011, :4105` and the Task-0 rows above them)
resolves correctly under it: all are historical, none may be edited. But E9
measured two sentences the criterion cannot classify. So MAR-191 lifts the
widened form into `## Repo facts that outlive their story`, citing both:

> **A sentence is historical if it is anchored — by a `## MAR-NNN` heading, by a
> date, or by a narrated event — regardless of where it sits.**
> `discription.md:59` opens *"MAR-182는 2026-08-30에 … 완료했다"* and then states
> the registry as 64: the date anchors the whole sentence, and the repair is a
> parenthetical naming today's 66, never an overwrite. `AGENTS.md:456`'s
> *"restored it to 250 objects and 22/22"* narrates one review's measurement
> inside a durable entry and is historical for the same reason.

`AGENTS.md`'s own rule — lift the general form once a second instance appears in
a second place — is what authorises this, and the two instances are named so the
entry is self-checking.

**Consequence for the edits.** Every AC5 change is one of exactly two moves:
**overwrite** a present-tense claim, or **append a parenthetical** to an anchored
one. A third move — deleting a stale sentence — is not available, because a
deletion of an anchored sentence falsifies the record just as an overwrite does.

### 2.5 The importer-order limitation, and the half of it that **is** measurable

Three stories have carried *"Photoshop's real layer-record order is unverified
and unverifiable here"* unchanged. The claim is true and MAR-191 does not
challenge it: obtaining a Photoshop-authored PSD requires Photoshop, which is not
a macOS limitation and not something any agent can produce.

**But the claim conflates two questions, and only one of them is about
Photoshop.** Measured at `b127048`:

- `psd_import.cpp:490-501` maps `lsct` 1/2 → `LayerSectionType::GroupStart` and
  `lsct` 3 → `GroupEnd`;
- the record walk at `:729-745` pushes on `GroupStart` and pops on `GroupEnd`,
  and returns *"PSD folder end marker appeared without an open folder."* when a
  pop finds `active_groups` empty;
- **there is no reversal anywhere**: `grep -n 'reverse\|rbegin'
  src/editor/psd_import.cpp` returns nothing, so records are consumed in file
  order.

So *"which order does the importer accept?"* is fully answerable here, and
*"which order does Photoshop emit?"* is not. What no story has done is measure
the first. MAR-191 adds **one case**, `O1`, to `psd_import_smoke.cpp`: synthesise
a PSD carrying a group in the **inverse** record order — divider first, children,
folder record last — and assert it is refused **by that exact message**.

What O1 buys, stated without inflation:

- it converts *"if the real order is the inverse, every group-bearing real PSD
  fails at `psd_import.cpp:734`"* from an inference into a **measured
  conditional**: the failure mode is now demonstrated, and only the antecedent
  remains unknown;
- it gives the human who eventually opens a real PSD a named message to
  recognise instead of an unexplained parse error;
- it is a **gate, not a witness** — it is red today, because no such case exists,
  and it goes red again if anyone makes the parser order-agnostic without saying so.

What it does **not** buy: it does not tell anyone which order Photoshop writes.
The inventory row stays class **G** and stays open. Saying otherwise would be a
correct measurement answering a question adjacent to the one asked.

O1 reuses MAR-188's synthesiser, which is gated on reproducing the fixture's
layer report, bone report, extracted pixels and header (Q0/Q0b) — so the
synthesiser is trustworthy for this by an argument MAR-188 already measured.

### 2.6 Acceptance criteria satisfied in form by more arms than in evidence

The brief's framing is right and its instance was invented (E1). Rebuilt from
measured cases, **five** real instances, each an inventory row:

| Criterion | Satisfied in form by | Satisfied in evidence by | Where measured |
|---|---|---|---|
| MAR-189 AC3 "an injected failure after **every** step" | **15** sweep arms (`kAllCommitSteps`) | **4** arms carry a story-owned inversion — I3→`PlaceSkeleton`, I4→`PlaceAtlas`, I12→`CleanJournal`, I13→`BackupTexture` | MAR-189 §7, counted here. **Prospective** |
| MAR-189 AC3, the **validate-only** arms | 3 arms — `ValidateRequest`, `PruneUnpreserved`, `ValidateStagedBundle` | **0.** These three touch nothing in the target bundle, so a byte-map clause comparing before against after passes **with rollback entirely broken** | Relayed by the team lead as the finding beneath E1's lost quotation, and **not independently verified here** — MAR-189 does not exist. Recorded as a **prospective** row whose wording is "a row that should exist", and MAR-189's implementer has been asked to label the three arms |
| MAR-186 severity assignment | **7** assignments | **0** inversions | `diagnostics.cpp`; the debt §2.3 closes |
| MAR-187 "the siblings guard the erase" | **8** erase arms | **6** with a detector; 2 (`DrawOrder`, `Event`) have no finer key and nothing to lose | Already measured and corrected in `AGENTS.md` — the model for this table |
| MAR-185 exhaustive switches | **25** `-Wswitch` sites | **11** switch-site inversions; 14 deliberately uninverted and named | MAR-185; the arithmetic 14 + 11 = 25 is its own check |
| MAR-189 AC5 "approval executes" | the `Approve` button | `apply_agent_review`, the handler behind it | MAR-189 §9.1. **Prospective** |

MAR-187's row is the template: review put the uncovered count at five, and
measuring arm by arm produced two-with-no-finer-key and one-already-covered. The
lesson the table encodes is that **"in form" and "in evidence" both have to be
counted, and neither may be inferred from the other.**

### 2.7 EXDEV, restated correctly

The brief's entry is replaced by a two-part row, because the two parts have
different owners:

- **Structurally unreachable on the production path.** `write_file_atomically`
  creates its temporary in the destination's own directory precisely so the
  rename cannot cross a boundary. This is a *property of the design*, not a gap,
  and it belongs in the inventory as class **C** (unreachable), not as an
  untested case.
- **Unreachable as a real event in this environment.** `/tmp`, the repository and
  the scratchpad are device `16777232`. A real EXDEV would need a second volume;
  a macOS RAM disk (`hdiutil attach -nomount ram://…` then
  `diskutil eraseVolume`) would produce one, but it mounts a volume on the user's
  machine and is **not** taken without explicit approval. Class **G**.
- **Message coverage is available and cheap.** `RenameCallback` can return
  `std::errc::cross_device_link`, which exercises the error path and its wording
  without either of the above. If AC4's "every PSD parse/build/commit failpoint"
  is read as requiring a cross-device arm, this is the satisfier — and MAR-191
  says so rather than letting a reviewer discover the volume question late.

### 2.8 Measurement hygiene, made structural

Four rules, each because something in this chain already went wrong without it.

1. **Every measurement comes from an isolated `git archive HEAD | tar -x` tree**
   in a scratch directory **named after the agent**, never after the story. The
   live worktree mutated under this design twice (§0.4). If a directory you did
   not create is already there, `rm -rf` and re-extract; `tar -x` **overlays**.
2. **Every restore is verified by a `cmp` naming absolute paths for the file, the
   baseline and the comparison** — and MAR-191 does this **itself**, in its own
   step, rather than trusting `invert.sh`'s, because that one is silent on
   failure (E8). MAR-191 does **not** edit `tools/inversion/` while MAR-189 and
   MAR-190 may be using it; the fix is offered to a later story as an inventory
   row, which is MAR-189's own F6 reasoning applied consistently.
3. **Every "expect zero" gate is run once against a deliberately planted hit and
   then the hit is removed.** A zero is evidence only once the pattern is known
   to match something: the shell can eat the glob and BSD `grep` can reject the
   pattern silently. Prefer `grep -F` for literals.
4. **Stage hunks, not files** (`git apply --cached`). `src/samples/psd_import_smoke.cpp`
   is held by MAR-189's implementer **right now**, and O1 (§2.5) lands in that
   exact file. This is not a hypothetical shared path.

### 2.9 What MAR-189 and MAR-190 must deliver first

Ordered by how badly MAR-191 is blocked. Everything here is **prospective**.

| # | Needed from | What | Blocks | If absent |
|---|---|---|---|---|
| 1 | MAR-189 | `commit_psd_reimport` and the `PsdCommitStep` failpoint seam | **AC4** entirely — "every PSD parse/build/commit failpoint proves atomic rollback" has no other satisfier | AC4 is satisfiable only for **Save/Save As** (MAR-180 S1, C3). Report AC4 as half-met, naming the half, and do not simulate the other |
| 2 | MAR-189 | The tracked-asset guard (its §9.4 A7): every committing case copies the bundle to temp, and the tracked bundle is byte-identical at end of run | **§2.10** — CTest registration of the PSD smoke | Do not register. The smoke would write tracked assets on every `ctest` run |
| 3 | MAR-189 | Resolution of the 14-vs-15 step count (E2) | AC4's arm count and its form-vs-evidence row (§2.6) | Report the inconsistency; count the enum, not the prose |
| 4 | MAR-190 | The review modal and `expect_reimport_no_op` | **AC1**'s "PSD reimport" clause and **AC3**'s "reviewed PSD reimport" | AC1/AC3 hold for the other fourteen behaviours; the PSD clause is reported unmet with the story named |
| 5 | MAR-190 | Whether AC6's "rollback errors" was read as *commit failure with verified rollback* (its §9.4) | How MAR-191 words AC4's result | Adopt MAR-190's reading and say so; a **failed rollback** is unreachable without a second seam neither story builds |
| 6 | Either | Registry movement | The eleven `!= 66U` guards, `agent_dispatch_smoke.cpp:42`, `test_client.py:53,55`, and the AC5 prose sites | MAR-189 §F3 predicts **no** movement (`import.psd_layers` already exists). **Re-derive; do not trust the prediction** — MAR-189 §9.3 flags a reviewer reading of AC5 that would make it 67 |

**MAR-191 does not implement any of the above.** If Task 0 finds a signature
missing, that is a blocking finding to report — not a gap to design around.

### 2.10 Registering `marrow_psd_import_smoke` in CTest — conditional

Measured at `b127048`: the smoke defaults its two inputs to
`assets/fixtures/…` **relative** paths (`psd_import_smoke.cpp:25-26`), so it
needs `WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}` exactly as
`marrow.agent_dispatch_smoke` does; it writes only under
`temp_directory_path()` (`:1595`, `:1620`, `:1624`); and it reads two tracked
fixtures by absolute path at `:885-886` without writing them. Registration is
therefore a five-line `add_test` with the `editor` label, and it closes an
inventory entry carried by two stories.

**It is conditional, and the condition is MAR-189.** MAR-189 makes this smoke
**write files**, and its own §9.4 calls a committing test pointed at the tracked
fixture *"the single most likely way MAR-189 damages the repository"*. So:

- register **only if** MAR-189's tracked-asset guard exists (§2.9 row 2), and
- **verify it by running `git status --porcelain assets/` after the full CTest
  run** — an empty result is the gate, and the gate is run once against a
  planted modification first, per §2.8 rule 3.

Cost if taken: `ctest -N` **22 → 23**, `grep -c add_test` **25 → 26**. Measured
consequence for prose: **none**. Every `22` in `AGENTS.md` sits in a `## MAR-NNN`
validation section and is historical; the `## Current Validation` line
(`- Focused CTest guardrail discovery: ctest --test-dir build -N`) carries no
number. That is why this is cheap, and it was checked rather than assumed.

If the condition fails, the entry stays in the inventory with "blocked on
MAR-189's §9.4 guard" as its reason. **Not registering is the safe default.**

### 2.11 What MAR-191 does not do

- **It does not fix anything in the inventory.** The 22 class-E entries are real
  product defects and every one is another story's work. This is a **mandate**,
  not a preference, and the plan enumerates all 22 by name and owner so the
  boundary is a lookup rather than a judgement made under pressure. A row the
  implementer is tempted to fix becomes a backlog entry with an owner, never a
  diff. The single assigned exception is MAR-186's severity gap (§2.3), which was
  deferred here **by name** by two stories.
- **It does not edit any `## MAR-NNN` validation section.** Not to correct a
  number, not to add a cross-reference, not to mark an entry closed.
- **It does not touch `tools/inversion/`** (§2.8 rule 2).
- **It does not change `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json`
  v1, or the `.marrow` schema.** AC2 requires this and it is the one AC that is
  satisfied by a **zero-line diff** rather than by an assertion — proved the way
  MAR-180 proved its registry was unchanged, by `git diff`, not by a test.
- **It does not add `-Wall`/`-Wextra` or `-Werror=switch`.** Both are recorded
  mitigations; both are their own work.

---

## 3. The four degenerate gate shapes, checked against this design

Each of the four shipped somewhere in this chain. Each is checked here against
MAR-191's own cases, because a validation story writing a degenerate gate has
failed at its only job.

**1. Fails on correct code.** The live trap is the severity register: a flip that
reddens a case for the *wrong reason* — say, by changing a message string a case
also asserts — reads as a bite and is not one. Countermeasure: each of V1–V7
records the **exact failure text**, and a bite is only credited when the reddened
clause is a *severity* clause. A count clause reddening is a bite; a message
clause reddening is a report of an over-broad mutation.

**2. Passes on unchanged code.** The whole story is exposed, because almost all of
its evidence is **re-run** evidence. Every case MAR-191 re-runs was green before
MAR-191 existed, and none of them is a gate for *this* story. The countermeasure
is labelling, applied without exception: **AC1, AC2, AC3 and AC6 are satisfied by
witnesses, not by gates**, and the story's write-up says so in those words. The
three things that are gates are named and are red today: **V1–V7** (no such
mutation has been run), **O1** (no such case exists), and the **AC5 prose
sweep** (the stale numbers are measurably wrong right now). Anything else
reporting green is a witness.

**3. A mutation that is a provable no-op.** The trap is inverting a *tally*
instead of an *assignment*. Mutating `finalize`'s counter at `diagnostics.cpp:129`
would redden `check_invariants` — but that is MAR-186's already-inverted I14
(swapped accumulators), and re-running it proves nothing new. Worse, mutating a
severity and its tally together is provably unobservable, since both sides move.
V1–V7 mutate **only** the seven `make_issue` arguments, and §2.3's table names
the line for each so an implementer cannot drift onto `:129`.

**4. Self-satisfying — a clause testing an artefact the operation creates, or a
value the design defines.** Two live traps, both written into the plan rather
than left to attention:

- **The inventory must not be validated against itself.** A gate asserting "every
  `### Not independently covered` bullet appears in the inventory" is
  self-satisfying if the inventory was *built by* that sweep. The falsifiable
  form is a **count comparison against state captured before**: the 74-bullet
  census is taken in Task 0, from the pristine tree, **before** the inventory is
  written, and the inventory is checked against **that** number. Compare against
  state captured before the operation, never against a value the operation
  defines.
- **The AC5 sweep must not check for the value it just wrote.** Grepping for
  `66` after replacing `64` proves the `sed` ran. The falsifiable form is the
  mirror: after the edits, **`grep -rnF '64 ops'` and the six stale
  milestone-pointer strings must return zero across the five documents**, with
  each pattern first proven capable of matching by running it before the edit.

---

## 4. Where each piece lands

| Path | Change |
|---|---|
| `AGENTS.md` | New `## Editing P1 Limitation Inventory` after `## Repo facts…`; new durable entry widening the historical-sentence criterion (§2.4); new `## MAR-191 …` validation section; `## Project State` corrected (E5); `## Current Validation` gains MAR-189/190/191 lines |
| `docs/root1/editing-gap-analysis.md` | `:3` header; `:47`, `:224`, `:288` milestone pointers; `:95` ×2 and `:455` registry count; MAR-189/190/191 rows in the story table marked complete |
| `docs/root1/discription.md` | `:61`, `:892` milestone pointers; `:59`, `:60` get parentheticals, **not** overwrites (§2.4) |
| `docs/root1/refector.md` | `:20` "current registry"; `:333` next milestone |
| `docs/root1/format-spec.md` | MAR-189/190 schema delta **only if one exists**; otherwise untouched and stated as untouched |
| `.agents/tasks/prd-marrow-runtime.json` | MAR-189/190/191 `status` |
| `src/samples/psd_import_smoke.cpp` | **O1 only** (§2.5). Staged with `git apply --cached` |
| `CMakeLists.txt` | **Conditional**, one `add_test` (§2.10) |
| `src/editor/diagnostics.cpp` | **Mutated and restored seven times. Committed change: none.** |

---

## 5. Acceptance criteria → artifacts

| AC | Satisfied by | Proved by | Kind |
|---|---|---|---|
| **AC1** E2E over fifteen named behaviours | The existing suite, re-run at the tip | Full CTest, both smokes, the shell scenarios | **Witness** for fourteen; **prospective** for PSD reimport (MAR-190) |
| **AC2** old and new projects survive save/reload; exports equivalent; no format change | `marrow_project_smoke` + `marrow_inspect --compare`; the format half by **zero-line `git diff`** on the schema surfaces | Existing P- and S-cases; `git diff --stat` | **Witness** + a diff-proved non-effect |
| **AC3** C++/MCP parity over nine mutations; five surfaces unexposed | Registry 66 re-derived; `test_client.py` name-set equality; the eleven guards | `marrow_agent_dispatch_smoke` (**437**), `test_client.py`, `py_compile` | **Witness**; parity is count- and name-only (inventory entry) |
| **AC4** Save/Save As and every PSD failpoint prove atomic rollback | MAR-180 S1/C3 for Save; MAR-189's sweep for PSD | S1, C3; **prospective** R3 | Half **witness**, half **prospective**. §2.9 row 1 |
| **AC5** five documents reflect the implemented schema | §2.4 edits + the mirrored zero-result sweep (§3, shape 4) | `grep -rnF` over six stale strings, each pre-proven to match | **GATE** — red today |
| **AC6** final validation passes the nine commands | The command list, run at the tip | Recorded verbatim with counts | **Witness**, plus **V1–V7** and **O1** as this story's only gates |

**Four of six criteria are satisfied by witnesses.** That is the honest shape of
a validation story and it is stated in the design rather than discovered in
review. The story's *own* falsifiable content is V1–V7, O1 and the AC5 sweep.

---

## 6. Cases and the rules they obey

### 6.1 The rules

1. **Every case is proven falsifiable and uniquely attributed by run order.** For
   V1–V7 this means the *first* reddening case is recorded, and every additional
   reddening case is recorded as over-determination rather than folded into it.
2. **No case compares against a value this story defines.** §3, shape 4.
3. **A red run is evidence only once the build is known sound.** Every inversion
   deletes objects under `<build>/CMakeFiles` — the `/CMakeFiles` is load-bearing;
   the unscoped form rebuilds `libSDL3.a` at 96 bytes and produces a phantom
   two-test regression with the sources provably untouched.
4. **A zero is evidence only once the pattern is known to match.** Every
   expect-zero grep is run against a planted hit first.
5. **A case green before the implementation exists is a witness.** Labelled, with
   the inversion that would catch its subject named or its absence stated.

### 6.2 New cases

| # | Where | What |
|---|---|---|
| **O1** | `psd_import_smoke.cpp` | A synthesised PSD whose group records are in the inverse order is refused by the exact message *"PSD folder end marker appeared without an open folder."*; a control PSD in the accepted order still parses. §2.5 |
| **V1–V7** | `diagnostics.cpp`, mutated only | The seven severity flips. §2.3 |
| **X1** | Task 0, `AGENTS.md` | The 74-bullet / 8-section census, taken **before** the inventory is written. §3 shape 4 |
| **X2** | Post-edit, five documents | Six stale strings return zero, each pattern pre-proven against a planted hit. §3 shape 4 |

### 6.3 Inversion register

| # | Mutation | Predicted first failure | Why nothing earlier |
|---|---|---|---|
| **V1–V7** | §2.3's table | §2.3's table | The seven sites are disjoint; no case reads two severities as one value |
| **I-O1** | Make the record walk order-agnostic (accept a pop on an empty `active_groups` as a no-op) | **O1**: the inverse-order PSD parses instead of being refused | Every existing PSD case uses the accepted order, so an order-agnostic walk is invisible to all of them |
| **I-X2** | Revert one of the six AC5 edits | **X2**: the stale string reappears with a non-zero count | Nothing else reads those documents |

**Deliberately uninverted, by name.** The re-run suite: MAR-191 owns none of it,
and re-inverting twenty-three stories' cases is not this story's work — each was
inverted by its own story and those registers stand. The inventory table itself:
a table is data, and a mutation of a row changes no observable behaviour — a
provable no-op of MAR-188's I7 shape, recorded rather than written.

---

## 7. Facts to MEASURE in Task 0, not assume

| # | Claim | How | Consequence if it differs |
|---|---|---|---|
| T1 | The parent commit and its trailers | `git log -1 --format='%(trailers)' HEAD` | The `Claude-Session:` value is copied from it; three consecutive stories shipped without it |
| T2 | `ctest -N`, `-L runtime`, `-L editor` | Configure the archive tree | Every baseline in §1 |
| T3 | `marrow_agent_dispatch_smoke` `[ OK ]` count | Build and run it | **Re-measure. 437 is this design's number at `b127048`, not a constant** |
| T4 | Registry count; the eleven guards; `agent_dispatch_smoke.cpp` bound line; `test_client.py` lines | `grep`; re-derive every line number | MAR-180 recorded `:39` and it measured `:42` |
| T5 | MAR-189's and MAR-190's actual delivered signatures | `grep` the headers | §2.9's whole table |
| T6 | The 74-bullet / 8-section census | Script over `AGENTS.md`, **before** writing the inventory | X1's falsifiability |
| T7 | The six stale documentation strings each match **before** the edit | `grep -rnF`, once each | X2's falsifiability |
| T8 | `tools/mcp/venv` exists and `test_client.py` imports resolve | `ls`; `py_compile` | The venv is **gitignored**, so it is absent from any `git archive` tree. AC6's MCP half must run against the live worktree's venv or create one |
| T9 | The seven severity sites are still at `:203, :456, :504, :544, :596, :629, :764` | `grep -n 'DiagnosticSeverity::' src/editor/diagnostics.cpp` | Line anchors drift; MAR-185 found three stale ones in its own document |
| T10 | `git status --porcelain` before and after every full run | Run it | The worktree is shared with a live implementation (§0.4) |

---

## 8. Known limitations, stated rather than hidden

- **This story's own evidence is mostly re-run evidence.** §5 says which four
  criteria that applies to. A green re-run of another story's case is a
  regression witness and nothing more; it is not evidence that MAR-191 works,
  because MAR-191 has almost nothing that can work.
- **The inventory is a document, not a mechanism.** Nothing enforces that a
  future story adds its `L#` row. The only guard offered is that the inventory
  sits in the file the next agent reads. A CI check comparing bullet counts to
  row counts was considered and rejected: it would fire on every legitimate
  de-duplication, which is the operation the inventory exists to perform.
- **V6's detector is unknown before the run** (§2.3), and "no detector" is a
  permitted outcome.
- **AC4 is at most half-satisfiable without MAR-189**, and AC1/AC3's PSD clauses
  are unsatisfiable without MAR-190. Neither is a MAR-191 defect.
- **AC6's MCP client half is manual.** `test_client.py` is in no CTest target and
  needs a running editor with the agent socket listening. AC6 is satisfied at
  that standard and no higher — the same standard MAR-180 and MAR-185 met.
- **The headless renderer cannot render on this host** (`Failed to create a Metal
  device`), so AC6's renderer clause is satisfied by `--skip-render`, which is
  what AC6 names. An actual rendered frame needs an interactive Metal host.
- **No acceptance criterion is believed unsatisfiable on macOS as written.** The
  four that are at risk are at risk for other reasons: AC4 and parts of AC1/AC3
  wait on unimplemented stories; AC6's MCP half is manual by construction. The
  nearest thing to a macOS limitation is AC4's cross-device arm, and §2.7 shows
  it is unreachable **by design** rather than by platform.

---

## 9. Risks for the implementer

1. **The largest risk is scope, and it is designed against rather than noted.** A
   73-row inventory of real defects, read by an agent whose story is called
   "validate", invites fixing — and the cheapness of a one-line fix is not
   evidence that it is in scope. The plan carries an explicit **non-fixing
   mandate** naming all 22 class-E rows with their recording stories and their
   owners, so the boundary is checkable rather than a judgement call at 2am. Add
   rows with owners; fix nothing. One assigned exception: §2.3.
2. **Do not edit a `## MAR-NNN` section.** Not even to add a cross-reference. The
   inventory carries the pointers; the sections carry the record.
3. **Do not report "seven inversions, seven bites."** Per-site failure text, or
   the claim is an inference wearing a measurement's grammar. This chain spent
   three passes unwinding one of those.
4. **V6 may not bite. That is a result.** Do not invent an assertion to close it.
5. **`src/samples/psd_import_smoke.cpp` is held by MAR-189's implementer right
   now.** O1 lands there. `git apply --cached`, hunks not files.
6. **Re-measure 437, 66, 22 and every line number at your actual parent.** A
   carried number and a re-measured number can be numerically identical and
   epistemically different; this design's 437 is the latter and yours must be too.
7. **Scope the object deletion**: `find build/CMakeFiles -name '*.o' -delete`.
8. **Name your scratch directory after yourself, not after MAR-191.**
9. **`cmp` with absolute paths only**, and do your own — `invert.sh`'s is silent
   on failure (E8) and you must not fix it here.
10. **One commit. Korean subject and body. Both trailers in one contiguous block
    with no blank line between them.** Three consecutive stories shipped without
    `Claude-Session:` and each needed an amend.
