#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
set +u
source "${KOS_ENV:-${KOS_BASE:-$HOME/.local/share/dreamcast/kos}/environ.sh}"
set -u
mkdir -p "$root/build/apps"
for app in hello vditest; do
"$KOS_CC" -m4-single -ml -O2 -ffreestanding -fno-pic -fno-pie -fpack-struct=2 -fno-common \
 -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdlib -I"$root/include" \
 -Wl,--emit-relocs -Wl,-T,"$root/apps/native.ld" "$root/apps/$app.c" -o "$root/build/apps/$app.elf"
name=$(printf '%s' "$app" | tr '[:lower:]' '[:upper:]')
python3 "$root/tools/native_app.py" "$root/build/apps/$app.elf" "$root/build/apps/$name.PRG"
done

"$root/scripts/build-bundle.sh"
