# Local VDR image_next services for SuiteOSD (v1.0)

image_next remains the single owner of photo source configuration,
directory discovery, thumbnail decoding and fullscreen/slideshow playback.

The shared header `image-next-service-v1.h` is copied verbatim in
`hotzenplotz5/vdr-plugin-image-nex` and
`hotzenplotz5/vdr-plugin-suiteosd`; keep them synchronized.

| VDR Service ID | Data | Behavior |
| --- | --- | --- |
| `ImageNext-Browse-v1.0` | `imagenext::Browse` | `source=-1`: configured source names; else directory entries |
| `ImageNext-Thumbnail-v1.0` | `imagenext::Thumbnail` | Enqueue/read from existing 64 MiB asynchronous bitmap cache; `ready=false` while decoding |
| `ImageNext-Play-v1.0` | `imagenext::Play` | Start existing image-next `cImageControl` with chosen photo and slideshow flag |

Clients use `cPluginManager::GetPlugin("image_next")` and check
`Service(..., request)` plus the request's explicit `success`/`ready`/
`started` status flags.

All paths are relative to a configured `imagesources.conf` source, not
absolute arbitrary filesystem paths. The service rejects empty/invalid
source indices, traversal segments, NULs, and canonical paths escaping
the source root through symlinks. Source discovery is read-only. The
service returns no raw `cFileSource*`, `cDirItem*` or decoder pointers.

The service makes no changes to source files. It does not generate
`folder.jpg`, and does not mount unavailable media automatically.
It returns readable `folder.jpg` as a cover when already present.

The plugin supports the historical `Image-ShowImage-v1.0` service
unchanged for backward compatibility. The new scoped interface should
be used for SuiteOSD navigation.

Build `WITH_SKINDESIGNER=0` or `=1` as desired; these native VDR
services do not depend on Skindesigner or public API v1.

This interface has been implemented in source. VDR 2.8.4 compilation,
inter-plugin playback handoff and television presentation must all pass
their respective validation before a release tag.
