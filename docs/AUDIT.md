# panim++ product and authoring API audit

Audit date: 2026-08-17

## Product goal

panim++ should make the common path—write a short animation, iterate on a
timeline window, render a high-quality video, and publish the source—feel
small. Its primitives should remain composable enough that advanced authors
can add their own scene systems, renderers, and GPU effects without forking the
engine.

The useful boundary is:

```text
Animation lifecycle
    ├── Painter and Frame          immediate drawing and composition
    ├── Track<T>                   values over time
    ├── SceneSequence              scene-local time and transitions
    ├── LatexTrack/EquationMorph   mathematical typography
    └── Compute                    automatic portable GPU work

Runtime loop
    ├── RenderSession              setup plus deterministic timeline sampling
    ├── WebGPU preview             interactive high-DPI presentation
    └── FrameSink                  PNG/video or custom output destination
```

That is a compact vocabulary. New APIs should earn their place by composing
with these concepts rather than introducing a parallel animation model.

## Current author journey

1. Derive one class from `panim::Animation`.
2. Return its preferred duration, dimensions, and frame rate from `info()`.
3. Cache resources and construct tracks/scenes in `on_setup()`.
4. Draw a frame for an absolute time in `render_frame()`.
5. Export the class with `PANIM_EXPORT_ANIMATION(MyAnimation)`.
6. Register it with `panim_add_plugin`, then iterate with
   `scripts/preview.sh MyAnimation` and export with
   `scripts/render.sh MyAnimation`.

This is now concise enough for examples and in-repository work. The
`FeatureTour` plugin deliberately uses only this public path.

## Changes completed during this audit

- Added `AnimationInfo`, allowing each plugin to own its default duration,
  resolution, frame rate, and output name.
- Added `SceneSequence`, with named scenes, scene-local time, sampling, reused
  transition buffers, and automatic crossfades.
- Added `PANIM_EXPORT_ANIMATION` and the `panim_add_plugin` CMake helper to
  remove repeated ABI and target boilerplate.
- Added a required integer plugin API handshake. The loader now rejects an old
  or incompatible binary before invoking its C++ virtual interface.
- Made plugin loading use `LoadLibrary`/`GetProcAddress` on Windows and
  `dlopen`/`dlsym` on Unix-like hosts.
- Added plugin-name discovery and a non-throwing CLI with `--start`, `--frames`,
  `--size`, and named options. Positional commands remain compatible.
- Added `scripts/render.sh` for one-target incremental builds and short preview
  windows.
- Reused the main frame allocation across the render loop.
- Made frame construction safe for non-positive dimensions, made
  `Painter::clear` replace RGBA as its name promises, added a const pixel
  accessor, and added bilinear scaled compositing.
- Added a portable WGSL Mandelbulb and changed compute dispatch to two-
  dimensional workgroups, avoiding the previous one-dimensional dispatch
  limit at large frame sizes.
- Kept WebGPU adapter selection automatic and moved all environment selection
  language into diagnostic documentation.
- Fixed tinted `LatexTrack` transitions so morphing glyphs retain their tint.
- Fixed FFmpeg frame-rate rational handling and packet duration metadata, so
  fractional rates and one-frame iteration renders are valid.
- Added draft/share/master encode profiles, 4:4:4 high-fidelity masters,
  BT.709 metadata, fast-start MP4 output, and whole-frame supersampling with a
  Lanczos downsample.
- Added lossless single-frame PNG output for exact timeline inspection and
  raised the feature tour's native canvas from 720p to 1080p.
- Split animation sampling from output through `RenderSession` and `FrameSink`,
  keeping FFmpeg as one final-output sink instead of the authoring loop.
- Added `preview`, `frame`, and `render` commands. Preview uses a high-DPI
  SDL3/WebGPU surface, playback and stepping controls, a visible scrub strip,
  lossless screenshots, automatic adapter selection, and safe plugin reload
  that preserves the playhead and keeps the previous generation on failure.
- Added explicit `VideoWriter::finish()` error reporting and made the render
  session finalize its sink before reporting success.
- Captured WebGPU shader/pipeline validation in initialization, allowing a
  broken embedded shader to fail into the normal compute fallback instead of
  reaching command submission with an invalid pipeline.
- Added lightweight CTest coverage for tracks, frames, Painter, scaled blits,
  and scene transitions.

## What is already strong

- The lifecycle is deterministic and direct: the same time produces the same
  requested frame unless the plugin deliberately uses external state.
- C++ plugins keep the engine open to custom algorithms without forcing a
  domain-specific language on every author.
