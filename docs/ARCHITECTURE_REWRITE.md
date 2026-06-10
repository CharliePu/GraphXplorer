# GraphXplorer Renderer — Architecture

Self-contained CPU rendering engine for implicit relations (inequalities and equalities) over a 2D
viewport. Namespace `gxr`, tree `engine/`; written from scratch in 2026-06 to replace the original
renderer (removed 2026-06-10). GPU is used only as a thin compositor behind a `Presenter` seam
(OpenGL now, Vulkan-ready).

## Objectives (canonical statement in [`CLAUDE.md`](../CLAUDE.md)), with how each is met

1. **Sound precision.** Per pixel we compute *coverage* ∈ [0,1] = the area fraction of the pixel
   where the relation holds, by sound interval subdivision with analytic leaf models and
   world-jittered measure sampling. Coverage → alpha. For sub-pixel oscillation (`y>sin(2^x)`, with
   or without detectable structure) coverage converges to the *Lebesgue measure* of the true-set →
   a stable, deterministic gray, not point-sample noise. Refinement is progressive and monotone
   (`publishRefine` never downgrades a raster).
2. **Responsiveness independent of load.** The main thread only *composites cached tiles* for the
   current viewport — O(visible tiles), provably independent of formula cost and outstanding solver
   work; all math is on workers. Generation always targets the current viewport: detail tiles
   refine through a 4-pass ladder (first paint in a few ms even on a pathological tile), the queue
   orders visible > first-paint > newest > coarse-first and is re-trued on every viewport change,
   and in-flight solves the new view won't draw abort in under a millisecond via a per-job flag the
   solver polls inside its loops.
3. **Immersion.** The compositor always draws the best content already published for every visible
   region — its own (possibly stale) raster, or the nearest ready ancestor threaded down the
   descent as a stand-in — and never replaces better with worse: no holes, no flicker, no
   downgrade. A region with nothing published yet (cold start, formula change, first visit at a
   scale beyond every cached ancestor) is covered from its first published primitive onward.

### Ground rules (settled constraints, not goals)

- **Precision on CPU.** All certified math is double precision with directed rounding on the CPU.
  The GPU only blits coverage textures + UI.
- **Efficiency is the tiebreaker.** World-space power-of-two tile pyramid maximizes reuse across
  pan/zoom; proven-uniform regions collapse to greedy variable-size tiles; the centered form gives
  quadratic boundary convergence; per-tile work is bounded; idle is event-driven at ~0% CPU.

---

## 1. Numerical core (`engine/math`)

### 1.1 Sound interval arithmetic (`Interval`)
`struct Interval { double lo, hi; uint8_t flags; }` with `flags` carrying `Undefined` (empty/domain
violation) and `Discontinuity` (a pole/branch cut crosses the box). All operators use **directed
(outward) rounding**: lower bounds computed toward −∞, upper toward +∞, via an RAII
`RoundingScope` (`_controlfp`/`fesetround`) around arithmetic kernels, plus one-ulp `nextafter`
widening for `std::` transcendentals (which are not correctly-rounded). This makes
"uniform-true/false" *proofs sound*, which is required for pixel-accurate boundaries.

Four-way box classification (not three):
`enum class Sign { AllTrue, AllFalse, Undefined, Uncertain }`.
`Undefined` contributes **0** area and does **not** subdivide.

