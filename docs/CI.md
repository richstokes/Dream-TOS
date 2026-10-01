# Continuous Dreamcast downloads

The **Build Dreamcast release** GitHub Action builds every push to `main`,
pull requests targeting `main`, and manual runs. Successful main builds update
the rolling [`continuous` release](https://github.com/richstokes/Dream-TOS/releases/tag/continuous).
Pull requests build and test but never publish.

- [Download the latest CDI](https://github.com/richstokes/Dream-TOS/releases/download/continuous/DreamTOS.cdi)
- [Download the ELF](https://github.com/richstokes/Dream-TOS/releases/download/continuous/dream-tos.elf)
- [Checksums](https://github.com/richstokes/Dream-TOS/releases/download/continuous/SHA256SUMS)
- [Build provenance](https://github.com/richstokes/Dream-TOS/releases/download/continuous/BUILD-INFO.txt)

These URLs become available after the first successful main workflow run.
The release uses a fixed tag so download links continue working as the build
changes. GitHub may show the original release publication date; the title,
notes and BUILD-INFO.txt identify the current commit. Source archives follow
the updated tag. A failed build leaves the previous release in place.

CI uses the same pinned SH-4 compiler image and KallistiOS revision as
`richstokes/dreamcast-homebrew`. It builds KOS, runs the normal repository
build/CDI scripts and host tests, validates the ELF architecture, and checks
the download hashes before publishing. It does not run Flycast or verify
real hardware; see TESTING.md for runtime coverage.

The SDK and tools live under ignored `build/` directories. Compiler and SDK
pins are in `.github/workflows/release.yml`; the mkdcdisc pin remains in
`scripts/bootstrap-mkdcdisc.sh`. No repository secret is needed: only the
main-only publishing job receives the workflow token with `contents: write`.
Tested artifacts are also retained with each workflow run for 14 days.

To reproduce the CI build, use a fresh checkout with Docker, set KOS_COMMIT
and TOOLCHAIN_IMAGE to the workflow's values, and run:

```sh
docker run --rm --volume "$PWD:/workspace" --workdir /workspace \
  --env KOS_COMMIT "$TOOLCHAIN_IMAGE" sh .github/scripts/build-in-container.sh
```

The container writes `dist/` as root on Linux. Normal local builds should
continue to use `scripts/build-cdi.sh` and the locally installed SDK.
