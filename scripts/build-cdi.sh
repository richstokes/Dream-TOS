#!/bin/bash
# Build the native OS, demo application, read-only FAT volume and bootable CDI.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
"$root/scripts/build.sh"
"$root/scripts/build-apps.sh"
mkdcdisc=${MKDCDISC:-$root/build/tools/mkdcdisc/build/mkdcdisc}
if [ ! -x "$mkdcdisc" ]; then mkdcdisc=$(command -v mkdcdisc || true); fi
if [ -z "$mkdcdisc" ] || [ ! -x "$mkdcdisc" ]; then
 echo 'Install mkdcdisc with scripts/bootstrap-mkdcdisc.sh, or set MKDCDISC.' >&2;exit 1
fi
mkdir -p "$root/build/disc" "$root/dist"
# A fresh temporary staging directory prevents stale app files entering D:.
stage=$(mktemp -d "$root/build/fat-stage.XXXXXX")
cp "$root"/disc/* "$stage/"
python3 "$root/tools/stage_bundle.py" "$root" "$stage"
python3 "$root/tools/fat_image.py" "$stage" "$root/build/disc/DISC.IMG"
python3 - "$stage" <<'PY'
import shutil,sys
shutil.rmtree(sys.argv[1])
PY
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1790553600}
# GDEMU and Flycast need no full-disc filler. Enable it for a padded CD-R image.
padding=(--disable-data-track-padding)
if [ "${CD_PADDING:-0}" = 1 ]; then padding=(); fi
"$mkdcdisc" --allow-overwrite "${padding[@]}" --main-elf "$root/build/emutos-dreamcast.elf" \
 --directory-contents "$root/build/disc" --title 'EMUTOS DREAMCAST' \
 --author 'EMUTOS DC PORT' --release 20260928 --output "$root/dist/emutos-dreamcast.cdi"
cp "$root/build/emutos-dreamcast.elf" "$root/dist/emutos-dreamcast.elf"
(cd "$root/dist" && shasum -a 256 emutos-dreamcast.elf emutos-dreamcast.cdi > SHA256SUMS)
echo "Built $root/dist/emutos-dreamcast.cdi"
