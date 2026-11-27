# Changelog

This is the development changelog of `vdr-plugin-image-nex`. The original
plugin's full historical record remains in [HISTORY](HISTORY).

This fork uses [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
headings where useful. The single source for the build version is [VERSION](VERSION).
Unreleased work stays in the existing development branch and is not a release.

## [Unreleased]

No additional changes since the release-candidate version update.

## [0.7.0-rc1] — 2026-11-25 (candidate only, not tagged or published)

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
- Ubuntu 24.04 native build and Debian package content checks passed CI.
- The official VDR 2.8.4 release-source build passed CI, both without
  Skindesigner and with the Skindesigner API compiled against VDR 2.8.4.
- Runtime output on a real VDR is not yet verified. This candidate exists
  only as source on the single development branch, without a Git tag.

## [0.6.1] — 2026-05-01

Historical development baseline; see HISTORY for the original changes.

## Release policy

The source version **0.7.0-rc1** is a pre-release candidate. It may be
tagged or published only after approval and validation. Stable **0.7.0**
requires real VDR OSD/slideshow and compatibility acceptance tests.
No release tag is created solely for passing source-contract checks.
