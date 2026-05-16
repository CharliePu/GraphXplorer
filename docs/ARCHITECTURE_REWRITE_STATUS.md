# Contract-First Rewrite Status

## Implemented

- Split the former broad `GraphXplorerLib` into subsystem targets:
  `GraphXplorerUtil`, `GraphXplorerFormula`, `GraphXplorerTile`,
  `GraphXplorerCompute`, `GraphXplorerRender`, `GraphXplorerUI`, and
  `GraphXplorerApp`.
- Added versioned cross-subsystem contracts in `src/Util/Contracts.h`,
  including formula handles, viewport requests, tile transactions, render tile
  instances, draw commands, upload plans, and frame snapshots.
- Added stable hashing for formula identity and a compiled formula layer with
  source, semantic, and backend hashes.
- Added `TileCache`, `TileStage`, `TileCoverageIndex`, `TileScheduler`,
  `ComputeBackend`, CPU batch classification/rasterization, upload planning,
  immutable frame command buffering, render resource handles, text atlas
  scaffolding, pure `AppState` reduction, and a vertical `FramePipeline`.
- Replaced the compatibility bridge with the command runtime. `Application`
  now owns `FramePipeline` and `InteractionController`; it no longer constructs
  `ComputeEngine`, `SceneManager`, or scene/UI mesh components.
- Decoupled `Renderer` from `UIComponent`; command-buffer rendering now
  delegates to `RenderResourceManager`, which owns the plot pipeline, static
  quad geometry, instance buffer, and GL submission for tile instances.
- Made `TileCache::apply()` atomic: a transaction is staged and rejected as a
  whole if any delta is stale, invalid, or violates the tile state machine.
- Made `TileScheduler` state-aware for the frame path; it emits the next valid
  job for each visible tile rather than all downstream jobs up front.
- Added fixed variable-slot evaluation to compiled formulas and routed CPU
  raster hot loops through slot arrays instead of per-pixel variable maps.
- Connected mixed-tile CPU raster output to frame-owned region payload IDs
  before producing `RegionImageRef` deltas.
- Added hierarchical parent fallback to `TileCoverageIndex` for progressive
  visible cover selection.
- Added command producers for plot, grid, debug text, and formula-input overlay
  state. `RenderResourceManager` owns plot instances, region texture-array
  slices, grid submission, and overlay rectangle/text-block submission.
- Deleted the legacy scene/UI mesh path from the build and source tree:
  `SceneManager`, `MainScene`, `Plot`, `Grid`, `AxisLabels`, `InputBox`,
  `UIComponent`, `Mesh`, and `TextMeshesGenerator`.
- Added architecture, contract, formula compiler, tile cache, scheduler/backend,
  render command, and frame pipeline tests.

## Current Runtime Boundary

- `FramePipeline` is the live UI runtime.
- `InteractionController` converts GLFW key/text/drag/scroll/resize callbacks
  into pure app-state events.
- `Renderer` consumes command buffers only. The mesh-vector API has been
  removed.

## Remaining Deletion Ledger

- Delete `Graph` and state-owning `ChunkTree` once all visible-cover queries use
  `TileCache` and `TileCoverageIndex`.
- Delete or quarantine request-sized `ComputeEngine`, `GraphProcessor`, and
  `GraphRasterizer` after the remaining golden graph-generation tests are
  ported to the batch `ComputeBackend`/`TileCache` model.
- Delete `ChunkRenderer`, `ChunkRegionRasterizer`, and `ChunkContourRasterizer`
  once OpenCL is ported behind the batch `ComputeBackend` ABI.
- Port OpenCL to the batch `ComputeBackend` ABI and delete
  `ChunkRenderer`/region/contour rasterizer adapters as standalone owners.
