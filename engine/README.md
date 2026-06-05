# GraphXplorer rewrite engine (`gxr`)

A fresh, self-contained CPU renderer for implicit relations (inequalities and equalities), built on
branch `claude_rewrite`. Reuses none of the prior architecture. Full design in
[`docs/ARCHITECTURE_REWRITE.md`](../docs/ARCHITECTURE_REWRITE.md).

## What it does (the four objectives)

1. **Pixel-precise, anti-aliased, stable under oscillation.** Sound directed-rounding interval
   arithmetic + interval-AD *centered form* (kills the dependency problem) drive a bounded
   area-accumulating coverage solver. Equalities use a gradient-band model (resolution-independent
   ~1px line). Explicit `y=f(x)` / `x=f(y)` relations use an exact-measure 1-D quadrature, so
   `y>sin(2^x)` converges to the smooth `½+arcsin(y)/π` gray with **no flicker** — and this is general,
   not a sin special-case (`sin(x*y)>0` works through the general 2-D path).
2. **Frame-level responsiveness independent of load.** Async tile store (immutable snapshots + atomic
   swap), scheduler thread, worker pool, latest-wins mailbox, epoch cancellation. The main thread is
   O(visible tiles); a deterministic test proves it loops free while workers grind pathological tiles.
3. **Throughput.** World-space power-of-two tile pyramid, DFS subdivision, adaptive centered form,
   bounded per-tile work. Release: a full screenful is 2–35 ms for normal formulas.
4. **CPU precision; OpenGL is a thin compositor** behind a Vulkan-ready `Presenter` seam.

## Build

Windows + MSVC (via `VsDevCmd.bat`) + CMake/Ninja + vcpkg, same as the parent project.

```
cmd /c _build_msvc.bat <target>      # Debug   -> build/engine/
cmd /c _build_release.bat <target>   # Release -> build-release/engine/
```

Targets: `GxEngineTests` (Catch2, 28 cases), `gxrender` (headless PNG), `gxbench` (throughput),
`GraphXplorer2` (live GL app). **Use the Release build for any performance impression** — Debug is
~10× slower (no inlining, checked iterators).

## Run

- **Live app:** `build-release/engine/GraphXplorer2.exe ["<formula>"]`
  Pan = drag, zoom = scroll, keys **1–6** = preset formulas, **R** = reset view, **Esc** = quit.
- **Headless image:** `gxrender "<formula>" out.png [cx cy worldPerPixel size subBits]`
  e.g. `gxrender "y > sin(2^x)" sin.png 0 0 0.0156 512 4`
- **Tests:** `build-release/engine/GxEngineTests.exe`
- **Bench:** `build-release/engine/gxbench.exe`

Formula syntax: `+ - * / ^`, `sin cos tan log exp sqrt abs`, comparisons `< <= > >= = !=`, `&& ||`,
variables `x y`, constants `pi e`. A relation needs a comparison (e.g. `x^2+y^2<1`, `y=x^2`).

## What to validate on a real display
The GL window (`GraphXplorer2`) compiles and links but could not be run in the headless build
environment. Please confirm: window opens, tiles fill in coarse-then-fine, pan/zoom stay smooth under
heavy formulas (try preset 5 = `sin(x*y)>0` and zoom out), and the axes/grid track the view. The CPU
engine itself is fully validated headlessly (golden PNGs, closed-form oracles, latency invariant).

## Layout
`math/` sound interval + ULP rounding · `expr/` parser, bytecode, 3 eval modes, relation+structure ·
`solve/` coverage solver + 1-D accelerator · `tile/` geometry, keys, thread-safe store · `sched`/`app/`
engine (mailbox, scheduler, workers) · `present/` Presenter seam + GL compositor · `image/` PNG ·
`tools/` headless CLIs · `tests/` Catch2.
