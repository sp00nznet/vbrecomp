#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Does the picture MOVE? -- the question shotdiff.py cannot answer.

shotdiff.py measures how far our frame is from hardware's. It cannot tell
"identical because we render correctly" from "identical because both sides are
parked on the same boot screen", and that distinction turned out to cover most
of its 0.0% rows.

This compares each side against ITSELF across its own frame window:

    ourmove   most our picture changes over frames 300 -> 900
    refmove   how much hardware's changes over its 900 -> 2400
    bestlit   the shotdiff.py number, for context
    onscreen  fraction of our last frame that is lit at all

    ourmove 0, refmove > 0   -- we are GATED: hardware advances, we do not
    ourmove 0, refmove 0     -- both parked; a matching score here proves nothing
    ourmove > 0              -- we are advancing, so a gap is rendering or pacing

Reads the shots shotdiff.py already cached; no emulator launches.

    python scripts/reference/motion.py
"""
import json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from shotdiff import shades, ROOT, OUR_FRAMES, REF_FRAMES


def lit_diff(a, b):
    """Fraction of pixels where one side is black and the other is not."""
    return sum(1 for i in range(len(a)) if (a[i] == 0) != (b[i] == 0)) / len(a)


def main():
    surv = json.load(open(os.path.join(ROOT, "corpus", "render_survey.json"),
                          encoding="utf-8-sig"))
    rows = []
    for r in surv:
        if r["cat"] != "RENDERS":
            continue
        slug = r["slug"]
        ours = [shades(p) for p in
                (os.path.join(ROOT, "corpus", "_timeline", slug, "f.png.%04d.png" % f)
                 for f in OUR_FRAMES) if os.path.exists(p)]
        refs = [shades(p) for p in
                (os.path.join(ROOT, "corpus", "_ref", "%s.%d.png" % (slug, f))
                 for f in REF_FRAMES) if os.path.exists(p)]
        if len(ours) < 2 or len(refs) < 2:
            continue
        ourmove = max(lit_diff(ours[0], o) for o in ours[1:])
        refmove = lit_diff(refs[0], refs[1])
        best = min(lit_diff(a, b) for a in refs for b in ours)
        onscreen = sum(1 for v in ours[-1] if v) / len(ours[-1])
        rows.append((ourmove, refmove, best, onscreen, slug))

    rows.sort()
    gated = [r for r in rows if r[0] == 0 and r[1] > 0]
    parked = [r for r in rows if r[0] == 0 and r[1] == 0]
    print("%-46s %8s %8s %8s %9s" % ("game", "ourmove", "refmove", "bestlit", "onscreen"))
    for om, rm, bl, on, slug in rows:
        print("%-46s %7.1f%% %7.1f%% %7.1f%% %8.1f%%"
              % (slug[:46], om * 100, rm * 100, bl * 100, on * 100))
    print("\n%d rendering games: %d gated (frozen while hardware advances), "
          "%d parked on both sides, %d advancing"
          % (len(rows), len(gated), len(parked), len(rows) - len(gated) - len(parked)))


if __name__ == "__main__":
    main()
