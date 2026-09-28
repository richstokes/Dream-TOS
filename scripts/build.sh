#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
kos_env=${KOS_ENV:-${KOS_BASE:-$HOME/.local/share/dreamcast/kos}/environ.sh}
if [ ! -f "$kos_env" ]; then echo "Set KOS_ENV to your KallistiOS environ.sh" >&2; exit 1; fi
set +u
source "$kos_env"
set -u
"$root/scripts/resources.sh"
make -C "$root" -j"${JOBS:-8}" "$@"
