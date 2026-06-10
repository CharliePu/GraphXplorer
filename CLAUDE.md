# GraphXplorer — project guide

A CPU renderer for implicit relations (`y>sin(2^x)`, `x^2+y^2<1`, `tan(x)>y`, …). The engine lives
under `engine/` (namespace `gxr`); it is a from-scratch rewrite — the legacy `src/` app it replaced
was removed 2026-06-10. Design detail in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md); module map in [`engine/README.md`](engine/README.md).

## Objectives (the contract — do not regress these)

Three outcomes define the product; the ground rules after them are settled constraints, not goals.
Mechanisms (greedy quadtree, refine ladder, job tiers) exist to serve the objectives and may change;
a change that regresses an objective is wrong no matter what it improves. Each objective names the
check that holds it. (Commits and reports before 2026-06-10 used a 7-objective numbering: 1→1,
2&7→2, 6→3; old 3/5 efficiency items and old 4 CPU-only are ground rules now.)

1. **Sound precision.** Every render converges to the analytic truth. A region is claimed uniform
   only when interval arithmetic *proves* it — never from samples — so a uniform tile is exact at
   every zoom. Boundaries are sub-pixel anti-aliased; sub-pixel oscillation (`y>sin(2^x)`, with or
   without detectable structure) converges to the analytic area measure: smooth, stable gray, not
   sample noise, including while the user interacts.
   *Held by:* solver oracle + determinism tests in `GxEngineTests`.

2. **Responsiveness independent of load.** The user never waits on the math, however pathological
   the formula. *Input:* the main thread reads only cached snapshots, does O(visible) work per
   frame, and never blocks on workers — input reflects on screen within a monitor frame.
   *Generation:* work always targets the current viewport — a new view first-paints within a few
   frames' worth of coarse passes and then sharpens progressively, visible region first; superseded
   queue entries are dropped, and in-flight solves the new view won't draw abort in under a
   millisecond.
   *Held by:* the `OBJECTIVE 2` tests (latency invariant, storm safety, pan-storm completion) and
   `gxrepro_prio` on `y>sin(2^x)+y*0` (first paint ≈100–200 ms after a zoom storm; `buildPresent`
   avg ≪1 ms under full worker load).

3. **Immersion.** What is on screen only ever improves. For an unchanged formula, every visible
   region draws the best content already published for it — its own (possibly stale) raster or the
   nearest ready ancestor — and is never replaced by anything worse: no holes, no flicker, no
   downgrade, through any pan/zoom. Where nothing covering a region exists yet (cold start, formula
   change, or a first visit at a scale beyond every cached ancestor), the guarantee begins with the
   first primitive published for it.
   *Held by:* `GraphXplorer --reprogl` (TRUEHOLE=0 across scrubs, deep-zoom cascade continuation)
   and `--selftest … debug` (holes=0).

### Ground rules (settled constraints — not up for trade, not goals)

- **CPU-certified math.** The GPU cannot run directed-rounding double precision, so all certified
  math is CPU; the GPU only composites cached tiles (OpenGL now; Vulkan-ready `Presenter` seam).
- **Efficiency is the tiebreaker.** Subject to the objectives, prefer the design that does less
  work. Concretely: a proven-uniform region collapses to a single greedy tile of any size
  (`x>x+1` is a handful of tiles, not a grid — work and memory scale with visible complexity, not
  area); the store stays count-bounded (LRU, stale epochs first); an idle app blocks at ~0% CPU/GPU
  (`glfwWaitEvents`), woken per event (`glfwPostEmptyEvent` on publish). Checks: `gxbench`,
  store-bound test assertions, the event-driven main loop.

> Objectives 2–3 make the tiling/scheduling/compositing intertwined and **error-prone**. Treat the
> invariants below as load-bearing; verify with the headless harnesses before/after any change.

## Architecture

`math` (sound interval + ULP rounding) → `expr` (parser, bytecode w/ point/interval/jet eval, relation
+ structure detection) → `solve` (coverage solver, 1-D accelerator) → `tile` (geometry, quadtree keys,
thread-safe store) → `app` (engine: mailbox, scheduler thread, worker pool) → `present`
(Presenter seam, GL compositor, freetype overlay/UI). `image` (PNG), `tools` (headless CLIs), `tests`.

- **Numerics.** Sound directed-rounding interval arithmetic + **interval-AD centered form** (mean-value
  form; kills the dependency problem, gives quadratic convergence and the gradient). Adaptive: the
  centered form is disabled per-tile when it stops certifying boxes.
- **Coverage solver.** Bounded **DFS** area-accumulating interval subdivision: proven-uniform boxes
  fill their whole region greedily; only uncertain boxes subdivide, to a sub-pixel floor where they
  are measured by world-jittered point sampling. Equalities use a gradient band (~1px line). Explicit
  `v op g(w)` (both axes) use an exact-measure 1-D quadrature; the general 2-D path reaches the same
  smooth measure via sampling (`SolveParams.analytic=false` forces pure subdivision). Detail tiles
  refine through a fixed 4-pass ladder (`refinePassParams`): pass 0 first-paints in a few ms even on
  a pathological tile, the final pass is byte-identical to a single legacy fine solve; equalities
  (band model, pass-independent) solve once at final quality.
