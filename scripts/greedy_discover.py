#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Greedy profile-guided discovery: add harvested entry points one at a time,
and keep each only if the game's screens do not get worse.

scripts/discover.ps1 applies everything it harvests at once. That is fine for
most games and ruinous for some: one harvested entry can blank a game on its
own (Wario Land's 07FE40B0 is one of 26; Elevated Speed's 07E01A30 one of 7),
and applied wholesale it takes the good entries down with it. This tries each
candidate alone on top of what has been accepted so far:

  1. build and run the game with the current hints (headless, 3,600 frames,
     Start then A pressed, a screenshot every 300 frames, heartbeat on);
  2. score it: distinct screenshots, minus two for every blank one, minus ten
     if the run died before the last screenshot;
  3. for each unimplemented call target the run reported (cartridge ROM,
     halfword-aligned), rebuild with it added; keep it if the score is no
     lower, otherwise write it into the hints file commented out
     (`# entry X: rejected ...`) so discover.ps1 will not harvest it back;
  4. repeat with the targets the kept entries exposed.

    python scripts/greedy_discover.py Virtual_Boy_Wario_Land_Japan_USA_En
    python scripts/greedy_discover.py Mario_Clash_Japan_USA_En --rounds 8

Needs the corpus built (scripts/build_all.ps1) and the ROM in roms/. Writes
games/hints/<slug>.hints.txt and leaves corpus/<slug> and the executable built
from the final hints. A score is a coarse proxy for "got further": look at the
pictures before believing a gain.
"""
import argparse, glob, hashlib, os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAMES, EVERY, BLANK_BYTES = 3600, 300, 3700   # an all-black 384x224 PNG is 3,619 bytes


def slug_of(path):
    return re.sub(r"[^A-Za-z0-9]+", "_", os.path.basename(path)[:-3]).strip("_")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("slug")
    ap.add_argument("--rounds", type=int, default=6)
    ap.add_argument("--press", default="1000,0004")
    a = ap.parse_args()

    roms = [f for f in glob.glob(os.path.join(ROOT, "roms", "*.vb")) if slug_of(f) == a.slug]
    if not roms:
        sys.exit("no ROM in roms/ for slug %s" % a.slug)
    rom = roms[0]
    hints = os.path.join(ROOT, "games", "hints", a.slug + ".hints.txt")
    tool = os.path.join(ROOT, "build", "Release", "v810recomp.exe")
    exe = os.path.join(ROOT, "build_all", "games", "Release", "corpus_%s.exe" % a.slug)
    tmp = tempfile.mkdtemp(prefix="greedy_")

    def known(ls):
        return {m.group(1).upper() for l in ls
                if (m := re.match(r"\s*#?\s*entry\s+([0-9A-Fa-f]{8})", l))}

    def build(ls):
        hf = os.path.join(tmp, "hints.txt")
        with open(hf, "w") as f:
            f.write("\n".join(ls) + "\n")
        subprocess.run([tool, rom, os.path.join(ROOT, "corpus", a.slug), "--hints", hf],
                       capture_output=True)
        r = subprocess.run(["cmake", "--build", os.path.join(ROOT, "build_all"), "--config",
                            "Release", "--target", "corpus_" + a.slug], capture_output=True, text=True)
        return " error " not in r.stdout

    def run(tag):
        shot = os.path.join(tmp, tag)
        env = dict(os.environ, VBRECOMP_HEADLESS="1", VBRECOMP_HEADLESS_FRAMES=str(FRAMES),
                   VBRECOMP_PRESS=a.press, VBRECOMP_SHOT_PATH=shot,
                   VBRECOMP_SHOT_EVERY=str(EVERY), VBRECOMP_HEARTBEAT="1")
        try:
            err = subprocess.run([exe, rom], env=env, capture_output=True,
                                 timeout=900).stderr.decode(errors="replace")
        except subprocess.TimeoutExpired as e:
            err = (e.stderr or b"").decode(errors="replace")
        files = sorted(glob.glob(shot + ".*.png"))
        hs = {hashlib.md5(open(f, "rb").read()).hexdigest() for f in files}
        blank = sum(os.path.getsize(f) <= BLANK_BYTES for f in files)
        died = len(files) < FRAMES // EVERY
        stubs = sorted({s for s in re.findall(r"^\s+sub_([0-9A-F]{8}) x", err, re.M)
                        if re.fullmatch(r"07[0-9A-F]{5}[02468ACE]", s)})
        return len(hs) - 2 * blank - 10 * died, stubs

    cur = (open(hints).read().splitlines() if os.path.exists(hints) else
           ["# Profile-guided entry points for %s." % os.path.basename(rom)[:-3],
            "# Harvested and accepted one at a time by scripts/greedy_discover.py."])
    if not build(cur):
        sys.exit("the current hints do not build")
    best, stubs = run("base")
    print("%s: base score %d, %d candidate(s)" % (a.slug, best, len(stubs)), flush=True)
    for rnd in range(a.rounds):
        cands = [s for s in stubs if s not in known(cur)]
        if not cands:
            break
        exposed = set()
        for s in cands:
            trial = cur + ["entry " + s]
            if not build(trial):
                print("  %s: does not build, skipped" % s)
                continue
            sc, st = run(s)
            keep = sc >= best
            print("  round %d %s: score %d vs %d -> %s" % (rnd + 1, s, sc, best,
                                                          "keep" if keep else "drop"), flush=True)
            if keep:
                cur, best = trial, sc
                exposed |= set(st)
            else:
                cur = cur + ["# entry %s: rejected by greedy_discover.py (score %d < %d)" % (s, sc, best)]
        stubs = sorted(exposed)
    with open(hints, "w") as f:
        f.write("\n".join(cur) + "\n")
    build(cur)
    print("%s: final score %d; wrote %s" % (a.slug, best, hints))


if __name__ == "__main__":
    main()
