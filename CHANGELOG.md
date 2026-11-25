# Changelog

This is the development changelog of `vdr-plugin-image-nex`. The original
plugin's full historical record remains in [HISTORY](HISTORY).

This fork uses [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
headings where useful. The single source for the build version is [VERSION](VERSION).
Unreleased work stays in the existing development branch and is not a release.

## [Unreleased] — 0.6.3-dev

### Added
- Optional full-screen 32-bit VDR OSD output without MPEG-2 re-encoding, with
  explicit fallback to the existing MPEG-2 still-picture player.
- Picture output setting: Automatic, True-color OSD, and Legacy MPEG-2.
- Skindesigner XML grid reference template, plus a native grid fallback.
- Automated source-contract tests and an Ubuntu native-build CI workflow.
- One canonical `VERSION` file and refreshed Debian/Ubuntu packaging.

### Changed
- Corrected Skindesigner normalized grid positions and plugin API version.
- Modernized the thumbnail loader with visible-page rendering, an LRU memory
  budget, file-change invalidation, and safer temporary files.
- Improved rotation and mirroring of photographs based on EXIF metadata.
- Avoided writing generated thumbnails into original photograph directories.
- Removed the implicit filesystem-root photo source.

### Verification
- The native build on Ubuntu 24.04 with packaged VDR 2.6.0 passed GitHub CI.
- True-color OSD runtime, the Skindesigner build and VDR 2.8.4 still require
  independent validation. Do not tag or publish a stable release on this basis.

## [0.6.1] — 2026-05-01

Historical development baseline; see HISTORY for the original changes.

## Release policy

The next planned release is **0.7.0-rc1**, but only after the VDR 2.8.4 and
Skindesigner builds pass and release acceptance checks are recorded.
Version **0.7.0** additionally requires a real VDR OSD/slideshow acceptance
test. No release tag is created solely for passing source-contract checks.
