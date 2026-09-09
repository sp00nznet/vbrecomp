#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare what we draw against what the reference emulator draws.

Everything else in scripts/reference/ compares numbers -- how much of WRAM is
non-zero, which pages differ. That was the right tool while games were blank.
Now that most of them draw, the question is whether what they draw is *right*,
and the only honest answer is the reference emulator's own picture.

    python scripts/reference/shotdiff.py                # every rendering game
    python scripts/reference/shotdiff.py Bound_High_Japan_En_Proto ...

For each game it boots BizHawk's Mednafen VB core to hardware frame 400 holding
the same buttons the driver injects, screenshots it, and compares against our
frame-900 shot (one hardware frame is ~2.25 of ours, so those line up).

The two emulators tint the display differently -- ours red, BizHawk whatever its
VB palette is set to -- so colour is ignored. Both quantise to the Virtual Boy's
four shades, which is what is compared: `shade` is the fraction of pixels on a
different shade, `lit` the fraction where one is black and the other is not.
`lit` is the number that finds real defects; a whole-screen brightness
difference moves `shade` and leaves `lit` alone.

Reference shots are cached in corpus/_ref/ -- delete one to re-capture.
"""
import json, os, subprocess, sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REF_DIR = os.path.join(ROOT, "corpus", "_ref")
BIZHAWK = os.environ.get("BIZHAWK", r"G:\recomp\snes\bizhawk\EmuHawk.exe")
REF_FRAMES = [400, 900, 1800, 3600]   # one BizHawk launch each; see shot.lua
# Four, spread wide, because we usually run AHEAD of hardware: our frame is a
# budget of interrupt checks, not 1/50s. BLOX is on its title screen at our
# frame 900 and still on the warning screen at hardware frame 400 -- scored
# 73.8% different, all of it drift. Give hardware room to catch up.
OUR_FRAMES = [300, 600, 900]   # what render_survey.ps1 leaves behind


def shades(path):
    """384x224 of 0..3, ignoring hue: the VB has four shades and so does this."""
    im = Image.open(path).convert("RGB")
    px = im.load()
    w, h = im.size
    vals = [max(px[x, y]) for y in range(h) for x in range(w)]
    peak = max(vals) or 1
    # 0 is off; split the rest into three bands of the observed peak.
    return [0 if v <= peak * 0.12 else
            1 if v <= peak * 0.42 else
            2 if v <= peak * 0.72 else 3 for v in vals]


def capture(slug, rom):
    """One reference shot per frame point, cached. Delete a file to re-capture.

    A launch per frame is not laziness on BizHawk's part being worked around --
    client.screenshot() is deferred, so several shots in one run all receive the
    final framebuffer and look, convincingly, like a settled screen.
    """
    out = []
    lua = os.path.join(ROOT, "scripts", "reference", "shot.lua")
    for f in REF_FRAMES:
        path = os.path.join(REF_DIR, "%s.%d.png" % (slug, f))
        if not os.path.exists(path):
            os.makedirs(REF_DIR, exist_ok=True)
            env = dict(os.environ, VBREF_FRAME=str(f), VBREF_OUT=path)
            try:
                subprocess.run([BIZHAWK, rom, "--lua=" + lua], env=env, timeout=300,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except subprocess.TimeoutExpired:
                pass
        if os.path.exists(path):
            out.append(path)
    return out


def main():
    surv = json.load(open(os.path.join(ROOT, "corpus", "render_survey.json"),
                          encoding="utf-8-sig"))
    want = set(sys.argv[1:])
    rows = []
    for r in surv:
        if r["cat"] != "RENDERS":
            continue
        if want and r["slug"] not in want:
            continue
        ourdir = os.path.join(ROOT, "corpus", "_timeline", r["slug"])
        ourshots = [os.path.join(ourdir, "f.png.%04d.png" % f) for f in OUR_FRAMES]
        ourshots = [q for q in ourshots if os.path.exists(q)]
        rom = os.path.join(ROOT, "roms", r["name"] + ".vb")
        if not ourshots or not os.path.exists(rom):
            continue
        refs = capture(r["slug"], rom)
        if not refs:
            print("  no reference capture: %s" % r["slug"])
            continue
        # Best pairing over both sides' frames. The two emulators do not share a
        # clock, so any single pairing measures drift rather than rendering; the
        # best match is the closest thing to "same moment, different renderer".
        best = None
        for q in refs:
            a = shades(q)
            for o in ourshots:
                b = shades(o)
                n = len(b)
                lit = sum(1 for i in range(n) if (a[i] == 0) != (b[i] == 0)) / n
                shade = sum(1 for i in range(n) if a[i] != b[i]) / n
                cand = (lit, shade, os.path.basename(q), os.path.basename(o))
                if best is None or cand < best:
                    best = cand
        rows.append((best[0], best[1], r["slug"], best[2], best[3]))

    rows.sort(reverse=True)
    print("%-46s %7s %7s  %s" % ("game", "lit", "shade", "best pairing"))
    for lit, shade, slug, ref, our in rows:
        print("%-46s %6.1f%% %6.1f%%  %s vs %s"
              % (slug[:46], lit * 100, shade * 100,
                 ref.rsplit(".", 2)[-2], our.rsplit(".", 2)[-2]))


if __name__ == "__main__":
    main()
