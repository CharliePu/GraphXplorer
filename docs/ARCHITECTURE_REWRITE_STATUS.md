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
- Made render-ready uniform tiles authoritative. A uniform tile owns its subtree,
  descendants are pruned or rejected, and agreeing uniform children promote to a
  parent authority.
- Moved scheduling to `TilePlanner`, which starts from viewport seed tiles,
  reuses cache records, traverses mixed tiles, and stops at the viewport leaf
  level.
- Moved async compute to `TileRuntime`, with tile work invalidated by formula
  semantics rather than viewport request id.
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

- Port any future GPU/OpenCL acceleration behind the batch `ComputeBackend`
  interface instead of reviving chunk renderer ownership.
- Keep presentation-specific behavior in `VisualCoverBuilder`; `TilePlanner`
  should remain work-only.
