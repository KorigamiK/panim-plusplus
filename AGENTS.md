# Panim++ Development Guide

This repository hosts **panim++**, a programmatic animation engine written in C++20. It uses a core-plugin architecture where the engine loads shared libraries (plugins) to render frames.

## 1. Build & Run

### Build System
The project uses **CMake** (3.18+) and C++20.
- **Build Type**: Defaults to `Release`.
- **CPU Acceleration**: Enabled by default (`-march=native`).
- **FFmpeg**: Required for video output via `pkg-config`.
- **GPU Compute**: One WGSL implementation runs through wgpu-native. Set
  `PANIM_WGPU_ROOT` when it is not installed in a default prefix. Adapter and
  native-API selection are automatic; environment overrides are diagnostic.
- **Exceptions**: Disabled globally (`-fno-exceptions`).

### Prerequisites (Debian/Ubuntu)
```bash
sudo apt-get install cmake build-essential pkg-config \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
    libcairo2-dev librsvg2-dev libtinyxml2-dev
```

### Prerequisites (macOS)

Use Homebrew packages and keep MicroTeX outside the repository:

```bash
./scripts/setup-macos.sh
```

For a manual system-dependency install, run `brew bundle --file Brewfile`.

### Compilation Commands
```bash
# Configure (Create build directory if it doesn't exist)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build all targets (core, main executable, plugins)
cmake --build build --parallel

# Build a specific target
cmake --build build --target panim
cmake --build build --target SampleWave
cmake --build build --target hardware_demo
cmake --build build --target FeatureTour
```

### Running & Testing
The project has lightweight core tests without an external test framework:

```bash
ctest --test-dir build --output-on-failure
```

Visual verification is done by running `panim` with a plugin and checking the
output.

**Run the default sample:**
```bash
./build/bin/panim
```

**Run a specific plugin:**
```bash
# Plugin names are portable across `.dylib`, `.so`, and `.dll` hosts.
./build/bin/panim Showcase --duration 5 --size 1920x1080 --fps 60 \
    --output output.mp4
```

On macOS, use `build/plugins/libShowcase.dylib`; the no-argument invocation
discovers the correct platform suffix automatically.

**Verification Step:**
After modifying core code or plugins, always run the `SampleWave` or `Showcase`
plugin to ensure the video renders without crashing and no errors appear in the
logs. Run `HardwareDemo` after compute-backend changes.

## 2. Code Style & Conventions

### Formatting
- **Indentation**: 4 spaces.
- **Braces**: K&R style (opening brace on the same line).
- **Line Length**: Keep strictly under 100 columns where possible.
- **File Encoding**: UTF-8.

### Naming Conventions
- **Files**: PascalCase (e.g., `Frame.cpp`, `Animation.hpp`).
- **Classes/Structs**: PascalCase (e.g., `AnimationContext`, `VideoWriter`).
- **Functions/Methods**: snake_case (e.g., `render_frame`, `init_logging`).
- **Variables**: snake_case (e.g., `frame_count`, `ctx`).
- **Namespaces**: snake_case (e.g., `panim`).
- **Macros**: UPPER_CASE (e.g., `PANIM_LOG_INFO`).
- **Private Members**: Trailing underscore (e.g., `width_`, `data_`) is common but not strictly enforced in `main.cpp`; consistency with the specific file being edited is key.

### Language Idioms (C++20)
- **Standard**: strict C++20 (`-std=c++20`).
- **Exceptions**: **Disabled** for core libraries (`-fno-exceptions`). Do not use `try/catch`. Use error codes or status objects (e.g., `writer.ok()`).
- **Memory Management**: Prefer RAII. Use smart pointers (`std::unique_ptr`, `std::shared_ptr`) or raw pointers for non-owning references.
- **Strings**: `std::string` and `std::string_view`.
- **Filesystem**: `std::filesystem`.

### Project Structure
- `src/`: Core engine implementation.
- `include/panim/`: Public API headers.
- `plugins/`: Example animations (implementing `Animation` interface).
- `panim_core`: The main library target.
- `panim`: The CLI executable.

## 3. Agent Instructions

### General
- **Context is King**: Before editing, read the file to match local style (imports order, specific variable naming patterns).
- **Safety**: Do not assume libraries (like Boost) are available unless you see them in `CMakeLists.txt`.
- **No Exceptions**: Remember that `-fno-exceptions` is set. Code that throws will fail to compile or link. Handle errors explicitly.

### Adding a New File
1. Create the `.hpp` in `include/panim/` and `.cpp` in `src/`.
2. Register the new source file in `CMakeLists.txt` under `add_library(panim_core ...)`.

### Modifying Plugins
- Plugins must export the API version, `create_animation`, and
  `destroy_animation` C-compatible symbols. Use
  `PANIM_EXPORT_ANIMATION(Type)` to emit all three.
- Plugins derive from `panim::Animation`.

### Creating a New Plugin
To create a new animation plugin `MyAnim`:
1. Create `plugins/MyAnim.cpp` and include `panim/Plugin.hpp`.
2. Export the plugin with `PANIM_EXPORT_ANIMATION(MyAnim)`.
3. Add a build target in `CMakeLists.txt`:
   ```cmake
   panim_add_plugin(my_anim plugins/MyAnim.cpp MyAnim)
   ```

### Logging
- Use the `PANIM_LOG_*` macros defined in `panim/Log.hpp`.
- Example: `PANIM_LOG_INFO("Processing frame {}", frame_index);`

### Error Handling Pattern
Return a status struct or boolean, and log the error.
```cpp
// Preferred pattern
if (!component.initialize()) {
    PANIM_LOG_ERROR("Failed to init component: {}", component.error_message());
    return false; // or error code
}
```

### Debugging & Diagnostics
Since exceptions are disabled:
- **GDB**: Use `gdb --args ./build/bin/panim ...` to debug crashes.
- **Sanitizers**: To catch memory errors, configure with sanitizers:
  ```bash
  cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address"
  cmake --build build
  ```
- **Logging**: Ensure `panim::init_logging()` is called. Logs go to stdout/stderr.

### Common Pitfalls
1. **Header Inclusion**: When adding new core headers, ensure they are added to `include/panim/` and included as `#include "panim/Header.hpp"`.
2. **Linker Errors**: If adding a new dependency, ensure it is linked in `CMakeLists.txt` using `target_link_libraries`.
3. **Runtime Errors**: If the engine crashes, check if the plugin path is correct. The `panim` CLI attempts to guess, but explicit paths are safer.
4. **Export Symbols**: Prefer `PANIM_EXPORT_ANIMATION(Type)` so both required
   C-compatible symbols and export visibility stay consistent.
