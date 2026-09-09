#!/usr/bin/env python3
"""Build the corpus gallery page from the survey and the timeline screenshots.

The gallery was hand-built the first time and went stale immediately, which is
the usual fate of a page whose numbers are typed in. This regenerates it from
corpus/render_survey.json plus the PNGs render_survey.ps1 leaves in
corpus/_timeline/<slug>/, so refreshing it is one command:

    python scripts/gen_gallery.py [out.html]

Classification uses the screenshots themselves, so it always agrees with what
the page shows: a game the survey called NO-GRAPHICS is blank; otherwise the
300/600/900 frames are compared byte-for-byte and a game whose frames are all
identical is frozen, anything else advancing.

SPDX-License-Identifier: MIT
"""
import base64, hashlib, html, json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TIMELINE = os.path.join(ROOT, "corpus", "_timeline")
FRAMES = (300, 600, 900)


def load(slug):
    """Distinct frames for one game, as (base64 png, label) in sample order."""
    shots = []
    for n in FRAMES:
        p = os.path.join(TIMELINE, slug, "f.png.%04d.png" % n)
        if os.path.exists(p):
            shots.append((n, open(p, "rb").read()))
    out, run = [], []
    for n, data in shots:
        digest = hashlib.md5(data).hexdigest()
        if run and run[-1][0] == digest:
            run[-1][2].append(n)
        else:
            run.append((digest, data, [n]))
    for _, data, ns in run:
        label = str(ns[0]) if len(ns) == 1 else "%d&ndash;%d" % (ns[0], ns[-1])
        out.append((base64.b64encode(data).decode("ascii"), label))
    return out


def classify(rec):
    if rec["cat"] != "RENDERS":
        return "blank"
    return "frozen" if len(load(rec["slug"])) <= 1 else "live"


def game_card(rec, kind):
    strips = "".join(
        '<figure class="fr"><img loading="lazy" alt="{n} at frame {lab}" '
        'src="data:image/png;base64,{b64}"><figcaption>{lab}</figcaption></figure>'.format(
            n=html.escape(rec["name"]), b64=b64, lab=lab)
        for b64, lab in load(rec["slug"]))
    return ('<article class="game" data-b="{k}"><header><h3>{n}</h3>'
            '<code>{s}</code></header><div class="strip">{strips}</div></article>').format(
        k=kind, n=html.escape(rec["name"]), s=html.escape(rec["slug"]), strips=strips)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "corpus", "gallery.html")
    surv = json.load(open(os.path.join(ROOT, "corpus", "render_survey.json"), encoding="utf-8-sig"))
    buckets = {"live": [], "frozen": [], "blank": []}
    for rec in sorted(surv, key=lambda r: r["name"].lower()):
        buckets[classify(rec)].append(rec)
    n_live, n_frozen, n_blank = (len(buckets[k]) for k in ("live", "frozen", "blank"))

    tpl = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "gallery_template.html"), encoding="utf-8").read()
    page = (tpl
            .replace("{{LIVE_N}}", str(n_live))
            .replace("{{FROZEN_N}}", str(n_frozen))
            .replace("{{BLANK_N}}", str(n_blank))
            .replace("{{TOTAL_N}}", str(len(surv)))
            .replace("{{RENDER_N}}", str(n_live + n_frozen))
            .replace("{{OURS_N}}", str(n_blank - 5))
            .replace("{{LIVE}}", "".join(game_card(r, "live") for r in buckets["live"]))
            .replace("{{FROZEN}}", "".join(game_card(r, "frozen") for r in buckets["frozen"]))
            .replace("{{BLANK}}", "".join(game_card(r, "blank") for r in buckets["blank"])))
    open(out_path, "w", encoding="utf-8", newline="\n").write(page)
    print("%s  (%d advancing / %d frozen / %d blank, %.1f MB)"
          % (out_path, n_live, n_frozen, n_blank, os.path.getsize(out_path) / 1e6))


if __name__ == "__main__":
    main()
