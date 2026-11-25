/*
 * Stable VDR plugin service contract between image_next and suiteosd.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Both plugins must ship exactly the same copy of this header. Calls are
 * synchronous cPlugin::Service requests in the VDR process, not HTTP/API v1.
 * Each request owns its own memory; no pointers escape image_next.
 */
#ifndef VDR_IMAGE_NEXT_SERVICE_V1_H
#define VDR_IMAGE_NEXT_SERVICE_V1_H

#include <cstdint>
#include <string>
#include <vector>

namespace imagenext {

static const char kBrowse[] = "ImageNext-Browse-v1.0";
static const char kThumbnail[] = "ImageNext-Thumbnail-v1.0";
static const char kPlay[] = "ImageNext-Play-v1.0";

struct Source {
    int index = -1;
    std::string title;
};

struct Entry {
    std::string name;
    std::string path;       // path relative to the configured source root
    std::string cover;      // optional relative photo or folder.jpg
    bool directory = false;
};

struct Browse {
    int source = -1;        // -1: enumerate configured sources
    std::string directory;  // empty: source root
    std::vector<Source> sources;
    std::vector<Entry> entries;
    bool success = false;
    std::string error;
};

struct Thumbnail {
    int source = -1;
    std::string path;       // relative JPEG/PNG or folder.jpg path from Browse
    int maxWidth = 0;
    int maxHeight = 0;
    int width = 0;
    int height = 0;
    std::vector<uint32_t> argb; // tightly packed VDR ARGB tColor pixels
    bool ready = false;         // false: queued asynchronously, poll on kNone
};

struct Play {
    int source = -1;
    std::string path;       // relative file path returned by Browse
    bool slideshow = false; // true: begin advancing automatically
    bool started = false;
};

} // namespace imagenext

#endif // VDR_IMAGE_NEXT_SERVICE_V1_H
