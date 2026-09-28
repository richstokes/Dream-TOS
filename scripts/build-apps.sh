#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
set +u
source "${KOS_ENV:-${KOS_BASE:-$HOME/.local/share/dreamcast/kos}/environ.sh}"
set -u
mkdir -p "$root/build/apps"
"$KOS_CC" -m4-single -ml -O2 -ffreestanding -fno-pic -fno-pie -fpack-struct=2 -fno-common \
 -fno-unwind-tables -fno-asynchronous-unwind-tables -nostdlib -I"$root/include" \
 -Wl,--emit-relocs -Wl,-T,"$root/apps/native.ld" "$root/apps/hello.c" -o "$root/build/apps/hello.elf"
python3 "$root/tools/native_app.py" "$root/build/apps/hello.elf" "$root/build/apps/HELLO.PRG"