### 1.2 Interval automatic differentiation + centered form (the tightening core)
Rather than a separate affine-arithmetic type, tightening and the gradient are produced by **one**
mechanism: forward-mode interval AD. The compiled program evaluates to a `Jet = {Interval v, dx, dy}`
(value plus interval partials). The **centered (mean-value) form**
`F_c(B) = f(mid) + dx·(x−mid_x) + dy·(y−mid_y)`, intersected with the naive range, kills the
dependency problem (`x−x→0`, `x·x` tight, repeated variables don't bloat) and gives quadratic
convergence under bisection — the single biggest quality lever for complex formulas. It is only
trusted where `f` is smooth on the box (finite, gap-free derivatives); otherwise the sound naive range
stands. The same Jet supplies the gradient for the equality band (§3.3) and structure detection. This
is a sound, simpler equivalent of affine arithmetic that also yields derivatives for free.

### 1.3 Three evaluation modes from one bytecode
`double` (point, for sub-pixel sampling), `Interval` (sound naive range, domain/discontinuity), and
`Jet` (value + interval gradient, for the centered form and the equality band).

---

## 2. Expression compiler (`engine/expr`)

`Lexer → Parser (recursive descent, precedence climbing) → Ast → Program`.
`Program` is flat stack bytecode (`Op{kind, imm, slot}`) evaluable in any of the modes above with a
reusable per-thread scratch stack (no per-eval allocation). Variables are slot-indexed (`x`=0, `y`=1).

The compiler also extracts **structure** used by analytic fast paths:
- `Relation` = `{ side: f(x,y), cmp: <,≤,>,≥,=,≠ }` normalized to `f rel 0`.
- `ExplicitY` when `f` has the form `y − g(x)` (or `g(x) − y`): enables the per-column 1-D solver and
  the analytic oscillation model (§3.4). Detected by checking `y` appears affinely and isolated.
- `ExplicitX` symmetric case.
- `AffineHalfPlane` when `f` is linear: exact polygon-clip coverage, no subdivision (§3.2).

Supported ops: `+ − * / ^`, `sin cos tan log exp sqrt abs`, comparisons, `&& ||`, unary `−`.
(Extensible; these cover the target set incl. `y>sin(2^x)`, `x^2+y^2<1`, `tan(x)>y`, `y=x^2`.)

---

## 3. Coverage solver (`engine/solve`)

Input: a `Relation`, a tile world-rect `R`, output resolution `T×T`. Output: `CoverageTile`
(`T×T` of `float` ∈ [0,1]) + `done` flag + `worstUncertainty`.

### 3.1 General bounded area accumulator (handles everything)
Integer-relative subdivision: the tile is the integer box `[0, T·2^K]²` of *sub-pixel cells*
(K = `kSubpixelBits`, e.g. 4 → 16×16 sub-cells per pixel, the **fixed floor**). Boxes are integer
rects; world coords are derived only at evaluation as `origin + local·step` with `step` an exact power
of two → **bit-exact** parent/child boundaries and deep-zoom stability.

**Depth-first** worklist of boxes (an explicit stack: the working set stays O(tree depth) and
cache-resident, instead of the hundreds of MB an O(frontier) breadth-first sweep reaches on a fully
oscillating tile). Classification is **adaptive**: the cheap naive interval runs first, and the
centered form is escalated to only while it keeps certifying boxes — a tile that is genuine 2-D
oscillation (centered never decides until sub-pixel) disables it after a 1024-box sampling window and
stops paying ~3× per box. For each box `B` (covering an integer cell-rect, ≥ 1 cell):
- Evaluate affine→interval. Classify:
  - `AllTrue`  → add `area(B)` to every pixel `B` overlaps (whole or fractional).
  - `AllFalse` / `Undefined` → contribute 0.
  - `Uncertain` and `B` larger than one floor cell → split into 4 (integer halves), enqueue.
  - `Uncertain` at the floor cell → add a **leaf estimate** (§3.5) of its covered area.
- After each BFS level, the per-pixel area accumulator is a valid coverage estimate (emit progressively).
Accumulator is `float` per pixel (one tile = one thread → no atomics, no false sharing).

**Bounded work:** per-tile box budget `≈ c·T²`. The frontier is bounded; at the floor every box
resolves to an estimate. The solver **stops deterministically** when either (a) total uncertain area
< ε (truly converged → `done`, certainty high), or (b) the budget/floor is reached (best-estimate →
`done`, certainty = worstUncertainty). Idle viewport ⇒ all tiles reach `done` ⇒ workers sleep ⇒ **no
fan-spinning**.

### 3.2 Affine half-plane fast path (linear `f`)
Exact: clip the pixel square by the half-plane `ax+by+c rel 0`; covered area = clipped-polygon area.
No subdivision, analytically pixel-perfect AA. Golden-test anchor.

### 3.3 Equality / curve model (`f = 0`, measure-zero)
Area of a curve is 0, so a pure area model makes curves vanish. Instead render the **1-px-wide AA band**
`{ |f| ≤ ½·w·|∇f| }` (w≈1.5 px). `|∇f|` is bounded per box via the interval-gradient mode (§1.3).
Coverage of a floor cell = `smoothstep(1 − |f_mid| / (½·w·|∇f|·worldPerPixel))`. Where `∇f` can't be
bounded (oscillation), fall back to **sign-change at a fixed sub-pixel depth**: cell lit if `0∈f(cell)`;
coverage = lit-fraction. Both keep the curve ~1px and non-vanishing at all zoom levels; oscillating
equalities fill to gray. `≠` is the complement.

### 3.4 Explicit-1D exact-measure accelerator (the `y>sin(2^x)` centerpiece — but general)
For any relation reducible to `v <op> g(w)` (v,w ∈ {x,y} in **either order** — `y>g(x)`, `x<g(y)`,
…), the 2-D area collapses to a per-line quadrature: for each line of constant w (a pixel column or
row), `g(w)` is densely sampled across the line's w-extent (world-aligned, so deterministic and
flicker-free), sorted, and each pixel's coverage is the exact fraction of its v-range satisfying the
relation, via prefix sums. This is **not** a sin special-case: it converges to the true Lebesgue
measure for an arbitrary `g`. For a smooth `g` it is exact AA; for a sub-pixel-oscillating `g`
(`g=sin(2^x)` at large x) it converges to the measure — which for `y>sin(t)` is the closed form
`½ + arcsin(clamp(y,−1,1))/π` (a smooth, deterministic gray), the property the old renderer lacked.
Sample count scales with the refinement pass, so idle passes smooth the gray further. Relations
without explicit structure (`sin(x·y)>0`) fall back to the general 2-D solver (§3.1) and still converge
to the correct measure, just more slowly.

