# Roadmap

The goal is every game in the corpus in game: past its boot and warning screens
and playable, through the generic driver with no game-specific code. The order
below is commercial (retail and prototype) titles first, homebrew second; both
count toward the goal.

## Next

- **The commercial titles first.** Work through the retail and prototype rows of
  [COMPATIBILITY.md](COMPATIBILITY.md) that do not yet reach gameplay, measured
  against hardware with `scripts/reference/motion.py` (`onscreen`, `ourmove`).
- **The 16 gated games** in [docs/RENDER_TRIAGE.md](docs/RENDER_TRIAGE.md): our
  picture is identical at frames 300/600/900 while hardware advances.
- **Bound High and Virtual Bowling** draw in one session and not the next, with
  the same build. Find the source of the non-determinism before reading any
  delta that involves them.
- **Soviet Union 2011 v1.2**: BGMap empty at frame 800, blocked before rendering.
- **Homebrew rows**, once the commercial set is through.
- **The corpus on netlab.** The recipe builds the toolkit; the survey still runs
  on the workstation, where the ROMs are.

## Deferred

- **Standalone emulator build** of `src/` (an interpreter in front of the same
  runtime). The runtime work counts toward it already.
- A quieter default log: the generic driver prints VIP/interrupt diagnostics to
  stderr unconditionally.

## Out of scope

- Distributing ROMs or recompiled output, in any form.
- Stereoscopic 3D output. The left eye is rendered.
