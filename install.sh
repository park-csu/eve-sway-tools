#!/bin/sh
set -eu

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
bin_dir="${HOME}/.local/bin"
config_dir="${XDG_CONFIG_HOME:-$HOME/.config}/eve-sway-tools"

mkdir -p "${bin_dir}" "${config_dir}"
install -m 0755 "${source_dir}/bin/eve-sway-cycle" "${bin_dir}/eve-sway-cycle"
install -m 0755 "${source_dir}/bin/eve-sway-manager" "${bin_dir}/eve-sway-manager"

if [ ! -e "${config_dir}/config.yaml" ]; then
    install -m 0644 "${source_dir}/config.example.yaml" "${config_dir}/config.yaml"
fi

"${source_dir}/install-vulkan-layer.sh"

printf '%s\n' \
    "Installed eve-sway-tools." \
    "Enable the Vulkan limiter for EVE's Proton environment with:" \
    "  EVE_SWAY_TOOLS_ENABLE_FPS=1"
