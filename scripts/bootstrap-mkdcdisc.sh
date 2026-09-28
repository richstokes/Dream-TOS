#!/bin/bash
# Fetch the exact official mkdcdisc revision used to test this port.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
path="$root/build/tools/mkdcdisc"
revision=a663882c4ac0e2123c6229af6dd596cee7a66dc1
mkdir -p "$root/build/tools"
for tool in git python3 pkg-config ninja; do
 if ! command -v "$tool" >/dev/null; then echo "Missing build prerequisite: $tool" >&2;exit 1;fi
done
if ! pkg-config --exists libisofs-1; then
 echo 'Install libisofs development files (macOS: brew install libisofs pkg-config ninja).' >&2;exit 1
fi
# Keep Python build tooling local to this checkout, without changing system Python.
venv="$root/build/tools/python"
if [ ! -x "$venv/bin/python" ]; then python3 -m venv "$venv"; fi
if ! "$venv/bin/python" -c 'import mesonbuild' 2>/dev/null; then
 "$venv/bin/python" -m pip install 'meson==1.9.1'
fi
meson="$venv/bin/meson"
if [ ! -d "$path" ]; then git clone https://gitlab.com/simulant/mkdcdisc.git "$path"; fi
if [ -n "$(git -C "$path" status --porcelain)" ]; then echo "Refusing to change modified checkout: $path" >&2;exit 1;fi
git -C "$path" fetch origin "$revision"
git -C "$path" checkout --detach "$revision"
if [ ! -d "$path/build" ]; then "$meson" setup "$path/build" "$path"; fi
"$meson" compile -C "$path/build"
