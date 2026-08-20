# panim++

`panim++` is a C++20, plugin-driven animation engine inspired by
[swaptube](https://github.com/2swap/swaptube) and
[tsoding/panim](https://github.com/tsoding/panim). Animations are compiled as
shared libraries and loaded at runtime. Frames can be processed through a
portable CPU path or one WGSL compute path running through WebGPU, then encoded
to video through the system FFmpeg libraries. Interactive iteration uses a
native SDL3 window presented by the same WebGPU implementation, so FFmpeg is
reserved for final files rather than used as a preview player.

## macOS setup

The supported macOS setup uses Homebrew libraries and keeps MicroTeX and
[wgpu-native](https://github.com/gfx-rs/wgpu-native) outside this repository:

```bash
./scripts/setup-macos.sh
```

The script:

- installs the packages in `Brewfile` with `brew bundle`;
- clones MicroTeX beside this repository at `../MicroTeX`;
- applies the small compatibility fixes needed to build and run MicroTeX
  against Homebrew on macOS;
- builds MicroTeX and exposes it as `microtex` on `PATH`;
- installs MicroTeX's resources under the per-user data directory it already
  supports;
- downloads the pinned, checksum-verified wgpu-native binary beside the
  repository when `PANIM_WGPU_ROOT` is not already set;
- configures and builds panim++ with WebGPU over the system Metal framework; and
- runs the lightweight core tests; and
- renders `panim_out/setup-smoke.mp4`, `panim_out/hardware-smoke.mp4`, and
  `panim_out/feature-smoke.mp4` as end-to-end checks.

Set `PANIM_DEPS_DIR` to choose another parent directory for external
dependencies. Set `MICROTEX_ROOT` or `PANIM_WGPU_ROOT` to use existing
installations:

```bash
PANIM_DEPS_DIR="$HOME/Developer/deps" ./scripts/setup-macos.sh
```

MicroTeX is intentionally installed as `microtex`. A MacTeX/TeX Live binary
named `LaTeX` is a different program and does not implement MicroTeX's
headless SVG command-line interface.

## Manual build

Once the dependencies are installed:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Important options:

- `PANIM_ENABLE_HWACCEL=ON` adds `-march=native` and `-mtune=native` when the
  compiler supports them. Disable it for portable release binaries.
- `PANIM_ENABLE_FFMPEG=ON` uses `libavformat`, `libavcodec`, `libavutil`, and
  `libswscale` from pkg-config.
- `PANIM_ENABLE_SVG=ON` uses system librsvg, Cairo, and tinyxml2 for SVG
  compositing and glyph-aware equation transitions.
- `PANIM_USE_SPDLOG=ON` uses system spdlog when found and otherwise falls back
  to stderr logging.
- `PANIM_ENABLE_WEBGPU=ON` enables the single WGSL compute implementation when
  an installed wgpu-native distribution can be found.
- `PANIM_BUILD_PREVIEW=ON` builds the optional interactive preview when system
  SDL3 and wgpu-native are available. Headless renders and `panim_core` do not
  depend on SDL3 when this option is disabled.
- `PANIM_WGPU_ROOT=/path/to/wgpu-native` supplies an explicit wgpu-native
  installation. The directory must contain `include/webgpu/webgpu.h` and
  `lib/libwgpu_native.a` or a compatible shared library.
- `PANIM_BUILD_HARDWARE_DEMO=ON` builds a demo that exercises the selected
  compute backend through the shared API.

All bundled plugins are enabled by default. The primary build products are:

- `build/bin/panim`;
- `build/plugins/libQuickStart.dylib` on macOS, or `.so` on Linux;
- `build/plugins/libShowcase.dylib` on macOS, or `.so` on Linux;
- `build/plugins/libSampleWave.dylib` on macOS, or `.so` on Linux; and
- `build/plugins/libLatexDemo.dylib` on macOS, or `.so` on Linux; and
- `build/plugins/libHardwareDemo.dylib` on macOS, or `.so` on Linux; and
- `build/plugins/libFeatureTour.dylib` on macOS, or `.so` on Linux.

Plugin target names match the names accepted by the CLI. Build only the
animation you are editing to avoid recompiling unrelated plugins:

```bash
cmake --build build --target FeatureTour --parallel
./build/bin/panim frame FeatureTour --time 0 --output panim_out/frame.png
```

For a configure preset or IDE that uses `build/Debug`, substitute that build
directory in both commands. `scripts/preview.sh FeatureTour` detects either
layout, builds only the CLI and `FeatureTour`, and then launches the preview.

## Preview, inspect, and render

The normal authoring loop has three explicit commands:

```bash
# Native WebGPU window: no video encode, automatic Metal/Vulkan/D3D12 choice.
./build/bin/panim preview FeatureTour

# One exact, lossless timeline frame for close visual inspection.
./build/bin/panim frame FeatureTour \
    --time 8.8 \
    --size 1920x1080 \
    --output panim_out/integral.png

# Final encoded deliverable through system FFmpeg.
./build/bin/panim render FeatureTour \
    --size 1920x1080 \
    --quality share \
    --output panim_out/feature-tour.mp4
```

`preview` renders the plugin's native canvas directly into a high-DPI WebGPU
surface. It uploads the completed RGBA frame to one presentation texture,
preserves aspect ratio, and does not round-trip through H.264 or a temporary
video. Its visible transport bar provides restart, previous-frame, play/pause,
next-frame, timeline, PNG-capture, and plugin-reload controls. The matching
keyboard shortcuts are:

- Space plays or pauses;
- Left/Right steps one frame, and Shift+Left/Right steps one second;
- Home/End seek to the timeline boundaries;
- clicking or dragging the timeline scrubs;
- S writes a lossless PNG to `panim_out/` (or the `--output` directory);
- R reloads the plugin immediately; and
- Escape closes the preview.

Plugin watching is on by default. Rebuild the active target from another
terminal or your editor, and preview stages and validates the new shared
library before swapping it in. A bad or half-written build is rejected while
the previous animation keeps running. Playback time is preserved across a
successful reload:

```bash
# Terminal 1
./scripts/preview.sh FeatureTour --time 6.8

# Terminal 2, after an edit
cmake --build build --target FeatureTour --parallel
```

Use `--no-watch` for a fixed plugin or `--frames 3` for an auto-closing preview
smoke test. WebGPU chooses the adapter and native graphics API; ordinary
preview commands never specify Metal, Vulkan, D3D12, CUDA, HIP, or a GPU model.

The original command without a subcommand remains a backward-compatible alias
for `render`.

## Final render quality

Run the complete feature tour with the duration, size, and frame rate declared
by the plugin:

```bash
./build/bin/panim render FeatureTour
```

The CLI accepts a plugin name and discovers the correct shared-library prefix
and suffix for the current platform. Named options are the preferred interface:

```bash
./build/bin/panim render Showcase \
    --duration 3 \
    --size 1280x720 \
    --fps 30 \
    --quality share \
    --output panim_out/showcase.mp4
```

Output quality is explicit so previews and final renders do not fight each
other:

- `--quality draft` uses fast H.264 at CRF 23 in broadly compatible 4:2:0;
- `--quality share` (the default) uses CRF 16 in 4:2:0; and
- `--quality master` uses CRF 10 and full-resolution 4:4:4 chroma for a
  high-fidelity source. Some hardware-only players do not support H.264 4:4:4,
  so publish the share render and keep the master as the source artifact.

For line art, equations, and geometry, `--supersample 2` renders the plugin at
twice the output width and height and performs one high-quality Lanczos
downsample before encoding:

```bash
./build/bin/panim render FeatureTour \
    --size 1920x1080 \
    --supersample 2 \
    --quality share \
    --output panim_out/feature-tour-share.mp4
```

Use the `frame` command for a lossless, one-frame timeline inspection. It uses
the same supersampling path and always emits exactly one frame:

```bash
./build/bin/panim frame FeatureTour \
    --time 8.8 \
    --size 1920x1080 \
    --supersample 2 \
    --output panim_out/integral.png
```

The original positional form remains available:

```text
panim [plugin] [seconds] [width] [height] [fps] [output_file]
```

For a short encoded regression, build one plugin and render only the timeline
window under active development:

```bash
./scripts/render.sh FeatureTour \
    --start 6.8 \
    --frames 24 \
    --size 640x360 \
    --output panim_out/integral-preview.mp4
```

Run `./build/bin/panim --help` for the complete command reference.

Inspect an output without opening a video player:

```bash
ffprobe -v error \
    -show_entries stream=codec_name,width,height,r_frame_rate,nb_frames \
    -show_entries format=duration \
    -of default=noprint_wrappers=1 \
    panim_out/showcase.mp4
```

## LaTeX and SVG rendering

The renderer calls MicroTeX in headless mode and caches generated SVGs under
`panim_out/latex/`. Discovery order is:

1. the executable named by `MICROTEX_BIN`;
2. `$MICROTEX_ROOT/build/LaTeX`; and
3. `microtex` on `PATH`.

The generated SVGs are rasterized with librsvg and Cairo. A missing MicroTeX
binary disables equation overlays but does not prevent non-LaTeX animations
from rendering.

`EquationMorph` inspects the generated MicroTeX SVGs and matches identical
glyph outlines between equations. Matching symbols move to their new positions.
Remaining source and target glyphs are paired by proximity, move along a shared
path, and cross-shape; only excess glyphs fade independently. Related
expressions therefore morph best—for example, rearranging one identity—while
unrelated expressions retain visual density through the midpoint.

`LatexTrack` chooses a transition that fits the content. Math uses the
glyph-aware morph above. Pure `\text{}` phrases use a size-aware vertical push
dissolve, which avoids turning unrelated sentences into a brief tangle of
half-letters. Endpoint raster sizes are shared by held and transitioning frames,
so expressions do not jump when their natural SVG bounds differ. An optional
fourth `add_keyframe` argument sets an intentional relative height, which then
changes continuously:

```cpp
track.add_keyframe("\\text{large title}", 0.8, 0.0, 1.2);
track.add_keyframe("\\text{smaller detail}", 1.2, 0.7, 0.85);
```

Transitions around 0.5–0.8 seconds usually make the motion easy to read.

## Portable WebGPU compute

`panim/Compute.hpp` exposes one frame-effect API independent of the GPU vendor.
Its hardware implementation is one WebGPU dispatch implementation plus one
WGSL shader at `shaders/Compute.wgsl`. There are no per-effect CUDA, HIP, or
Metal source copies.

Adapter selection is automatic. panim++ calls WebGPU's adapter request without
a vendor, device, power, or native-API hint. Normal animation code therefore
does not select CUDA, HIP, Metal, Vulkan, D3D12, or a GPU model. It can inspect
`compute_device()` for diagnostics, but it does not need that information to
dispatch work.

[wgpu-native](https://github.com/gfx-rs/wgpu-native) selects the native
graphics API underneath WebGPU:

| Host | Typical native API | GPU vendors |
| --- | --- | --- |
| macOS and iOS | Metal | Apple, supported external GPUs |
| Linux and Android | Vulkan | NVIDIA, AMD, Intel, mobile GPUs |
| Windows | D3D12 or Vulkan | NVIDIA, AMD, Intel |
| Any supported host | Portable fallback | CPU |

Use a prebuilt wgpu-native release or a system installation rather than
vendoring it into the source tree. A manual configuration looks like:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPANIM_WGPU_ROOT=/path/to/wgpu-native
```

`PANIM_WGPU_ROOT` only tells CMake where the wgpu-native library is installed;
it does not select hardware. `PANIM_COMPUTE_BACKEND` and `WGPU_BACKEND` are
optional diagnostic overrides for testing the CPU fallback or a particular
wgpu-native API. They should not appear in ordinary run commands or plugins.

```cpp
#include "panim/Compute.hpp"

panim::ComputeParams params;
params.time_seconds = static_cast<float>(time_sec);
params.strength = 1.0f;

panim::ComputeResult result = panim::apply_compute_effect(
    frame, panim::ComputeEffect::AnimatedGradient, params);
if (!result.ok) {
    PANIM_LOG_ERROR("Compute effect failed: {}", result.message);
}
```

Run the same demo on the automatically selected adapter:

```bash
./build/bin/panim render HardwareDemo \
    --duration 4 \
    --size 1920x1080 \
    --fps 60 \
    --output panim_out/hardware-demo.mp4

# Diagnostic only: exercise the fallback deterministically.
PANIM_COMPUTE_BACKEND=cpu ./build/bin/panim render \
    HardwareDemo \
    --duration 4 \
    --size 1920x1080 \
    --fps 60 \
    --output panim_out/hardware-demo-cpu.mp4
```

The current abstraction accepts the engine's host-resident RGBA frame, so each
WebGPU effect includes an upload and a readback. The implementation reuses its
GPU buffers and pipeline. A future GPU-resident `Frame` is the next architectural
step: it would allow Painter, compositing, and several WGSL effects to remain on
the GPU until a single FFmpeg transfer or a hardware-encoder handoff.

## Authoring animations

The intended author-facing surface has six pieces:

- `Animation` supplies metadata, one setup hook, and one per-frame render hook.
- `Painter` draws and composites CPU frames.
- `Track<T>` animates numbers, positions, and colors with easing.
- `SceneSequence` gives each scene local time and crossfades scene outputs.
- `LatexTrack` and `EquationMorph` render and morph MicroTeX glyphs.
- `apply_compute_effect` runs built-in WGSL effects on the automatically
  selected WebGPU adapter, with a CPU fallback.

A complete plugin can stay small:

```cpp
#include "panim/Animation.hpp"
#include "panim/Painter.hpp"
#include "panim/Plugin.hpp"

class Hello : public panim::Animation {
public:
    panim::AnimationInfo info() const override {
        return {"Hello", 3.0, 1280, 720, 30.0};
    }

    void on_setup(const panim::AnimationContext &ctx) override {
        context_ = ctx;
    }

    void render_frame(panim::Frame &frame, double time) override {
        panim::Painter painter(frame);
        painter.fill_vertical_gradient({8, 14, 34, 255},
                                       {48, 22, 82, 255});
        int x = static_cast<int>((time / 3.0) * context_.width);
        painter.fill_circle(x, context_.height / 2, 48,
                            {120, 224, 255, 255});
    }

private:
    panim::AnimationContext context_{};
};

PANIM_EXPORT_ANIMATION(Hello)
```

Register in-repository plugins with `panim_add_plugin` in `CMakeLists.txt`.
See `plugins/QuickStart.cpp` for a minimal complete animation. See
`plugins/FeatureTour.cpp` for scenes, animated properties, equation morphing,
scaled compositing, and a WGSL Mandelbulb in one animation.

Plugins carry an integer API version through `PANIM_EXPORT_ANIMATION`. Rebuild
plugin binaries after an engine API change; the loader rejects missing or
mismatched versions before calling the C++ animation interface.

The product/API audit and prioritized next steps are in
[`docs/AUDIT.md`](docs/AUDIT.md).

## Install

To stage the CLI, core library, public headers, and all enabled plugins:

```bash
cmake --install build --prefix dist
```

The CLI also searches an installed `lib/panim/plugins` or
`lib64/panim/plugins` directory relative to its executable.
