#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/ssh-sources.sh
mkdir -p build
${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections -Iinclude "${ssh_flags[@]}" "${ssh_sources[@]}" tests/ssh_platform_host.c src/dreamcast/hal_tcp.c -o build/ssh-host
