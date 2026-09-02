#!/usr/bin/env python3
"""X2 -- documented registry totals agree with the registry itself.

The oracle is COMPUTED from src/editor/agent_dispatch.cpp, never hardcoded: a
gate carrying its own copy of the number is the defect it exists to catch, one
story later.  Historical totals are legitimate and are pinned by anchor, not
erased -- a present-tense claim may be corrected, a historical measurement
must not.

THE ORACLE IS DERIVED; THE PINS ARE NOT, AND THAT IS DELIBERATE.  `EXPECTED` is
computed from the registry at run time.  `PINNED` holds LITERALS, written by hand
after measuring with `grep -o`.  It must stay that way: a `want` computed from the
same file `got` is measured from would compare a number to itself and pass on
everything -- a tautology, and the exact self-satisfying shape this gate exists to
avoid.  If you change a pinned document, re-measure and edit the literal.

KNOWN LIMITATIONS, stated rather than implied:

* The oracle is a TEXTUAL approximation of `std::size(kOperationSpecs)`.  A
  clang-format pass or a differently-shaped registry entry changes the census
  without the registry changing.  `assert EXPECTED > 0` is the mitigation: a
  reshape fails loudly instead of silently reporting zero and passing.
* The two clauses are NOT independent.  Reverting a present-tense 66 to 64 trips
  clause (1) AND clause (2), because it also changes the residue count.  The pin
  is not load-bearing for that case; it is load-bearing for a stale claim that
  carries no marker clause (1) recognises, which is the hole it was added for.
* A genuine stale claim written inside a blockquote or fenced code block is
  INVISIBLE to clause (1) -- measured, and the deliberate cost of not reddening
  on the methodology section's own quoted examples.  Clause (2) still sees it,
  which is a second reason the zero pins matter.
"""
import re, sys, pathlib

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
REGISTRY = ROOT / "src/editor/agent_dispatch.cpp"

# --- the oracle, derived ---------------------------------------------------
spec = REGISTRY.read_text(encoding="utf-8")
EXPECTED = len(re.findall(r'^    \{"', spec, re.M))
assert EXPECTED > 0, "registry census matched nothing -- the shape changed, fix the gate"

DOCS = ["AGENTS.md", "docs/root1/editing-gap-analysis.md",
        "docs/root1/discription.md", "docs/root1/refector.md"]

# --- (1) present-tense totals must equal the oracle -------------------------
PRESENT = [
    r'현재[^.]{0,90}?(\d+)\s*ops',
    r'이제[^.]{0,90}?(\d+)\s*ops',
    r'현재[^.]{0,90}?(\d+)-operation',
    r'\*\*current\*\*[^.]{0,90}?(\d+)-operation',
    r'\bcurrently\b[^.]{0,90}?(\d+)[\s-]operation',
]
# Historical residue, pinned by count -- INCLUDING THE ZEROS.  A zero pin is the
# clause that matters: it converts "this literal happens to be absent" into "this
# literal is ASSERTED absent", so a newly introduced stale claim trips the pin even
# when it carries no present-tense marker clause (1) would recognise.  Measured, not
# assumed; all eight document/literal pairs are covered.
#
# AGENTS.md's counts include the methodology entries' own QUOTATIONS of these
# literals -- an entry about stale-claim hazards necessarily contains stale claims.
# Editing those entries therefore trips this pin on purpose, forcing a re-measure.
PINNED = {
    ("AGENTS.md",                             "64 ops"):         5,
    ("AGENTS.md",                             "64-operation"):   3,
    ("docs/root1/editing-gap-analysis.md",    "64 ops"):         2,
    ("docs/root1/editing-gap-analysis.md",    "64-operation"):   0,
    ("docs/root1/discription.md",             "64 ops"):         0,
    ("docs/root1/discription.md",             "64-operation"):   2,
    ("docs/root1/refector.md",                "64 ops"):         0,
    ("docs/root1/refector.md",                "64-operation"):   0,
}

fail = []
print(f"oracle: {EXPECTED} operations, derived from {REGISTRY}")
for d in DOCS:
    p = ROOT / d
    raw = p.read_text(encoding="utf-8")
    # A gate over prose CLAIMS must not read quoted EXAMPLES.  A methodology
    # section documenting stale-claim hazards necessarily quotes stale
    # claims; without this the gate reddens on its own documentation.
    raw = re.sub(r"^```.*?^```", "", raw, flags=re.M | re.S)   # fenced code
    raw = re.sub(r"^\s*>.*$", "", raw, flags=re.M)             # blockquotes
    flat = re.sub(r"\s+", " ", raw)
    for pat in PRESENT:
        for m in re.finditer(pat, flat):
            n = int(m.group(1))
            ok = (n == EXPECTED)
            print(f"  [{'ok ' if ok else 'BAD'}] {d}: present-tense {n} (…{m.group(0)[:60]}…)")
            if not ok:
                fail.append(f"{d}: claims {n}, registry has {EXPECTED}")

# --- (2) historical residue pinned by count --------------------------------
for (d, lit), want in PINNED.items():
    got = len(re.findall(re.escape(lit), (ROOT / d).read_text(encoding="utf-8")))
    ok = got == want
    print(f"  [{'ok ' if ok else 'BAD'}] {d}: historical '{lit}' x{got} (pinned {want})")
    if not ok:
        fail.append(
            f"{d}: '{lit}' occurs {got}x, pinned {want}x. If you ADDED a claim, fix "
            f"the claim. If you edited a methodology entry that quotes this literal, "
            f"re-measure with `grep -o` and update PINNED.")

print()
if fail:
    print("X2 FAILED:"); [print("  -", f) for f in fail]; sys.exit(1)
print("X2 PASSED")
