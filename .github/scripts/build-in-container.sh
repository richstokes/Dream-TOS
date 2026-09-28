#!/bin/sh
# Run only inside the pinned compiler container, with the checkout at /workspace.
set -eu
cd /workspace
: "${KOS_COMMIT:?Set KOS_COMMIT to the pinned SDK revision}"

# The compiler image is Alpine-based. These are host tools, not Dreamcast ports.
# perl-utils supplies shasum used by the shared local/CI packaging script.
apk add --no-cache bash build-base git python3 py3-pip ninja pkgconf libisofs-dev perl-utils

kos_base=/workspace/build/ci/kos
mkdir -p "$kos_base"
if [ ! -d "$kos_base/.git" ]; then
    git init "$kos_base"
    git -C "$kos_base" remote add origin https://github.com/KallistiOS/KallistiOS.git
fi
git -C "$kos_base" fetch --depth 1 origin "$KOS_COMMIT"
git -C "$kos_base" checkout --detach "$KOS_COMMIT"

# Use the SDK's compiler locations, changing only its source checkout path.
cp "$kos_base/doc/environ.sh.sample" "$kos_base/environ.sh"
sed -i "s|^export KOS_BASE=.*|export KOS_BASE=\"$kos_base\"|" "$kos_base/environ.sh"
export KOS_ENV="$kos_base/environ.sh"
set +u
# shellcheck disable=SC1090
. "$KOS_ENV"
set -u
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')
export JOBS="$jobs"
# Keep KOS's default target aliases enabled. Some SDK subdirectories have
# all_naomi as their first make target even when KOS_SUBARCH is pristine;
# restricting KOS_BUILD_SUBARCHS makes those default targets silently do nothing.
make -C "$kos_base" -j"$jobs"
test -s "$kos_base/lib/dreamcast/_kos_startup.o"
test -s "$kos_base/lib/dreamcast/libkallisti.a"

# Reuse the developer scripts, including the pinned mkdcdisc revision.
bash scripts/bootstrap-mkdcdisc.sh
bash scripts/build-cdi.sh
python3 -m unittest discover -s tests -v

# A successful compiler invocation must actually have produced native SH-4 code.
"$KOS_CC_BASE/bin/$KOS_CC_PREFIX-readelf" -h dist/emutos-dreamcast.elf
python3 - <<'PY'
import struct
from pathlib import Path

raw = Path('dist/emutos-dreamcast.elf').read_bytes()
if raw[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<HH', raw, 16) != (2, 42):
    raise SystemExit('Expected a little-endian ELF32 SuperH executable')
if Path('dist/emutos-dreamcast.cdi').stat().st_size < 1024 * 1024:
    raise SystemExit('CDI output is missing or unexpectedly small')
PY
(cd dist && shasum -a 256 --check SHA256SUMS)
