#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
u=upstream/emutos
b=build/generated
mkdir -p "$b"
cc -O2 "$u/tools/erd.c" -o "$b/erd"
cc -O2 -DGEM_RSC "$u/tools/erd.c" -o "$b/grd"
cc -O2 -DICON_RSC "$u/tools/erd.c" -o "$b/ird"
cc -O2 -DMFORM_RSC "$u/tools/erd.c" -o "$b/mrd"
cc -O2 -DMACHINE_DREAMCAST -DWITH_AES=1 -Iinclude -iquote "$u/include" "$u/tools/draft.c" "$u/tools/draftexc.c" -o "$b/draft"
"$b/draft" "$u/desk/desktop" "$b/desktop"
"$b/erd" -pdesk "$b/desktop" "$b/desk_rsc"
"$b/grd" "$u/aes/gem" "$b/gem_rsc"
"$b/ird" -picon "$u/desk/icon" "$b/icons"
"$b/mrd" -pmform "$u/aes/mform" "$b/mforms"
printf '#define CONF_WITH_NLS 0\n#define CONF_MULTILANG 0\n' > "$b/i18nconf.h"
