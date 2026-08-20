#!/usr/bin/env bash

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This setup script is for macOS." >&2
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is required: https://brew.sh" >&2
    exit 1
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
dependency_root="${PANIM_DEPS_DIR:-$(cd -- "${repo_root}/.." && pwd)}"
microtex_root="${MICROTEX_ROOT:-${dependency_root}/MicroTeX}"
microtex_ref="0e3707f6dafebb121d98b53c64364d16fefe481d"
microtex_patch="${repo_root}/patches/MicroTeX-macos.patch"
wgpu_version="v29.0.1.1"
wgpu_root="${dependency_root}/wgpu-native-${wgpu_version}"

case "$(uname -m)" in
arm64)
    wgpu_asset="wgpu-macos-aarch64-release.zip"
    wgpu_sha256="a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e"
    ;;
x86_64)
    wgpu_asset="wgpu-macos-x86_64-release.zip"
    wgpu_sha256="8e2f7378548ddd0e2cf21e7d864dda46e953f0af724855a33778b85ead206d41"
    ;;
*)
    echo "wgpu-native has no configured macOS asset for $(uname -m)." >&2
    exit 1
    ;;
esac

echo "Installing Homebrew dependencies..."
brew bundle --file="${repo_root}/Brewfile"

if [[ ! -d "${microtex_root}/.git" ]]; then
    if [[ -e "${microtex_root}" ]]; then
        echo "MicroTeX path exists but is not a Git checkout: ${microtex_root}" >&2
        exit 1
    fi

    echo "Cloning MicroTeX into ${microtex_root}..."
    git clone https://github.com/NanoMichael/MicroTeX.git "${microtex_root}"
    git -C "${microtex_root}" checkout "${microtex_ref}"
fi

if git -C "${microtex_root}" apply --reverse --check "${microtex_patch}" 2>/dev/null; then
    echo "MicroTeX macOS patch is already applied."
elif git -C "${microtex_root}" apply --check "${microtex_patch}"; then
    echo "Applying MicroTeX macOS build patch..."
    git -C "${microtex_root}" apply "${microtex_patch}"
else
    echo "MicroTeX checkout cannot accept the required macOS patch." >&2
    echo "Use a clean checkout at ${microtex_ref}, or inspect ${microtex_root}." >&2
    exit 1
fi

echo "Building MicroTeX..."
cmake \
    -S "${microtex_root}" \
    -B "${microtex_root}/build" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DHAVE_LOG=OFF \
    -DGRAPHICS_DEBUG=OFF
cmake --build "${microtex_root}/build" --parallel

ensure_symlink() {
    local source_path="$1"
    local link_path="$2"

    if [[ -L "${link_path}" ]]; then
        if [[ "$(readlink "${link_path}")" == "${source_path}" ]]; then
            return
        fi
        echo "Refusing to replace existing symlink: ${link_path}" >&2
        exit 1
    fi
    if [[ -e "${link_path}" ]]; then
        echo "Refusing to replace existing path: ${link_path}" >&2
        exit 1
    fi

    ln -s "${source_path}" "${link_path}"
}

brew_prefix="$(brew --prefix)"
microtex_bin="${microtex_root}/build/LaTeX"
microtex_resources="${microtex_root}/build/res"
microtex_link="${brew_prefix}/bin/microtex"
data_home="${XDG_DATA_HOME:-${HOME}/.local/share}"
resource_link="${data_home}/clatexmath"

mkdir -p "${data_home}"
ensure_symlink "${microtex_bin}" "${microtex_link}"
ensure_symlink "${microtex_resources}" "${resource_link}"

smoke_dir="$(mktemp -d "${TMPDIR:-/tmp}/panim-microtex.XXXXXX")"
trap 'rm -rf "${smoke_dir}"' EXIT

"${microtex_link}" \
    -headless \
    '-input=e^{i\pi}+1=0' \
    '-foreground=#ffffffff' \
    '-background=#00000000' \
    '-padding=4' \
    "-output=${smoke_dir}/formula.svg"

if [[ ! -s "${smoke_dir}/formula.svg" ]]; then
    echo "MicroTeX smoke render did not produce an SVG." >&2
    exit 1
fi

if [[ ! -f "${wgpu_root}/include/webgpu/webgpu.h" ||
      ! -f "${wgpu_root}/lib/libwgpu_native.a" ]]; then
    if [[ -e "${wgpu_root}" ]]; then
        echo "Incomplete wgpu-native installation: ${wgpu_root}" >&2
        exit 1
    fi

    wgpu_archive="${dependency_root}/${wgpu_asset%.zip}-${wgpu_version}.zip"
    if [[ ! -f "${wgpu_archive}" ]]; then
        echo "Downloading wgpu-native ${wgpu_version}..."
        curl -fL \
            "https://github.com/gfx-rs/wgpu-native/releases/download/${wgpu_version}/${wgpu_asset}" \
            -o "${wgpu_archive}"
    fi

    actual_wgpu_sha256="$(shasum -a 256 "${wgpu_archive}" | awk '{print $1}')"
    if [[ "${actual_wgpu_sha256}" != "${wgpu_sha256}" ]]; then
        echo "wgpu-native archive checksum mismatch: ${wgpu_archive}" >&2
        exit 1
    fi

    echo "Installing wgpu-native outside the repository at ${wgpu_root}..."
    mkdir -p "${wgpu_root}"
    ditto -x -k "${wgpu_archive}" "${wgpu_root}"
fi

echo "Building panim++..."
cmake \
    -S "${repo_root}" \
    -B "${repo_root}/build" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${wgpu_root};${brew_prefix}"
cmake --build "${repo_root}/build" --parallel
ctest --test-dir "${repo_root}/build" --output-on-failure

echo "Rendering a short panim++ smoke video..."
(
    cd "${repo_root}"
    ./build/bin/panim \
        Showcase \
        --duration 0.25 \
        --size 320x180 \
        --fps 12 \
        --output panim_out/setup-smoke.mp4

    ./build/bin/panim \
        HardwareDemo \
        --duration 0.25 \
        --size 320x180 \
        --fps 12 \
        --output panim_out/hardware-smoke.mp4

    ./build/bin/panim \
        FeatureTour \
        --start 3.3 \
        --frames 3 \
        --size 320x180 \
        --fps 12 \
        --output panim_out/feature-smoke.mp4
)

echo "macOS setup complete."
echo "Run: ${repo_root}/build/bin/panim"
