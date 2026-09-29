#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
image=${1:-$root/dist/emutos-dreamcast.cdi}
flycast=${FLYCAST_BIN:-$HOME/.local/share/dreamcast/flycast/Flycast.app/Contents/MacOS/Flycast}
if [ ! -x "$flycast" ]; then flycast=/Applications/Flycast.app/Contents/MacOS/Flycast; fi
if [ ! -x "$flycast" ]; then echo 'Set FLYCAST_BIN to the Flycast executable.' >&2;exit 1;fi
if [ ! -f "$image" ]; then
 if [ "$#" -eq 0 ]; then "$root/scripts/build-cdi.sh";
 else echo "Image not found: $image" >&2;exit 1;fi
fi
# All options are transient. Map physical host inputs to the correct Maple ports.
options="config:Debug.SerialConsoleEnabled=yes,input:device1=0,input:device2=5,input:device3=6,input:maple_sdl_keyboard=1,input:maple_sdl_mouse=${FLYCAST_HOST_MOUSE_PORT:-2}"
# Broadband Adapter emulation with Flycast's local PicoTCP stack (built-in DHCP,
# NAT and DNS through the host). Set FLYCAST_BBA=0 to boot without an adapter.
if [ "${FLYCAST_BBA:-1}" != 0 ]; then options+=",network:EmulateBBA=yes,network:DCNet=no"; fi
# Optional isolated VMU directory for repeatable, non-destructive toolbox tests.
if [ -n "${FLYCAST_VMU_DIR:-}" ]; then
 if [ ! -d "$FLYCAST_VMU_DIR" ]; then echo 'FLYCAST_VMU_DIR must be an existing directory.' >&2;exit 1;fi
 options+=",config:Dreamcast.VMUPath=$FLYCAST_VMU_DIR,config:PerGameVmu=no,input:device1.1=1,input:device1.2=1"
fi
exec "$flycast" -config "$options" "$image"