### 3.5 Leaf estimate (unbiased)
At the floor, an `Uncertain` cell straddles the boundary. Instead of a biased constant ½, estimate the
covered sub-area from the boundary's linearization within the cell (using the cell's affine form: the
boundary `Σcᵢεᵢ + c0 = 0` clips the cell → area). Falls back to ½ only when even the affine form is
non-informative (true sub-pixel chaos), where ½ is the correct expectation.

### 3.6 What "pixel-perfect" means (testable)
`|coverage_computed(px) − coverage_truth(px)| ≤ τ` for all px, with `coverage_truth` from closed form
(half-plane, disk, monotone curve, oscillation measure) and `τ = 1/255`. This is the golden-test oracle.

---

## 4. Tiling & geometry (`engine/tile`)

- `worldPerPixel(level) = 2^level` (level ∈ ℤ; global power-of-two scale convention).
- Tile `(level L, i, j)`, `T×T` px, covers world
  `x∈[i·T·2^L,(i+1)·T·2^L], y∈[j·T·2^L,(j+1)·T·2^L]`; `i,j` are `int64`.
- `Viewport{ centerX, centerY (double), worldPerPixel s, pxW, pxH }`.
  `activeLevel = round(log2 s)`; compositor samples tiles at scale `s/2^L ∈ [~0.71,~1.41)`.
- `TileKey{ epoch:uint64, level:int32, i:int64, j:int64 }`. Content is a deterministic function of
  `(relation, key)` ⇒ pan/zoom reuse + temporal stability for free.
- **Mip-down reuse:** when all 4 children at L−1 are `done`, the parent at L is the exact area-average
  of the children — synthesized without re-evaluating the formula. On zoom-in, the parent upscales as
  immediate fallback.

