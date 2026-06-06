# GraphXplorer — project guide (branch `claude_rewrite`)

A CPU renderer for implicit relations (`y>sin(2^x)`, `x^2+y^2<1`, `tan(x)>y`, …). The rewrite lives
under `engine/` (namespace `gxr`); it reuses none of the legacy `src/` architecture. Design detail in
[`docs/ARCHITECTURE_REWRITE.md`](docs/ARCHITECTURE_REWRITE.md); module map in [`engine/README.md`](engine/README.md).

## Objectives (the contract — do not regress these)

1. **Precision & quality.** Every render iterates until near pixel-perfect. Generalized to complex /
   oscillating relations (e.g. `y>sin(2^x)`): sub-pixel aliasing and noise are handled gracefully and
   stay **stable and visually smooth** while the user interacts. (Coverage = exact area-measure;
   oscillation converges to the analytic measure, not point-sample noise.)
2. **Frame-level responsiveness, independent of load.** User input is reflected on screen within one
   monitor frame (1/60s, 1/120s) **regardless of render cost**. The main thread is light and **does not
   scale** with workload — the heavy work is fully asynchronous. (Proven by a deterministic invariant
   test: the main thread loops free while workers grind pathological tiles.)
3. **Throughput.** Subject to (1) and (2), maximize render speed / efficiency.
4. **CPU only.** The GPU cannot render precise images, so all certified math is CPU double precision.
   The GPU only composites cached tiles (OpenGL now; Vulkan-ready `Presenter` seam).

### Additional objectives (being enforced — see "Greedy tiling" below)

5. **Greedy variable-size tiles.** Tile size adapts to region complexity. A region the interval
   solver **proves uniform** (entirely 0 or entirely 1) becomes a single large tile of *any* size — no
   precision is lost because it is uniformly 0/1. Example: `x>x+1` (always false) collapses to a
   handful of huge tiles (≈ one per quadrant), not a full grid.
   - **Zoom out:** larger greedy tiles are discovered and replace finer tiles.
   - **Zoom in:** a large greedy tile stays valid and is drawn at any size (a proven-uniform region is
     uniform at every scale).
   - **Event-driven rendering:** render a frame **only when needed** — user input arrived, or the
     render is not yet final (tiles still refining). Otherwise block (`glfwWaitEvents`) at ~0% CPU/GPU.
     The engine wakes the main thread (`glfwPostEmptyEvent`) when a tile completes.
6. **Immersion (no flicker, no holes).** For an unchanged formula, tiles are **reused and kept**
   across zoom/pan until a finer / more precise tile is ready to replace the outdated one. The
   compositor must always have *something* correct to draw for every visible region (a coarser
   ancestor or an older tile), and must never drop a good tile for a worse one.

7. **Graph-generation responsiveness (prioritize on-screen).** Generation work is prioritized for the
   CURRENT viewport: when the user pans/zooms a lot, the visible region must complete quickly instead
   of waiting behind a backlog of now-off-screen tiles. Off-screen *un-started* work is
   discarded/deprioritized so workers spend their time on what is on screen; already-solved tiles are
   kept (immersion). Must not regress objectives 1, 2, 6.

> Objectives 5 & 6 make the tiling/scheduling/compositing intertwined and **error-prone**. Treat the
> invariants below as load-bearing; verify with the headless harness before/after any change.

## Architecture

`math` (sound interval + ULP rounding) → `expr` (parser, bytecode w/ point/interval/jet eval, relation
+ structure detection) → `solve` (coverage solver, 1-D accelerator) → `tile` (geometry, quadtree keys,
thread-safe store) → `sched`/`app` (engine: mailbox, scheduler thread, worker pool) → `present`
(Presenter seam, GL compositor, freetype overlay/UI). `image` (PNG), `tools` (headless CLIs), `tests`.

- **Numerics.** Sound directed-rounding interval arithmetic + **interval-AD centered form** (mean-value
  form; kills the dependency problem, gives quadratic convergence and the gradient). Adaptive: the
  centered form is disabled per-tile when it stops certifying boxes.
- **Coverage solver.** Bounded **DFS** area-accumulating interval subdivision: proven-uniform boxes
  fill their whole region greedily; only uncertain boxes subdivide, to a sub-pixel floor. Equalities
  use a gradient band (~1px line). Explicit `v op g(w)` (both axes) use an exact-measure 1-D quadrature
  (this is the general fix for sub-pixel oscillation; `SolveParams.analytic=false` forces pure
  subdivision).
- **Tiling (greedy quadtree).** `TileKey{epoch, level, i, j}` is a quadtree address (level = depth,
  worldPerPixel = 2^level; a node's 4 children are level-1). A node is classified **UniformTrue /
  UniformFalse / Mixed**. Uniform nodes are greedy leaves (flat fill, exact at any zoom, kept forever
  for the epoch). Mixed nodes are subdivided; at the detail level (≈ pixel scale) a Mixed node carries
  the coverage raster. The compositor walks top-down from a coarse level: stop at a uniform leaf
  (greedy), descend Mixed until the detail level, fall back to the nearest ready ancestor when a child
  is not ready yet (no holes).
- **Async pipeline.** `TileStore`: per-tile `atomic<shared_ptr<const CoverageTile>>` snapshot + state,
  read under a shared lock (main thread never blocks on workers); structural inserts under the unique
  lock; byte/-count-budgeted LRU eviction keyed by last-touched frame, keeping ≥1 stale generation for
  fallback. `Engine`: latest-wins mailbox (viewport+relation+epoch), a scheduler thread that enqueues
  tile jobs (coarse-first, center-out), a worker pool (epoch-cancellable). Relation change bumps the
  epoch and cancels stale work.
- **Presentation.** `GlPresenter` (OpenGL 3.3): one R32F texture per resident detail tile + flat quads
  for uniform tiles, world-positioned, coverage→alpha blend, grid/axes, budgeted uploads, LRU texture
  eviction. `Overlay` (freetype): formula bar (editable), help/status, debug panel.

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
  (descend to ready detail, else nearest ready ancestor, else an older tile). Never replace a more
  precise tile with a less precise one on screen.

## Build / run / test

Windows + MSVC (via `VsDevCmd.bat`) + CMake/Ninja + vcpkg. **Use Release for any performance claim —
Debug is ~10× slower (no inlining, checked iterators).**

```
cmd /c _build_msvc.bat <target>      # Debug   -> build/engine/
cmd /c _build_release.bat <target>   # Release -> build-release/engine/
```

Targets: `GxEngineTests` (Catch2 — golden oracles, latency invariant, must stay green),
`gxrender "<formula>" out.png [cx cy wpp size subBits]`, `gxbench`, `GraphXplorer2` (live app).
GUI controls: drag=pan, scroll=zoom, **Enter**=edit formula, **1-6**=presets, **D**=debug overlay,
**R**=reset, **Esc**=quit. Validate GL headlessly via
`GraphXplorer2 --selftest out.png "<formula>" [debug]` (offscreen render-to-PNG).

When modifying the tiling/scheduler/compositor: run `GxEngineTests` and an `--selftest` (with `debug`)
before and after, and check pan/zoom in the live app for holes/flicker.
