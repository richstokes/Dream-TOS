#!/bin/bash
# Rebuild vendored C programs against the native GEM/newlib runtime.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
set +u
source "${KOS_ENV:-${KOS_BASE:-$HOME/.local/share/dreamcast/kos}/environ.sh}"
set -u
cd "$root"
mkdir -p build/apps
flags=(-m4-single -ml -O2 -g -ffunction-sections -fdata-sections -ffreestanding -fno-pic -fno-pie -fno-common -fno-unwind-tables -fno-asynchronous-unwind-tables -Iinclude -Iapps/lib -D_GNU_SOURCE -nostdlib -Wl,--gc-sections -Wl,--emit-relocs -Wl,-T,apps/native.ld)
# Older KOS-patched newlib headers include kos/cond.h and arch/types.h.
# These are type declarations only: applications still link our own runtime.
flags+=(-isystem "$KOS_BASE/include" -isystem "$KOS_BASE/kernel/arch/dreamcast/include" -D_arch_dreamcast=1 -D_arch_sub_pristine=1 -D__DREAMCAST__)
runtime=(apps/lib/runtime.c apps/lib/app.c apps/lib/window.c apps/lib/accessory.c)
appbuild() {
 local name=$1; shift
 "$KOS_CC" "${flags[@]}" "${runtime[@]}" "$@" -Wl,--start-group -lm -lc -lgcc -Wl,--end-group -o "build/apps/$name.elf"
 python3 tools/native_app.py "build/apps/$name.elf" "build/apps/$name.PRG"
}
build_ssh() {
 source scripts/ssh-sources.sh
 appbuild SSH apps/ssh/platform_dc.c "${ssh_flags[@]}" "${ssh_sources[@]}"
 mv build/apps/SSH.PRG build/apps/SSH.TTP
}
if [[ "${1:-}" == --editor-only ]]; then
 appbuild EDITOR apps/ports/editor.c apps/ports/editor_core.c apps/ports/editor_file.c
 exit 0
fi
if [[ "${1:-}" == --ssh-only ]]; then
 build_ssh
 exit 0
fi
if [[ "${1:-}" == --vmu-only ]]; then
 appbuild VMUEDIT apps/ports/vmuedit.c apps/ports/vmuedit_core.c
 exit 0
fi
puzzle_common=(apps/vendor/puzzles/{midend,drawing,misc,malloc,random,tree234,dsf,grid,penrose,penrose-legacy,hat,spectre,sort,tdq,findloop}.c)
for game in fifteen mines net; do
 name=$(printf '%s' "$game" | tr '[:lower:]' '[:upper:]')
 appbuild "$name" apps/ports/puzzle.c "${puzzle_common[@]}" "apps/vendor/puzzles/$game.c" -Iapps/vendor/puzzles -DHAVE_STDINT_H
 done
appbuild EDITOR apps/ports/editor.c apps/ports/editor_core.c apps/ports/editor_file.c
appbuild WORM apps/ports/worm.c apps/vendor/gemworm/{field,player,scores}.c -Iapps/vendor/gemworm
appbuild BLOCKS apps/ports/blocks.c apps/ports/blocks_core.c
appbuild IMAGES apps/ports/viewer.c -Iapps/vendor/stb
appbuild PAINT apps/ports/paint.c
appbuild RUNTIME apps/ports/runtime_test.c
appbuild BENCH apps/ports/bench.c apps/ports/bench_core.c -ffp-contract=off
appbuild MP3 apps/ports/mp3.c apps/ports/mp3_core.c -Iapps/vendor/minimp3
appbuild VMUEDIT apps/ports/vmuedit.c apps/ports/vmuedit_core.c
appbuild FTP apps/ports/ftp.c apps/ports/ftp_core.c apps/ports/ftp_fs.c
appbuild SDFORMAT apps/ports/sdformat.c

# Accessories use the same validated native format, with a resident AES entry.
appbuild CALC apps/ports/calc.c apps/vendor/tinyexpr/tinyexpr.c -Iapps/vendor/tinyexpr
mv build/apps/CALC.PRG build/apps/CALC.ACC
appbuild CLOCK apps/ports/clock.c
mv build/apps/CLOCK.PRG build/apps/CLOCK.ACC

for accessory in CONTROL MONITOR; do
 source_name=$(printf '%s' "$accessory" | tr '[:upper:]' '[:lower:]')
 appbuild "$accessory" "apps/ports/$source_name.c"
 mv "build/apps/$accessory.PRG" "build/apps/$accessory.ACC"
done

# Console applications use TTP so EmuDesk supplies a command-tail dialog.
for tool in grep wc head tail sort hexdump cksum date df free uname expr; do
 name=$(printf '%s' "$tool" | tr '[:lower:]' '[:upper:]')
 appbuild "$name" apps/ports/cli_tools.c apps/vendor/tinyexpr/tinyexpr.c -Iapps/vendor/tinyexpr "-DCLI_TOOL=\"$tool\""
 mv "build/apps/$name.PRG" "build/apps/$name.TTP"
done

# Network tools use the KOS IPv4 stack through the optional net_* native API.
for tool in ping nslookup ifconfig; do
 name=$(printf '%s' "$tool" | tr '[:lower:]' '[:upper:]')
 appbuild "$name" apps/ports/net_tools.c "-DNET_TOOL=\"$tool\""
 mv "build/apps/$name.PRG" "build/apps/$name.TTP"
done

build_ssh
