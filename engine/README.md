# GraphXplorer rewrite engine (`gxr`)

A self-contained CPU renderer for implicit relations (inequalities and equalities). Full design in
[`docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md); the project contract lives in
[`CLAUDE.md`](../CLAUDE.md).

## What it does (the three objectives)

The canonical objective statements, each with the check that holds it, live in
[`CLAUDE.md`](../CLAUDE.md). In short:

1. **Sound precision.** Coverage converges to the analytic area measure (interval arithmetic +
   centered form + measured sampling); uniformity is claimed only on proof. `y>sin(2^x)` renders as
   smooth, stable gray — through the exact 1-D quadrature when the structure is detectable, and
   through the sampled general 2-D path when it is not (`y>sin(2^x)+y*0`, `sin(x*y)>0`).
2. **Responsiveness independent of load.** The main thread is O(visible tiles) and never blocks on
   workers. A new viewport first-paints in ~100–200 ms even on a pathological formula: detail tiles
   run a 4-pass refine ladder (final pass byte-identical to a single fine solve), a viewport change
   aborts in-flight work the new view won't draw, and the queue orders visible > first-paint >
   newest > coarse-first. Measure with `gxrepro_prio`.
3. **Immersion.** What is on screen only ever improves — no holes, no flicker, no downgrade,
   through any pan/zoom of an unchanged formula.

Ground rules (constraints, not goals): all certified math runs on the CPU — OpenGL is a thin
compositor behind a Vulkan-ready `Presenter` seam; and subject to the objectives, prefer less work —
proven-uniform regions collapse to single greedy tiles (a screenful of a normal formula solves in
2–35 ms Release), the tile store is count-bounded, and an idle app blocks at ~0% CPU.

## Build

Windows + MSVC (via `VsDevCmd.bat`) + CMake/Ninja + vcpkg.

```
cmd /c _build_msvc.bat <target>      # Debug   -> build/engine/
cmd /c _build_release.bat <target>   # Release -> build-release/engine/
```

Targets: `GxEngineTests` (Catch2), `gxrender` (headless PNG), `gxbench` (throughput),
`gxrepro` (resize/settle), `gxrepro_prio` (objective-2 harness: first-paint / all-sharp latency and
main-thread walk cost on the degenerate `y > sin(2^x) + y*0`), `GraphXplorer` (live GL app).
**Use the Release build for any performance impression** — Debug is ~10× slower (no inlining,
checked iterators).

## Run

- **Live app:** `build-release/engine/GraphXplorer.exe ["<formula>"]`
  Pan = drag, zoom = scroll, keys **1–6** = preset formulas, **R** = reset view, **Esc** = quit.
- **Headless image:** `gxrender "<formula>" out.png [cx cy worldPerPixel size subBits]`
  e.g. `gxrender "y > sin(2^x)" sin.png 0 0 0.0156 512 4`
- **Tests:** `build-release/engine/GxEngineTests.exe`
- **Bench:** `build-release/engine/gxbench.exe`

Formula syntax: `+ - * / ^`, `sin cos tan log exp sqrt abs`, comparisons `< <= > >= = !=`, `&& ||`,
variables `x y`, constants `pi e`. A relation needs a comparison (e.g. `x^2+y^2<1`, `y=x^2`).

Headless GL validation (no display needed): `GraphXplorer --selftest out.png "<formula>" [debug]`
renders offscreen to a PNG; `GraphXplorer --reprogl <prefix>` drives the real render loop through
resize/zoom scrubs and deep-zoom cascades, checking for holes and convergence.

## Layout
`math/` sound interval + ULP rounding · `expr/` parser, bytecode, 3 eval modes, relation+structure ·
`solve/` coverage solver + 1-D accelerator · `tile/` geometry, keys, thread-safe store · `app/`
engine (mailbox, scheduler, workers) + GUI main · `present/` Presenter seam + GL compositor ·
`image/` PNG · `tools/` headless CLIs · `tests/` Catch2.
