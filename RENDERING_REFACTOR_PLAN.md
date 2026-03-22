# Rendering Refactor Plan

## Goals

- Make the render pipeline easier to reason about.
- Reduce duplicated mode-specific logic.
- Isolate D3D11 details from higher-level app flow.
- Make future backend work (`DX12`/`Vulkan`) possible without a rewrite from scratch.
- Preserve current preview/export behavior while improving maintainability.

## Current Status

- Phases 1-15 are complete in practice.
- Preview/export orchestration has been decomposed into explicit request/plan, base-frame, and execution stages.
- GPU pass inputs/outputs now use named frame/pass structs instead of raw positional SRV lists.
- Composition, denoise, DOF, and post-process stages are explicit and shared more cleanly between preview and export.
- `GpuPathRenderer` now has a clearer CPU draw-list build vs GPU submission split.
- Common D3D11 shader/resource/buffer helpers are in place and used across the GPU renderers.
- Renderer shader code now lives in `src/renderer/shaders/*.hlsl` and is copied to the runtime shader directory by CMake.
- Preview/export state naming now separates render device, content, and pipeline stage instead of overloading one backend enum.
- GPU failure, fallback, and export interruption status reporting has been consolidated into shared helpers.
- Preview accumulation resets now track explicit reasons like scene, camera, iteration, resize, and device changes.
- Phase 11 preview-state cleanup is in place: app-owned preview progress now tracks target/displayed iterations, dirty vs queued vs accumulating vs complete state, and pending vs applied reset reasons, with GPU flame status snapshots feeding the viewport HUD.
- Phase 12 added a narrow internal D3D11 backend slice for CPU preview upload, texture readback, and presentable preview-surface lookup.
- Phase 13 extracted the CPU preview worker into its own request/completion component with a cleaner app-thread vs render-thread boundary.
- Phase 14 introduced shared render math and GPU pass parameter helpers for projection/depth normalization and common viewport/layer packing.
- Phase 15 added lightweight verification infrastructure with `RadiaryTests` and non-UI coverage for serialization, path draw-list building, and preview decision helpers.
- Preview and export were manually smoke-tested during the refactor and reported as working.

## Historical Notes

- Consider doing Phase 2 (frame input structs) before Phase 1. It is a small, mechanical, self-contained change that immediately makes the monster `RenderViewportIfNeeded` function more readable — which in turn makes Phase 1's extraction safer and less error-prone. The current call sites are already confusing: after denoising in the Path branch the denoised output gets passed as `flameSrv` with `pathSrv` nulled out (`src/app/AppWindowRender.cpp:436`), which a named struct would clarify.
- The Phase 1 helper names (`BuildViewportRenderRequest`, `DeterminePreviewCadence`, etc.) are useful as direction but may shift once the extraction actually happens. Let the helper boundaries emerge from the code rather than committing to names upfront.
- Phase 6 (split `GpuPathRenderer`) is more foundational than its current position suggests. The path renderer is referenced with different arguments in all three mode branches. Understanding its CPU/GPU split boundary would inform the pass-chain design in Phase 3. Consider moving it to before or alongside Phase 3.
- Phase 5 (move HLSL out of .cpp) is mostly cosmetic and does not affect pipeline architecture. Doing it mid-refactor adds diff churn. It can safely wait until after Phase 8.
- Phases 9–15 are aspirational and the right shape for them only becomes clear after Phases 1–8 land. Consider trimming them to a "Future Considerations" section to keep the plan actionable.
- Phase 12 (backend abstraction) should not be committed to yet. The right abstraction surface only becomes clear after the pipeline is clean. Note it as a possibility, not a plan.
- Each phase should include a manual smoke-test checklist to catch regressions early. At minimum: verify Flame/Path/Hybrid GPU preview, verify denoise + DOF + postprocess combinations, verify CPU fallback, and verify export for each completed phase.

Suggested phase execution order: **2 → 1 → 3 → 6 → 4 → 7 → 8**, then revisit 9–15 as needed.

## Completed Core Work

These phases are complete enough that they no longer need active tracking before future work:

- Phase 1: App-level preview orchestration untangled.
- Phase 2: GPU frame/pass input-output types introduced.
- Phase 3: Render/composition pipeline made explicit.
- Phase 4: Common D3D11 utilities extracted.
- Phase 5: Embedded HLSL moved out to standalone shader files.
- Phase 6: Path/flame renderer internals split into cleaner build/submission/resource seams.
- Phase 7: Shared scene-preparation helpers introduced.
- Phase 8: Preview and export composition logic substantially unified.
- Phase 9: Backend naming and render-stage semantics cleaned up.
- Phase 10: Failure handling and status reporting cleaned up.
- Phase 11: Accumulation/progressive preview state normalized around explicit reset and progress tracking.
- Phase 12: Internal D3D11 backend slice introduced for preview upload, readback, and preview image resolution.
- Phase 13: CPU preview worker extracted behind a dedicated async boundary.
- Phase 14: Shared render math and GPU pass parameter helpers introduced.
- Phase 15: Lightweight verification infrastructure and non-UI tests added.

