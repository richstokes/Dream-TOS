#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
image=${1:-$root/build/emutos-dreamcast.elf}
flycast=${FLYCAST_BIN:-$HOME/.local/share/dreamcast/flycast/Flycast.app/Contents/MacOS/Flycast}
if [ ! -x "$flycast" ]; then flycast=/Applications/Flycast.app/Contents/MacOS/Flycast; fi
if [ ! -x "$flycast" ]; then echo 'Set FLYCAST_BIN to the Flycast executable.' >&2;exit 1;fi
if [ ! -f "$image" ]; then
 if [ "$#" -eq 0 ]; then "$root/scripts/build.sh";
 else echo "Image not found: $image" >&2;exit 1;fi
fi
# All options are transient. Map physical host inputs to the correct Maple ports.
exec "$flycast" -config "config:Debug.SerialConsoleEnabled=yes,input:device1=0,input:device2=5,input:device3=6,input:maple_sdl_keyboard=1,input:maple_sdl_mouse=${FLYCAST_HOST_MOUSE_PORT:-2}" "$image"
