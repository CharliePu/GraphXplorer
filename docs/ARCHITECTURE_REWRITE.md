# GraphXplorer Renderer — Rewrite Architecture (`claude_rewrite`)

Fresh, self-contained CPU rendering engine for implicit relations (inequalities and equalities)
over a 2D viewport. Namespace `gxr`, tree `engine/`. Reuses **none** of the prior architecture or
numerical core. GPU is used only as a thin compositor behind a `Presenter` seam (OpenGL now,
Vulkan-ready).

## Objectives (verbatim, with how each is met)

1. **Iterate to near-pixel-perfect, graceful AA, stable under oscillation.**
   Per pixel we compute *coverage* ∈ [0,1] = the area fraction of the pixel where the relation holds,
   by sound interval/affine subdivision with **analytic leaf models**. Coverage → alpha. For sub-pixel
   oscillation (`y>sin(2^x)`) coverage converges to the *Lebesgue measure* of the true-set → a stable,
   deterministic gray, not point-sample noise. Refinement is progressive and monotone.
2. **Frame-level responsiveness independent of load.**
   The main thread only *composites cached tiles* for the current viewport and submits a cheap render
   request. Its per-frame work is O(visible tiles) — provably independent of formula complexity and of
   how much solver work is outstanding. All math is on worker threads, fully decoupled.
3. **High throughput.** World-space power-of-two tile pyramid maximizes reuse across pan/zoom;
   affine arithmetic gives quadratic boundary convergence; tile-granular lock-free parallelism;
   bounded per-tile work; mip-down reuse; optional SIMD batch evaluation.
4. **Precision on CPU.** All certified math is double precision with directed rounding on the CPU.
   GPU only blits coverage textures + UI.

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

### 1.2 Affine arithmetic (`Affine`)
`struct Affine { double c0; small_vector<Term> noise; double err; }` (Comba–Stolfi AAF).
First-order form `x = c0 + Σ cᵢ εᵢ + err·ε±` tracks correlations between subexpressions, so `x−x→0`,
`x·x` is tight, and repeated variables don't bloat. The evaluator runs **affine first** (cheap, kills
the dependency problem); only boxes still `Uncertain` under affine fall back to plain interval +
subdivision. This is the single biggest quality lever for complex formulas. Affine ranges are always
intersected with the sound interval range, so soundness is preserved.

### 1.3 Why both: interval is sound and cheap for ranges & domains; affine is tight for boundaries.
The compiled program can be evaluated in three modes from one bytecode: `double` (point),
`Interval` (sound range), `Affine` (tight range). A 4th mode yields an **interval gradient** (for the
equality band, §3.3) by carrying `∂/∂x, ∂/∂y` interval duals.

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

Breadth-first worklist of boxes. For each box `B` (covering an integer cell-rect, ≥ 1 cell):
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

### 3.4 Analytic oscillation model (the `y>sin(2^x)` centerpiece)
For `ExplicitY` relations `y rel g(x)` where, over a pixel column, `g` is monotone (g′ no sign change,
checked via interval-gradient) **and** its argument spans ≫ 2π (detected from the interval of the inner
argument), the true-set measure has a closed form instead of infinite subdivision. For `g=sin(arg)`:
`P[sin(t) > y] = ½ − arcsin(clamp(y,−1,1))/π`, so for `y > sin(t)`, coverage(y)= ½ + arcsin(y)/π,
averaged over the pixel's y-range using `∫arcsin(y)dy = y·arcsin y + √(1−y²)`. Analogous closed forms
for `cos`, and `tan` via its CDF. This yields the **exact, perfectly smooth gray** under pan/zoom —
the property the old renderer lacked. Outside the oscillating regime the same `ExplicitY` path does a
fast per-column 1-D bracket of `g(x)` (monotone → one root; few roots → enumerate) for crisp AA.

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
