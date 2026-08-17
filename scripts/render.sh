#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: scripts/render.sh <PluginName> [panim options]" >&2
    echo "Example: scripts/render.sh FeatureTour --start 6.8 --frames 24 --size 640x360" >&2
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${PANIM_BUILD_DIR:-${repo_root}/build}"
plugin_name="$1"
shift

cmake --build "${build_dir}" --target "${plugin_name}" --parallel
exec "${build_dir}/bin/panim" render "${plugin_name}" "$@"
