#!/bin/bash
# One-time: python3 -m venv build/ssh-venv
#           build/ssh-venv/bin/pip install -r tests/ssh-requirements.txt
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/ssh-sources.sh
bash scripts/build-ssh-host.sh
${CC:-cc} -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
 "${ssh_flags[@]}" "${ssh_sources[@]}" tests/ssh_unit_host.c -o build/ssh-unit
${CC:-cc} -std=gnu11 -O1 -g -fsanitize=address,undefined -Iinclude -Iapps/lib -Iapps/ssh \
 apps/ssh/platform_dc.c tests/ssh_native_input_host.c -o build/ssh-input
build/ssh-input
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
(cd "$work" && "$OLDPWD/build/ssh-unit")
"${SSH_TEST_PYTHON:-build/ssh-venv/bin/python}" tests/ssh_integration.py "$@"
