#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: scripts/preview.sh <PluginName> [panim preview options]" >&2
    echo "Example: scripts/preview.sh FeatureTour --time 6.8" >&2
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"

if [[ -n "${PANIM_BUILD_DIR:-}" ]]; then
    build_dir="${PANIM_BUILD_DIR}"
elif [[ -f "${repo_root}/build/CMakeCache.txt" ]]; then
    build_dir="${repo_root}/build"
elif [[ -f "${repo_root}/build/Debug/CMakeCache.txt" ]]; then
    build_dir="${repo_root}/build/Debug"
else
    build_dir="${repo_root}/build"
fi

plugin_name="$1"
shift

cmake --build "${build_dir}" --target panim "${plugin_name}" --parallel
exec "${build_dir}/bin/panim" preview "${plugin_name}" "$@"