The detailed phase writeups below are retained as historical notes for what was targeted and why.

## Phase 1: Untangle App-Level Render Orchestration

- Refactor `src/app/AppWindowRender.cpp:209` so it stops being the central place for every rendering decision.
- Split the current `RenderViewportIfNeeded` flow into small helpers:
  - `BuildViewportRenderRequest`
  - `DeterminePreviewCadence`
  - `RunGpuPreviewPipeline`
  - `RunCpuPreviewPipeline`
  - `PresentViewportPixels`
  - `UpdateRenderStatusOnFailure`
- Replace the current three large branches for `Flame`, `Path`, and `Hybrid` in `src/app/AppWindowRender.cpp:317`, `src/app/AppWindowRender.cpp:411`, and `src/app/AppWindowRender.cpp:475` with a shared pipeline that varies by available layers, not by copy-pasted mode blocks.
- Introduce a single `ViewportRenderRequest` struct containing:
  - scene
  - width/height
  - preview iterations
  - interactive flag
  - mode
  - enabled passes (`grid`, `flame`, `path`, `denoise`, `dof`, `postprocess`)
- Introduce a single `ViewportRenderResult` struct containing:
  - preview backend
  - displayed iterations
  - final SRV or CPU pixels
  - success/failure
  - error source/pass

Why first:

- This is the biggest readability win.
- It reduces risk in every later refactor.

## Phase 2: Introduce Frame Input/Output Types For GPU Passes

- Replace the multi-argument SRV passing in `src/renderer/GpuDofRenderer.h:18` and `src/renderer/GpuDenoiser.h:18`.
- Create a shared GPU frame input struct, something like:

```cpp
struct GpuFrameInputs {
    ID3D11ShaderResourceView* gridColor = nullptr;
    ID3D11ShaderResourceView* flameColor = nullptr;
    ID3D11ShaderResourceView* flameDepth = nullptr;
    ID3D11ShaderResourceView* pathColor = nullptr;
    ID3D11ShaderResourceView* pathDepth = nullptr;
};
```

- Create a matching `GpuFrameOutput` / `GpuPassResult` type:
  - output color SRV
  - output color texture
  - optional depth SRV
  - success
  - error string
- Update `GpuDofRenderer`, `GpuDenoiser`, and `GpuPostProcess` to consume these structs instead of positional parameters.
- Rename parameters to reflect intent, not implementation. For example:
  - `inputSrv` -> `sourceColor`
  - `gridSrv`/`pathSrv`/`flameSrv` -> grouped frame inputs

Benefits:

- Fewer mistakes from wrong argument ordering.
- Clearer semantics for hybrid composition.
- Easier to extend later.

## Phase 3: Make The Render Pipeline Explicit

- Define the actual pass sequence in one place instead of scattering it through conditions.
- For GPU preview, use a pass chain like:
  - `BaseFlamePass`
  - `BasePathPass`
  - `GridPass`
  - `CompositePass`
  - `DenoisePass`
  - `DofPass`
  - `PostProcessPass`
- Treat `Hybrid` as "flame + optional grid + path + optional merge/composite", not as a separate monolithic branch.
- Stop using the denoiser as an implicit compositor in non-obvious ways, which currently seems to happen in hybrid/postprocess flows in `src/app/AppWindowRender.cpp:371` and `src/app/AppWindowRender.cpp:541`. Today, when post-process is enabled but denoise is not, the denoiser is still invoked purely for grid+flame composition — this is the single most misleading control-flow path in the file.
- Add one explicit `CompositePass` or rename the current denoiser/combine behavior so the pipeline reads honestly.

Benefits:

- Much easier debugging.
- Cleaner separation between "compose" and "denoise".
- Better foundation for exporting and alternate backends.

## Phase 4: Extract Common D3D11 Utilities

- Centralize repeated D3D11 patterns found across:
  - `src/renderer/GpuFlameRenderer.h:99`
  - `src/renderer/GpuDenoiser.h:49`
  - `src/renderer/GpuPostProcess.h:52`
- Add a small utility layer for:
  - shader compilation
  - creating textures/SRVs/UAVs/RTVs/DSVs
  - creating dynamic constant buffers
  - resizing render targets
  - releasing COM resources
  - formatting HRESULT errors
