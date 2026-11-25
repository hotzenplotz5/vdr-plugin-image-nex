# VDR image_next — release procedure

The plugin is developed on a single feature branch until accepted. Do not
create extra release branches, merge to `master`, tag or publish a package
before required CI evidence and approval.

## Single version source

Edit only `VERSION` for the compiled upstream/plugin version. The Makefile
reads that file and provides `IMAGE_PLUGIN_VERSION` to `image.c`.
Keep `debian/changelog` consistent when publishing:
- `0.6.3-dev` -> `0.6.3~dev-1` in Debian packaging.
- `0.7.0-rc1` -> `0.7.0~rc1-1` in Debian packaging.
- `0.7.0` -> `0.7.0-1` in Debian packaging.

The 0.7.0-rc1 version is currently set in VERSION after CI build validation.
No public release is implied by development version strings.

## Candidate acceptance

1. GitHub Action source-contract tests pass.
2. Native plugin compiles and links against VDR 2.8.4.
3. Skindesigner-enabled build compiles with the matching API package.
4. Debian/Ubuntu package builds cleanly and contains
   `libvdr-image_next.so.<APIVERSION>` and the bundled
   `plug-image_next-grid.xml` template under
   `/usr/share/vdr-plugin-image-next/skindesigner/`.
5. Verify `debian/copyright`, COPYING and original authorship notices.
6. Document all changes in CHANGELOG.md and HISTORY.

## On-device acceptance (required before 0.7.0)

With compatible VDR output hardware, check menu browsing, native grid,
Skindesigner grid with the active skin XML, direct true-color playback,
JPEG/PNG portrait/landscape, EXIF orientations, slideshow, zoom, jump mode,
red-key commands, fallback to MPEG-2, and switching skins.
Check VDR logs for renderer errors and verify that VDR shutdown leaves no
background thread or OSD active. These checks cannot be replaced by CI.

## Release

VERSION and debian/changelog currently describe 0.7.0-rc1. Confirm the CI
build on this exact candidate commit and verify the compiled VDR Version()
against VERSION. Obtain user approval and device validation before creating
a Git tag or publishing a package. Repeat acceptance checks for final
0.7.0. This repository's current PR is a draft.