- **Tiling (greedy quadtree).** `TileKey{epoch, level, i, j}` is a quadtree address (level = depth,
  worldPerPixel = 2^level; a node's 4 children are level-1). A node is classified **UniformTrue /
  UniformFalse / Mixed**. Uniform nodes are greedy leaves (flat fill, exact at any zoom, kept forever
  for the epoch). Mixed nodes are subdivided; at the detail level (≈ pixel scale) a Mixed node carries
  the coverage raster. The compositor walks top-down from a coarse level: stop at a uniform leaf
  (greedy), descend Mixed until the detail level, fall back to the nearest ready ancestor when a child
  is not ready yet (no holes). A zoom-out's freshly-born coarser root (still unclassified, nothing
  above it) descends its EXISTING subtree instead of gapping, so previously-painted content keeps
  drawing through the scale transition (`--reprogl` freshOut GUARDBARE=0).
- **Async pipeline.** `TileStore`: per-tile `atomic<shared_ptr<const CoverageTile>>` snapshot + state
  + monotone `bestPass`, read under a shared lock (the compositor walk holds it once via
  `ReadAccess`); structural inserts under the unique lock; count-budgeted LRU eviction (stale epochs
  first, low-watermark batches, ordering done off-lock). `Engine`: latest-wins mailbox
  (viewport+relation+epoch), a scheduler thread, a worker pool. Relation change bumps the epoch and
  cancels everything stale. Viewport change re-trues the schedule against ONE draw-model predicate
  (`wantsTile`): queued jobs the new view will never draw are dropped (`requeueForViewport`),
  in-flight ones abort mid-solve (a per-job flag the solver polls inside its sampling loops), and
  the queue orders visible > first-paint > newest > coarse-first. The pan-ahead ring (cull margin
  0.5) stays resident and first-painted; only the draw set (margin 0.1) is emitted, requested, and
  refined.
- **Presentation.** `GlPresenter` (OpenGL 3.3): one R8 coverage texture per resident detail tile
  (object-pooled, `glTexSubImage2D` reuse) + flat quads for uniform tiles, world-positioned,
  coverage→alpha blend, grid/axes. Uploads are budgeted by count AND per-frame time (~3 ms) so a
  refine storm slows sharpening instead of the frame; a tile whose only alternative is a hole gets a
  budget-exempt critical upload (capped). LRU texture eviction recycles into the pool. `Overlay`
  (freetype): formula bar (editable), help/status, debug panel with frame-latency attribution.

## Invariants (break these and something subtle breaks)

- **Main thread O(visible nodes), never blocking.** `buildPresent`/compositing read cached snapshots
  only (atomic load under shared lock). No solving, no waiting on workers, no unbounded work. Texture
  uploads are byte-budgeted per frame.
- **Snapshots are immutable.** A worker publishes a *new* `shared_ptr<const CoverageTile>`; readers
  holding an old one are unaffected. Never mutate a published tile.
- **Determinism = stability.** Tile content is a pure function of `(relation, key, params)`. Same world
  region ⇒ identical tile ⇒ no flicker. Sampling/sub-cell positions are world-aligned, never
  screen-aligned.
- **Soundness of greedy.** A node may be marked uniform **only** when the interval/centered classify
  *proves* it (AllTrue/AllFalse). Never mark uniform on a sample. Uniform tiles are then safe at any
  zoom; a false-uniform would render a wrong region at every scale.
- **Epoch discipline.** Workers check the epoch/cancel flag between BFS/DFS levels and abandon stale
  work. Old-epoch tiles stay only as display fallback until evicted; new-epoch keys differ so they
  never alias.
- **No holes / no downgrade.** The compositor must emit a covering primitive for every visible region
  (the detail tile's own raster — possibly a stale pass while it refines — else the nearest ready
  ancestor). Never replace a more precise tile with a less precise one on screen.
- **`publishRefine` is the no-downgrade authority.** Raster publishes are keyed by the slot's
  monotone `bestPass`; a stale coarser publish can never overwrite a finer raster, regardless of
  completion order. `Done` is never demoted; snapshots are never cleared by abandonment.
- **The queue lies; the draw model doesn't.** Enqueue-time priority flags (generation, onScreen) go
  stale the moment the user moves. Correctness decisions — run, abort, drop — come only from
  `wantsTile` evaluated against the *latest* viewport: at dequeue, at requeue, and over in-flight
  work. Priority tiers only order; they never decide.

## Build / run / test

Windows + MSVC (via `VsDevCmd.bat`) + CMake/Ninja + vcpkg. **Use Release for any performance claim —
Debug is ~10× slower (no inlining, checked iterators).**

```
cmd /c _build_msvc.bat <target>      # Debug   -> build/engine/
cmd /c _build_release.bat <target>   # Release -> build-release/engine/
```

Targets: `GxEngineTests` (Catch2 — golden oracles, latency invariant, must stay green),
`gxrender "<formula>" out.png [cx cy wpp size subBits]`, `gxbench`, `gxrepro_prio` (objective-2
latency numbers on the degenerate wall — run before/after scheduler changes), `GraphXplorer`
(live app).
GUI controls: drag=pan, scroll=zoom, **Enter**=edit formula, **1-6**=presets, **D**=debug overlay,
**R**=reset, **Esc**=quit. Validate GL headlessly via
`GraphXplorer --selftest out.png "<formula>" [debug]` (offscreen render-to-PNG).

When modifying the tiling/scheduler/compositor: run `GxEngineTests` and an `--selftest` (with `debug`)
before and after, and check pan/zoom in the live app for holes/flicker.
