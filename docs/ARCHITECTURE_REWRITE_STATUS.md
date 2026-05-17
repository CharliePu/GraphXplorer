# Contract-First Runtime Status

## Current Runtime Boundary

- `FramePipeline` is the live UI runtime.
- `InteractionController` converts GLFW key/text/drag/scroll/resize callbacks
  into pure app-state events.
- `TilePlanner` owns compute work planning only. It creates/reuses sparse tile
  records, respects uniform tile authority, and emits interval/raster jobs.
- `TileRuntime` executes tile-granularity jobs through the `ComputeBackend`
  contract and applies completed tile transactions back into `TileCache`.
- `VisualCoverBuilder` owns presentation planning. It builds the displayed cover
  from current ready tiles, the last committed presentable visual frame, and
  residual missing placeholders.
- `Renderer` consumes command buffers only. `RenderResourceManager` owns plot
  instances, region texture-array slices, grid submission, and overlay text/rect
  submission.

## Implemented

- Split the former broad app library into subsystem targets:
  `GraphXplorerUtil`, `GraphXplorerFormula`, `GraphXplorerTile`,
  `GraphXplorerCompute`, `GraphXplorerRender`, `GraphXplorerUI`, and
  `GraphXplorerApp`.
- Added versioned cross-subsystem contracts in `src/Util/Contracts.h`,
  including formula handles, viewport requests, tile transactions, display
  tiles, render tile instances, draw commands, upload plans, and frame
  snapshots.
- Added stable hashing for formula identity and a compiled formula layer with
  source, semantic, and backend hashes.
- Replaced request-sized graph generation with a sparse dyadic tile cache:
  `TileCache` stores records by formula, level, and integer tile coordinate.
- Removed the old fixed `MinTileLevel`/`MaxTileLevel` viewport policy. Seed
  tile levels are now computed directly from viewport geometry and constrained
  only by finite `double`/integer representation limits, so zooming out past
  the previous level-30 boundary still produces a sparse cover instead of an
  empty frame.
- Made render-ready uniform tiles authoritative. A uniform tile owns its subtree,
  descendants are pruned or rejected, and agreeing uniform children promote to a
  parent authority.
- Moved scheduling to `TilePlanner`, which starts from viewport seed tiles,
  reuses cache records, traverses mixed tiles, and stops at the viewport leaf
  level.
- Moved async compute to `TileRuntime`, with tile work invalidated by formula
  semantics rather than viewport request id.
- Added a GPU-preferred compute backend factory. Raster jobs use an OpenCL GPU
  kernel when a compatible double-precision GPU device is available, and fall
  back to `CpuComputeBackend` otherwise. OpenCL initialization is lazy and runs
  off the UI path, so startup can fall back to CPU rasterization until the GPU
  path is ready.
- Added latency-aware batching in `TileRuntime`. Backend calls are split into
  measured sub-batches, and `BatchOptimizer` keeps a per-job-kind Pareto
  frontier of observed batch size and raster sampling bucket versus wall-clock
  latency. Runtime selection chooses the highest-throughput measured batch that
  fits the target latency budget, with bounded exploration of larger and
  in-between candidates.
- Made completed tile application budgeted. `FrameBudgetController` passes a
  per-frame apply budget into `TileRuntime::drainCompleted`; unfinished
  completions are deferred back to the front of the inbox instead of forcing the
  UI thread to drain a backlog in one frame.
- Added `FrameBudgetController` as the main-thread side of the optimization
  problem. Its objective is to keep `FramePipeline::process` under a target UI
  latency while maximizing useful tile work. It controls apply budget, tile job
  admission, upload budget, seed-cell count, and seed-relative refinement depth.
  The refinement depth is a topology policy such as "refine three levels below
  the current seed tile." It is not recomputed on every frame, pan, or zoom:
  zoom moves the seed and leaf levels together, while the depth stays fixed.
  Only formula changes and framebuffer/device-pixel-ratio changes are allowed
  to reselect that topology policy. Viewport interaction still uses smaller
  apply/upload/job budgets, but it does not switch the displayed tile grid
  between fine and coarse levels.
- Made statically derivable runtime sizing explicit. Worker count defaults to
  hardware threads minus a CPU headroom value, and raster sampling resolution is
  a `TileRuntimeOptions` value aligned with the target tile screen size rather
  than an implicit runtime constant.
- Added an event-loop responsiveness path: after a frame that processed input,
  the app polls GLFW events instead of sleeping for the normal wait interval.
- Added formula identity simplification so expressions such as `x=x`, `x<=x`,
  and `x!=x` classify correctly before interval evaluation.
- Added `VisualCoverBuilder` so presentation is not tree-exclusive: current
  ready tiles can replace old visuals while clipped previous tiles and residual
  placeholders keep zoom/pan frames from going blank.
- Added command producers for plot, grid, debug overlay, and formula-input
  state.
- Deleted the legacy scene/UI mesh path and the legacy graph/chunk compute path
  from the build and source tree.
- Added regression coverage for tile cache authority, sparse work planning,
  runtime staleness, visual cover fallback, render command ranges, formula
  simplification, and frame pipeline behavior.

## Removed Legacy Code

- Scene/UI mesh path: `SceneManager`, `MainScene`, `Plot`, `Grid`,
  `AxisLabels`, `InputBox`, `UIComponent`, `Mesh`, and
  `TextMeshesGenerator`.
- Request-sized graph/chunk path: `Graph`, `ChunkTree`, `ComputeEngine`,
  `GraphProcessor`, `GraphRasterizer`, `ChunkRenderer`,
  `ChunkRegionRasterizer`, `ChunkContourRasterizer`, `CpuChunkRenderer`,
  `OpenCLChunkRenderer`, and `RasterizedPlot`.
- Old scheduler path: `TileScheduler` and scheduler/backend tests.

## Remaining Work

- Extend GPU acceleration beyond mixed-tile raster masks only when it can stay
  behind the batch `ComputeBackend` interface.
- Keep presentation-specific behavior in `VisualCoverBuilder`; `TilePlanner`
  should remain work-only.
- Add budgeted/yieldable internals to visual cover construction and render
  resource uploads if profiling shows those stages exceeding the controller's
  UI target. The controller owns the budgets now, but those two stages still
  execute their selected work synchronously inside the frame.
- Persist or warm-start batch optimizer frontiers per backend/device/formula
  class after enough profiling data exists.