- Prefer small helper functions over an elaborate framework. Example utility areas:
  - `renderer/d3d11/D3D11ShaderUtils.*`
  - `renderer/d3d11/D3D11ResourceUtils.*`
  - `renderer/d3d11/D3D11ErrorUtils.*`
- Standardize resource ownership style. Today some code uses raw pointers with manual `Release`, while app-level code uses `ComPtr`. Moving renderer internals toward `ComPtr` would remove a lot of shutdown boilerplate and leak risk.

Benefits:

- Much smaller renderer classes.
- Less repeated failure-path code.
- Easier future migration to another graphics API.

## Phase 5: Move Embedded HLSL Out Of Cpp Files

- Extract large shader strings from:
  - `src/renderer/GpuFlameRenderer.cpp`
  - `src/renderer/GpuPathRenderer.cpp:26`
  - likely the DOF/denoiser/postprocess `.cpp` files too
- Store them in shader files under something like:
  - `src/renderer/shaders/*.hlsl`
- Add a small build or load strategy:
  - simplest: embed at build time or compile from file in development
  - later: optional precompiled blobs
- Split shared math/helpers into common shader includes:
  - color conversion
  - random/hash
  - bloom helpers
  - depth normalization
- Keep shader entry points small and focused.

Benefits:

- Shader code becomes reviewable.
- Easier experimentation and debugging.
- Lower friction if you later compile to DXIL/SPIR-V.

## Phase 6: Split CPU Geometry Generation From GPU Submission

- `src/renderer/GpuPathRenderer.cpp` currently does both CPU-side scene projection/batching and GPU draw submission.
- Break it into two layers:
  - `PathDrawListBuilder` or `ProjectedPathBuilder`
  - `GpuPathRasterPass`
- Move helpers like grid generation and vertex emission out of the renderer class path:
  - `EmitTriangle`
  - `EmitLineQuad`
  - `EmitPointQuad`
  - `AppendGrid`
- Make the builder produce a clear draw list:
  - triangles
  - line quads
  - point sprites
  - metadata like bounds/counts
- Let the GPU class just upload vertex data and issue draw calls.

Benefits:

- Path rendering becomes easier to test.
- CPU and GPU paths can share more scene preprocessing.
- The renderer class becomes much smaller.

## Phase 7: Create A Shared Scene-Preparation Stage

- `BuildRenderableScene` in `src/app/AppWindowRender.cpp:43` is a start, but scene preparation should be a proper render-stage concept.
- Add a `PrepareSceneForRendering` step that:
  - evaluates timeline frame
  - filters invisible transforms/paths
  - clamps selected indices
  - resolves mode-specific visibility
  - computes render flags
- If needed, split this into:
  - `EvaluateAnimatedScene`
  - `CullHiddenLayers`
  - `NormalizeRenderScene`
- Keep app state mutation out of this stage.

Benefits:

- Cleaner separation between editor state and render state.
- Fewer repeated scene adjustments across preview/export paths.

## Phase 8: Unify Preview And Export Composition Logic

- Export code also appears to duplicate texture readback/composition behavior in `src/app/AppWindowExport.cpp:433` onward.
- Extract a shared composition path used by both preview and export:
  - base pass generation
  - optional merge/composite
  - optional denoise
  - optional DOF
  - optional postprocess
  - readback if needed
- Make preview and export differ mainly by:
  - target resolution
  - iteration budget
  - interactive throttling
  - whether the final target is screen or CPU buffer

Benefits:

- Fewer preview/export mismatches.
- One place to fix render-order bugs.
- Better support for future batch rendering.

## Future Considerations

The detailed phase notes below are now primarily historical context for completed refactor work and a baseline for any future follow-on phases.

## Phase 9: Clean Up Backend Naming And Semantics

- `PreviewBackend` in `src/app/AppWindow.h:66` mixes implementation detail with output state.
- Consider separating:
  - render source (`CPU`, `GPU`)
  - pipeline stage reached (`base`, `denoised`, `dof`, `postprocessed`)
  - scene composition (`flame`, `path`, `hybrid`)
- Example:

```cpp
enum class RenderDevice { Cpu, Gpu };
enum class RenderStage { Base, Denoised, Dof, PostProcessed };
enum class RenderContent { Flame, Path, Hybrid };
```

- This removes a lot of "which enum means what" branching.

Benefits:

- Simpler logic.
- Easier telemetry/debug labels.
- Less enum explosion over time.

## Phase 10: Improve Failure Handling And Status Reporting

