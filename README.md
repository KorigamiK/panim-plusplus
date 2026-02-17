# panim++

Minimal plugin-driven animation sandbox inspired by swaptube and
[tsoding/panim](https://github.com/tsoding/panim). Animations are compiled as
shared objects and loaded at runtime. Output is encoded to video via FFmpeg; a
simple plugin is provided.

## Build

```bash
cmake -S panim++ -B panim++/build
cmake --build panim++/build
```

Targets:
- `panim` — main executable (encodes video to `panim_out/output.mp4` by default).
- `sample_wave` — example plugin (`build/plugins/libSampleWave.so`).
- `showcase` — timeline + painter demo plugin (`build/plugins/libShowcase.so`, preferred default).
- `PANIM_ENABLE_HWACCEL=ON` (default) adds `-march/-mtune=native` when supported; turn it off if you need portable binaries.
- `PANIM_ENABLE_FFMPEG=ON` (default) requires dev packages for `libavformat`, `libavcodec`, `libavutil`, `libswscale`.
- `PANIM_ENABLE_SVG=ON` (default) requires `librsvg-2.0` + `cairo` to rasterize LaTeX SVGs onto frames.
- `PANIM_ENABLE_CUDA=ON` (optional) builds CUDA helper (`gpu_invert`) linked via `CUDAToolkit`.
- `PANIM_USE_SPDLOG=ON` (default) enables structured logging; falls back to stderr when spdlog is missing. Exceptions are disabled across the project (`-fno-exceptions`).

## Run

```bash
./panim++/build/bin/panim                       # uses bundled plugin, H.264/MP4 to panim_out/output.mp4
./panim++/build/bin/panim <plugin.so> 3 1280 720 30 my_out.mp4
```

Arguments: `[plugin_so] [seconds] [width] [height] [fps] [output_file]`.
If `libShowcase.so` exists next to the binary (default build), it is selected automatically; otherwise it falls back to `libSampleWave.so`.

## Authoring animations quickly

- `#include "panim/Painter.hpp"` for immediate-mode drawing helpers: filled rects, circles, gradients, line stroke, and `blit` to composite pre-rendered frames.
- `#include "panim/Timeline.hpp"` for keyframe tracks with easing (`Track<T>::add(time, value, ease)` and `sample_loop(t)`). Built-in types: `double`, `float`, `int`, `panim::Vec2`, `panim::Color`; add your own by specializing `LerpHelper`.
- `panim::Color` and `panim::Vec2` live in `Color.hpp`/`Math.hpp` and are header-only.
- CUDA remains optional: call `gpu_invert(frame)` freely; it becomes a no-op when CUDA is off.

## LaTeX support

The renderer calls a MicroTeX `LaTeX` binary. Set `MICROTEX_BIN` (or `MICROTEX_ROOT` -> `build/LaTeX`). On first use, expressions are converted to SVG under `panim_out/latex/` and reused across frames.

## Extending

Implement `panim::Animation` in a shared object exporting `create_animation` (and optional `destroy_animation`). Link against `panim_core` to reuse the helpers and headers. Place the built `.so` where `panim` can load it or pass the path explicitly.

## CUDA hook

Include `panim/CudaHelpers.hpp` and call `gpu_invert(frame)` (or replace with your own kernels). Build with `-DPANIM_ENABLE_CUDA=ON` and a CUDA toolkit installed. The helper is a stub when CUDA is disabled, so plugins can call it unconditionally.

## SVG / LaTeX overlay

LaTeX snippets are converted to SVG via MicroTeX then rasterized with `render_svg_to_frame(...)` (librsvg+cairo). The sample plugin draws an equation near the bottom of the frame; adjust position/scale to taste.
