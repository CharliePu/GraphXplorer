# Tile Refinement Pipeline

This document defines the renderer workflow for inequality formulas. Equations are intentionally out of scope for now.

## Core Terms

- **Seed tile**: A coarse tile from the initial viewport cover.
- **Preview tile**: The first texture-sized tile. This is the tile that can receive an optional OpenCL preview texture.
- **Pixel tile**: A child tile corresponding to one texel in a preview tile's final texture.
- **Subpixel tile**: A proof-only tile below pixel level.
- **contains-1**: A boolean fact meaning this tile or one of its descendants has certified existence of a true region.
- **Texture prepare**: The step that converts tile proof facts into CPU texture data for upload.
- **GPU preview**: An optional, imprecise OpenCL texture for a mixed preview tile.
- **Refined texture**: The authoritative texture produced after CPU refinement and texture prepare.

## Main Workflow

1. Start from seed tiles that cover the current viewport.
2. Refine each seed tile with interval arithmetic until it either converges or reaches preview level.
3. If a tile is proven uniform true or uniform false, send that fact toward texture prepare.
4. If a tile reaches preview level and is still mixed, add it to the mixed preview set.
5. If OpenCL is available, render mixed preview tiles early as imprecise GPU previews.
6. Regardless of GPU preview, continue CPU refinement for each mixed preview tile.
7. CPU refinement descends from preview tile to pixel tiles.
8. For each uncertain pixel tile, CPU refinement descends to subpixel tiles until proof succeeds or the subpixel budget is exhausted.
9. A tile has `contains-1=true` if any descendant proves uniform true.
10. Propagate `contains-1` upward from subpixel/pixel tiles to the preview tile.
11. Texture prepare builds the refined texture:
    - pixel tile with `contains-1=true` becomes the 1-representing color,
    - proven empty becomes background,
    - exhausted unknown becomes fallback/debug/best-estimate.
12. When the refined texture is ready, it replaces the GPU preview texture for that preview tile.

## OpenCL Role

OpenCL is not the core renderer.

It is only an optional preview accelerator:

- If OpenCL is available, it may produce an imprecise texture early for mixed preview tiles.
- If OpenCL is unavailable, the pipeline waits for CPU refinement and texture prepare before texture data is available.
- OpenCL output must be replaceable by the refined texture.
- The refined texture is the authoritative output.

## Visual Cover Rule

The visual cover builder traverses the tile tree for the current viewport and draws the first usable prepared texture it finds on the traversal path.

This means:

- uniform tiles can be drawn directly,
- mixed preview tiles can be drawn if they have a prepared texture,
- an imprecise GPU preview may be used temporarily,
- a refined texture replaces the preview once available,
- missing or not-yet-uploaded textures fall back to previous/ancestor/missing visual state.

## Proof Storage

Refinement proof data is part of the formula-session cache.

The important distinction is representation:

- proof nodes should not become ordinary render/scheduler `TileRecord`s,
- proof nodes should be committed as a compact `TileProofTree` payload owned by `TileCache`,
- the proof tree is keyed by the preview tile and valid until the formula session changes or a stronger uniform authority prunes it,
- the render-facing tile record keeps only the current prepared texture reference and summary existence state.

CPU refinement still performs traversal and `contains-1` propagation inside the worker. The main thread receives the refined texture plus one proof-tree payload, so cache reuse is possible without pushing thousands of subpixel nodes through the visual tile state machine.
