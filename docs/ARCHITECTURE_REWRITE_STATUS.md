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
- Reverted the premature live `Application` cutover. The executable currently
  uses the legacy `ComputeEngine`/`SceneManager` path as a compatibility bridge
  until the new interaction controller, grid/axis/text render systems, and
  input overlay commands exist.
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
- Added architecture, contract, formula compiler, tile cache, scheduler/backend,
  render command, and frame pipeline tests.

## Current Runtime Boundary

- `FramePipeline` is compiled and covered by tests, but it is not the live UI
  runtime.
- `Application` owns `SceneManager` temporarily so keyboard routing, formula
  input, grid, axis labels, text, and the existing plot UI remain usable.
- The next destructive integration should migrate one visible producer at a
  time into command-producing systems before removing the corresponding legacy
  scene path.

## Remaining Deletion Ledger

- Delete `Graph` and state-owning `ChunkTree` once all visible-cover queries use
  `TileCache` and `TileCoverageIndex`.
- Delete or quarantine request-sized `ComputeEngine`, `GraphProcessor`, and
  `GraphRasterizer` after the command-based plot path reaches visual and input
  parity with the current scene path.
- Delete `ChunkRenderer`, `ChunkRegionRasterizer`, and `ChunkContourRasterizer`
  once OpenCL is ported behind the batch `ComputeBackend` ABI.
- Delete plot-owned chunk render items, region texture cache, contour cache, and
  per-chunk VAO/VBO/IBO mesh creation once plot emits tile render instances.
- Delete UI component mesh-vector callbacks and `Renderer::updateMeshes()` once
  remaining legacy scene/UI sources are removed from the app target.
- Delete `TextMeshesGenerator` per-glyph mesh path once text runs are backed by
  `TextAtlas`.
- Tighten architecture tests to ban `staplegl` ownership in UI after the plot,
  grid, labels, and input box migrations land.