## 5. Async pipeline (`engine/sched`, `engine/app`)

### 5.1 Threads
- **Main thread** (owns window+GL): poll input → update `Viewport` → select visible tiles at active
  level (+ coarser fallback) → for each, read its current **immutable coverage snapshot**
  (`shared_ptr<const CoverageTile>`, atomic load) → composite → draw grid/axes/UI → present. Drains a
  completed-tile queue and uploads to GL under a **byte budget**. Posts one `RenderRequest` to a
  **latest-wins mailbox**. Never blocks on workers. O(visible tiles).
- **Scheduler thread**: owns `TileStore`. On request: compute needed tiles (visible + velocity-sized,
  coarse-cover-gated prefetch). **Two-tier**: first ensure every visible tile has a coarse pass
  (active level −1, cheap) so the screen is always fully covered within a frame or two; then enqueue
  cancellable fine refinement, prioritized center-out. Bumps `epoch` on relation change; old-epoch
  tiles stay for fallback but are LRU-capped to one stale generation.
- **Worker pool** (`hardware_concurrency`−1): run `TileSolver` jobs, one tile per task (no intra-tile
  threading → no false sharing). Publish progressive snapshots by atomic pointer swap. Check an atomic
  `epoch`/`cancel` between BFS levels to abandon stale work cheaply.

### 5.2 TileStore concurrency
Per tile: `atomic<shared_ptr<const CoverageTile>> snapshot` + state. Workers write new immutable
snapshots; main reads the pointer — no torn reads, no locks on the hot read path. A coarse-grained
mutex guards the *map* structure only. Byte-budgeted LRU eviction keyed by `lastTouchedFrame`.

### 5.3 Storms (typing / rapid pan-zoom)
Each keystroke = new `epoch`; workers see the bumped atomic and abandon old jobs; scheduler only
enqueues current epoch. Work is bounded to "current-epoch visible tiles"; memory bounded by LRU. Main
thread always shows the latest coarse result. No unbounded pile-up.

## 6. Presentation (`engine/present`) — Phase C
`class Presenter` interface: `upload(TileKey, CoverageTile)`, `composite(viewport, visibleTiles)`,
`drawGrid/axes/text`, `present()`. `GlPresenter` implements it in OpenGL 3.3 (one R8/R32F texture per
resident tile, textured quads, coverage→alpha blend, instanced grid, freetype text). The interface is
the Vulkan-ready seam: a `VkPresenter` can replace it without touching the engine.

## 7. Proof harness (`engine/tools`, `engine/tests`) — all headless, CPU-only

- **Golden PNG** (`gxrender`): formula+viewport → coverage → PNG. Visual + byte-compare proof of
  objective 1. Canonical set: `y>sin(2^x)`, `x^2+y^2<1`, `y=x^2`, `tan(x)>y`, `x*y<1`.
- **Coverage golden tests**: half-plane area == closed form (τ≤1/255); disk area; oscillation measure
  == `½+arcsin/π`; equality curve is ~1px and non-empty.
- **Pan-stability test**: render the same world region from two sub-pixel-offset viewports; assert the
  overlapping coverage matches bit-for-bit (same cached world tiles) ⇒ no flicker.
- **Objective-2 invariant test**: drive the exact main-thread per-frame function with a *trivial* and a
  *pathological* (`sin(2^x)` at huge x) formula under a stub solver that sleeps; assert the main-thread
  step touches the **same bounded number of tiles** and does the same work in both ⇒ latency
  independent of load. (Invariant, not flaky wall-clock.)
- **Throughput bench** (`gxbench`): coverage-tiles/sec, single vs all cores, convergence time.

## 8. Module dependency graph (acyclic)
`math → expr → solve → tile → sched → app → present`. Tools/tests depend on `app`. `image` is a leaf.
No module depends on the legacy `src/` tree.