- Failure reporting is duplicated and inconsistent in `src/app/AppWindowRender.cpp:396`, `src/app/AppWindowRender.cpp:464`, and `src/app/AppWindowRender.cpp:561`.
- Create one helper that builds the best available status text from pass failures.
- Standardize fallback behavior:
  - base pass failure -> CPU fallback
  - optional pass failure -> continue with previous valid stage
  - only mark hard failure when no renderable output exists
- Track which pass failed explicitly rather than inferring from `LastError()` in a long `if/else` chain.

Benefits:

- Better user-facing errors.
- Cleaner control flow.
- Easier debugging.

## Phase 11: Normalize Accumulation And Progressive Rendering State

- Flame accumulation logic in `src/renderer/GpuFlameRenderer.cpp:1177` onward is capable, but state handling is very local to that renderer.
- Formalize progressive rendering state across preview modes:
  - accumulated iterations
  - temporal state validity
  - scene signature
  - reset reasons
- Add explicit reset reasons:
  - viewport resized
  - scene changed
  - camera changed
  - mode changed
  - user requested clear
- Surface those resets to app-level pipeline code so behavior is more understandable.

Benefits:

- Fewer subtle stale-state bugs.
- Easier tuning of interactive preview.
- Better support for future render graph design.

## Phase 12: Introduce A Small Internal Render Backend Interface

- Do not jump straight to DX12/Vulkan abstraction everywhere.
- Add a very narrow interface around what the app actually needs:
  - create/destroy device resources
  - present frame
  - create textures and views
  - run compute pass
  - run raster pass
  - read back textures
- Keep it pragmatic and internal, not a huge engine framework.
- Use it first only around app/render pass boundaries, not deep in all code.

Benefits:

- Reduces D3D11 spread across app code.
- Makes later backend experiments possible.
- Lets you incrementally port, not rewrite.

## Phase 13: Improve Threading Boundaries For CPU Rendering

- The CPU render thread in `src/app/AppWindowRender.cpp:156` is fine, but it lives inside app-window concerns.
- Extract the CPU preview worker into a dedicated component:
  - request queue
  - cancellation policy
  - completed frame handoff
- Make render requests immutable once queued.
- Keep app/window/UI locks away from render implementation details.

Benefits:

- Cleaner concurrency model.
- Easier testing.
- Better scaling if more async work is added later.

## Phase 14: Create Shared Render Math / Sampling Utilities

- There are likely repeated depth normalization, color mapping, and projection-related bits across CPU/GPU renderers.
- Pull shared concepts into named utilities where practical:
  - depth range constants
  - color/tone-map parameter packing
  - camera projection helpers
  - postprocess parameter preparation
- Keep CPU and GPU implementations separate where they must be, but unify intent and naming.

Benefits:

- Less "same concept, different names".
- Easier to verify CPU/GPU parity.
- Cleaner parameter packing.

## Phase 15: Add Lightweight Verification Infrastructure

- Implemented:
  - CMake `enable_testing()` plus a `RadiaryTests` executable and `add_test()` entry.
  - A tiny self-contained test harness for fast non-UI verification.
  - Coverage for `SceneSerializer` round-trips/invariants, `PathDrawListBuilder` structure, and pure preview render decision helpers.
  - Extraction of preview reset / render-stage selection helpers into a standalone testable unit.
- Add non-UI validation around render prep and pass wiring:
  - scene prep tests
  - path draw-list builder tests
  - serialization/render option invariants
  - smoke tests for pass-chain selection logic
- Even without GPU unit tests, you can test:
  - which passes would run
  - whether expected inputs are present
  - whether state resets occur on the right triggers
- Add debug logging/hooks around:
  - frame mode
  - pass sequence
  - accumulated iterations
  - fallback reason

Benefits:

- Safer refactors.
- Faster bug triage.
- Better confidence before larger backend changes.

## Completed Refactor Order

1. Replace raw multi-SRV parameter lists with frame input structs (Phase 2).
2. Extract render request/result structs (Phase 1).
3. Refactor `RenderViewportIfNeeded` into shared pipeline helpers (Phase 1).
4. Add explicit composite pass semantics (Phase 3).
5. Split `GpuPathRenderer` into draw-list builder + submission pass (Phase 6).
6. Extract D3D11 helpers and reduce renderer boilerplate (Phase 4).
7. Create shared scene-preparation stage (Phase 7).
8. Unify preview/export composition paths (Phase 8).
9. Move shaders into standalone `.hlsl` files (Phase 5).
10. Add lightweight verification infrastructure and non-UI render tests (Phase 15).

The next active work starts at Phase 16.

## Highest-Value Fixes

- `AppWindowRender` decomposition
- explicit pass-chain model
- frame input structs instead of raw SRV argument lists
- shared D3D11 utilities
- path renderer split
