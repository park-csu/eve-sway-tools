#!/bin/sh
set -eu

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
lib_dir="${HOME}/.local/lib/eve-sway-tools"
manifest_dir="${HOME}/.local/share/vulkan/implicit_layer.d"

mkdir -p "${lib_dir}" "${manifest_dir}"
library_tmp=$(mktemp "${lib_dir}/libeve_sway_tools_fps.so.XXXXXX")
trap 'rm -f "${library_tmp}"' EXIT
cc -O2 -fPIC -fvisibility=hidden -shared \
    -Wl,-z,defs \
    -o "${library_tmp}" \
    "${source_dir}/src/eve-sway-fps-limit.c" \
    -pthread
chmod 0755 "${library_tmp}"
mv -f "${library_tmp}" "${lib_dir}/libeve_sway_tools_fps.so"
trap - EXIT

cat >"${manifest_dir}/VK_LAYER_EVE_sway_tools.json" <<EOF
{
  "file_format_version": "1.2.0",
  "layer": {
    "name": "VK_LAYER_EVE_sway_tools",
    "type": "GLOBAL",
    "library_path": "${lib_dir}/libeve_sway_tools_fps.so",
    "api_version": "1.4.0",
    "implementation_version": "1",
    "description": "eve-sway-tools focus-aware FPS limiter",
    "enable_environment": {
      "EVE_SWAY_TOOLS_ENABLE_FPS": "1"
    },
    "disable_environment": {
      "EVE_SWAY_TOOLS_DISABLE_FPS": "1"
    }
  }
}
EOF
