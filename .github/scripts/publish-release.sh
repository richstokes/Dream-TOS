#!/bin/bash
# Publish only tested artifacts. Called by the main-only workflow publish job.
set -euo pipefail
: "${GITHUB_REPOSITORY:?}"
: "${GITHUB_SHA:?}"
: "${GH_TOKEN:?}"
output=${1:-dist}
output=$(cd -- "$output" && pwd)
assets=(emutos-dreamcast.cdi emutos-dreamcast.elf BUILD-INFO.txt SHA256SUMS)
for asset in "${assets[@]}"; do
    test -s "$output/$asset" || { echo "Missing release asset: $asset" >&2; exit 1; }
done
(cd "$output" && sha256sum --check SHA256SUMS)

notes=$(mktemp)
trap 'rm -f "$notes"' EXIT
cat > "$notes" <<EOF
Latest successful native SH-4 development build from main at [${GITHUB_SHA:0:7}](https://github.com/$GITHUB_REPOSITORY/commit/$GITHUB_SHA).

- **emutos-dreamcast.cdi**: self-booting CD image for Flycast and Dreamcast/GDEMU testing.
- **emutos-dreamcast.elf**: executable for direct loading and development.
- **SHA256SUMS**: download checksums; **BUILD-INFO.txt** records source and toolchain revisions.

C: is a temporary RAM disk; D: is read-only disc storage. Atari 68000 executables cannot run on this native port.
This is a development snapshot, not a guarantee of real-hardware compatibility. See the [testing notes](https://github.com/$GITHUB_REPOSITORY/blob/$GITHUB_SHA/docs/TESTING.md) for current verification and limitations.

This rolling release is updated after successful main builds. The source archives below match the continuous tag.
EOF

# Update the one rolling tag through the API: no Git credentials or broad
# write permissions are exposed to the compiler container or PR builds.
if gh api "repos/$GITHUB_REPOSITORY/git/ref/tags/continuous" >/dev/null 2>&1; then
    gh api --method PATCH "repos/$GITHUB_REPOSITORY/git/refs/tags/continuous" \
        -f sha="$GITHUB_SHA" -F force=true >/dev/null
else
    gh api --method POST "repos/$GITHUB_REPOSITORY/git/refs" \
        -f ref=refs/tags/continuous -f sha="$GITHUB_SHA" >/dev/null
fi

paths=()
for asset in "${assets[@]}"; do paths+=("$output/$asset"); done
if gh release view continuous --repo "$GITHUB_REPOSITORY" >/dev/null 2>&1; then
    gh release upload continuous "${paths[@]}" --clobber --repo "$GITHUB_REPOSITORY"
    gh release edit continuous --repo "$GITHUB_REPOSITORY" \
        --title "Latest EmuTOS Dreamcast build (${GITHUB_SHA:0:7})" \
        --notes-file "$notes" --latest
else
    gh release create continuous "${paths[@]}" --repo "$GITHUB_REPOSITORY" \
        --verify-tag --title "Latest EmuTOS Dreamcast build (${GITHUB_SHA:0:7})" \
        --notes-file "$notes" --latest
fi
