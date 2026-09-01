# Inversion harness

Tooling for the inversion methodology this repository's stories use: apply a
deliberate defect to a source file, rebuild, confirm a named case *fails*, then
restore and rebuild. A case that never fails under the mutation it names is not
evidence, and `AGENTS.md`'s H1-H4 record how easily such a run lies.

These scripts exist because **two of the guards they implement were learned the
hard way, and a guard that lives only in one agent's scratch directory is not a
guard.** Both hazards below were hit independently by more than one agent.

| Script | What it does |
| --- | --- |
| `snapshot.sh` | Copies the files you are about to mutate into a pristine baseline directory |
| `rebuild.sh` | Deletes object files, then builds. With no object arguments -- which is how `invert.sh` calls it -- deletes every object under `<build-dir>/CMakeFiles` |
| `invert.sh` | mutate -> rebuild -> run -> record -> restore -> rebuild, with both guards |

## The two guards, and why each exists

**1. A failed mutation build must ABORT the run.** If the mutated source does not
compile and the harness runs the suite anyway, it exercises the *previous*
binary, which passes — and the result is recorded as "the inversion did not
bite". That is a false pass manufactured by the harness rather than by mtime.
Seen when a mutation used an initializer the compiler rejected.

**2. A restore is only as good as the copy it restores from.** Restoring from a
baseline captured *before* the implementation was finished silently reverts
finished work, and the next inversion then runs against the stub. `invert.sh`
refuses to mutate unless the baseline copy is byte-identical to the tree, so a
stale baseline is an error rather than a silent revert. Run `snapshot.sh` after
every implementation step.

## The guard was committed unwired, and is now wired

**Between its first commit and MAR-188, `rebuild.sh` deleted nothing when called
the way `invert.sh` calls it.** Both call sites (`invert.sh:21` in `restore` and
`:28` in GUARD 1) pass only the build directory; `rebuild.sh` then `shift`ed and
ran its `for` loop over an empty `$@`. Every inversion therefore fell back to
make's mtime comparison -- the precise hazard the harness was written to prevent.

Demonstrated on one unchanged tree, three ways -- the point being a rebuild that
*would otherwise have been skipped*:

| Invocation, unchanged tree | Files recompiled |
| --- | --- |
| `cmake --build build -j8` (what make does alone) | **0** |
| `rebuild.sh build` **before** the fix | **0** -- indistinguishable from doing nothing |
| `rebuild.sh build` **after** the fix | **123** |

Objects go 130 -> 130 across the fixed run because they are deleted and rebuilt;
`libSDL3.a` stays intact at 13.9 MB because the deletion is scoped to
`CMakeFiles`. The middle row is the whole defect: the harness's central guard was
byte-for-byte as effective as not calling it.

Two agents found this independently, which is the useful part. **A guard that is
committed but not wired is worse than one that is absent**, because a README then
asserts it is working and every reader downstream believes it -- the same shape as
a frame-body gate that passes on a broken tree. When you commit a guard, run the
thing it guards against and watch it fire; "the code is present" is not the check.

## Why object deletion rather than `touch`

The generator is `Unix Makefiles` with GNU Make 3.81, whose one-second mtime
granularity means a `touch`ed source and the object written by the immediately
preceding build can share a second — so the rebuild is skipped. Deleting the
object removes the comparison from the question. See `AGENTS.md` H1.

## Usage

```sh
tools/inversion/snapshot.sh <build-dir> <file>...
tools/inversion/invert.sh <build-dir> <label> <file> <mutation.py> [run-command...]
```

`mutation.py` receives the file path as `$1` and must apply the defect,
asserting its own anchor count so a non-applying mutation is an error rather
than a green run against unmutated code. That assertion has already caught one
mutation whose anchor string was a **substring** of another occurrence.