- MicroTeX SVG output preserves mathematical typography, and glyph-level SVG
  decomposition makes related expressions morph rather than crossfade as flat
  images.
- A single WGSL implementation reaches Metal, Vulkan, and D3D12 through
  wgpu-native. There are no vendor shader forks.
- System FFmpeg, Cairo, librsvg, tinyxml2, and external MicroTeX/wgpu-native
  keep third-party source out of the repository.

## Open findings, ordered by product impact

### P0 — publishing is source sharing, not a stable package format

The loader now checks an integer API version, but plugin entry points still
exchange a C++ abstract class across a shared-library boundary. There is no
manifest, capability declaration, or stable C data interface. A binary plugin
therefore still depends on a compatible compiler, standard library, panim++
headers, and core build.

Until this changes, the honest distribution unit is plugin source plus a CMake
target. A publishable package should contain a manifest, source/assets, a
minimum engine API version, declared optional capabilities, and reproducible
build instructions. A stable C ABI can sit below the current ergonomic C++
wrapper.

### P0 — user-authored GPU work is still closed

`ComputeEffect` is an enum of built-in effects compiled into one engine WGSL
module. This proves portability but is not yet an extension surface: adding an
effect still edits core C++, the shared WGSL file, and the CPU reference.

The next compute API should let a plugin create a `ComputeProgram` from WGSL,
bind a standard frame/global-parameter layout, report shader diagnostics, and
dispatch without exposing wgpu-native handles. CPU fallback should be optional
for custom shaders rather than pretending arbitrary WGSL can automatically run
as C++.

### P0 — setup cannot report failure

`Animation::on_setup()` returns `void`. A plugin can log a missing required
asset but cannot stop the render cleanly. Replace or supplement it with a
status-returning setup hook before declaring a stable plugin ABI.

### P1 — frames cross the CPU/GPU boundary for every compute call

`Frame` owns host RGBA bytes. Each WebGPU effect uploads the frame, dispatches,
waits, maps a readback buffer, and copies pixels back. Chaining effects repeats
that round trip, while Painter and scene composition remain CPU-only.

A GPU-resident frame/layer type and command graph should keep uploads,
composites, and multiple WGSL passes on the device until one readback or a
hardware-encoder handoff. This matters more than adding another vendor backend.

### P1 — the drawing vocabulary needs transforms, paths, and real text

Painter covers the minimum raster primitives but lacks a transform stack,
camera, clipping, paths, stroked circles/curves, images with fit modes, and a
normal font/text API. Mathematical text should remain a separate MicroTeX
feature; ordinary labels should not need LaTeX. These additions should extend
Painter/layers instead of creating a second scene graph.

### P1 — installed authoring support is incomplete

Headers and libraries install, but there is no exported CMake package config or
standalone project template. `panim_add_plugin` currently helps plugins inside
this repository. A public release needs `find_package(panim CONFIG)`, imported
targets, a tiny starter repository, and platform packaging for wgpu-native and
MicroTeX discovery.

### P2 — resource and render robustness need another pass

- MicroTeX runs as a subprocess on cache misses. Its cache key uses
  `std::hash`, has no renderer/version metadata, and is not guaranteed stable
  across implementations. Use a stable content digest and include render
  options plus MicroTeX version in the key.
- WebGPU per-dispatch validation should be scoped into `ComputeResult`, and
  device-loss recovery is not implemented.
- There is no deterministic engine-provided random seed, asset registry,
  audio timeline, cancellation, render progress callback, or parallel frame
  scheduling contract.
- Equation glyph matching is heuristic. It should gain fixture-based visual
  regression tests for repeated symbols, long text, color/tint, and unmatched
  glyph counts.

## Recommended sequence

1. Harden the author loop: setup status, stable asset cache keys, visual
   regression fixtures, and clearer shader/device-loss diagnostics. Direct
   preview, safe plugin reload, screenshots, and explicit PNG/video commands
   are now supported.
2. Stabilize sharing: a stable C ABI/version policy, package manifest, exported
   CMake config, and a standalone starter plugin.
3. Expand composition: transforms, paths, clipping, ordinary text, and layers.
4. Introduce GPU-resident frames plus a plugin-owned WGSL `ComputeProgram`.
5. Add audio, hardware encoder handoff, and safe parallel rendering after the
   determinism/thread-safety contract is explicit.

The guiding constraint is to keep the default animation understandable from a
single source file. Extensibility should arrive through composable layers and
program objects, not by making every author understand backend selection,
FFmpeg, SVG internals, or plugin loading.
