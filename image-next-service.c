/*
 * Native VDR Image Next service for SuiteOSD (no HTTP and no duplicate player).
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The configured image sources remain authoritative. All requested paths
 * are relative to one of these roots and are canonicalized against it,
 * including following symlinks, before any file is listed or decoded.
 */
#include "image-next-service-v1.h"
#include "data.h"
#include "data-image.h"
#include "menu-image.h"
#include "control-image.h"

#include <vdr/osd.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

cFileSource *GetSource(int index)
{
    if (index < 0) return nullptr;
    int i = 0;
    for (cFileSource *src = ImageSources.First(); src; src = ImageSources.Next(src), ++i)
        if (i == index)
            return src;
    return nullptr;
}

bool SafeRelative(const std::string &path)
{
    if (path.size() > 4096 || (!path.empty() && path[0] == '/') ||
        path.find('\\') != std::string::npos || path.find('\0') != std::string::npos)
        return false;
    if (path.empty()) return true;
    size_t start = 0;
    while (start < path.size()) {
        const size_t end = path.find('/', start);
        const std::string segment = path.substr(start, end == std::string::npos ?
                                                std::string::npos : end - start);
        if (segment.empty() || segment == "." || segment == "..")
            return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

bool WithinSource(cFileSource *src, const std::string &relative,
                  std::string &absolute, bool directory)
{
    if (!src || !src->BaseDir() || !SafeRelative(relative))
        return false;
    char root[PATH_MAX] = {0};
    if (!realpath(src->BaseDir(), root))
        return false;
    char *path = src->BuildName(relative.empty() ? "." : relative.c_str());
    if (!path)
        return false;
    char resolved[PATH_MAX] = {0};
    const bool found = realpath(path, resolved) != nullptr;
    free(path);
    if (!found)
        return false;
    const std::string base(root);
    const std::string full(resolved);
    if (full != base && (full.size() <= base.size() ||
         full.compare(0, base.size(), base) != 0 ||
         (base != "/" && full[base.size()] != '/')))
        return false;
    struct stat st;
    if (stat(resolved, &st) != 0 ||
        (directory ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)))
        return false;
    absolute = full;
    return true;
}

bool IsPhoto(const std::string &relative)
{
    const size_t dot = relative.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = relative.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    static const char *valid[] = {".jpg", ".jpeg", ".jif", ".jiff",
        ".tif", ".tiff", ".gif", ".bmp", ".png", ".pnm", ".mps"};
    for (const char *candidate : valid)
        if (ext == candidate) return true;
    return false;
}

bool BrowseSource(imagenext::Browse &request)
{
    request.entries.clear();
    request.sources.clear();
    request.success = false;
    request.error.clear();

    if (request.source < 0) {
        int index = 0;
        for (cFileSource *src = ImageSources.First(); src;
             src = ImageSources.Next(src), ++index) {
            imagenext::Source entry;
            entry.index = index;
            entry.title = src->Description() && *src->Description()
                        ? src->Description() : "Bilder";
            request.sources.push_back(entry);
        }
        request.success = true;
        return true;
    }

    cFileSource *src = GetSource(request.source);
    std::string directory;
    if (!WithinSource(src, request.directory, directory, true)) {
        request.error = "Bildquelle oder Ordner nicht erreichbar";
        return true;
    }

    cDirList list;
    list.Load(src, request.directory.empty() ? nullptr : request.directory.c_str());
    for (cDirItem *item = list.First(); item; item = list.Next(item)) {
        if (item->Type != itDir && item->Type != itFile)
            continue; // SuiteOSD handles '..' via its own breadcrumb/Back.
        char *path = item->Path();
        if (!path) continue;
        const std::string relative(path);
        free(path);
        std::string absolute;
        const bool folder = item->Type == itDir;
        if (!WithinSource(src, relative, absolute, folder))
            continue;
        imagenext::Entry entry;
        entry.name = item->DisplayName ? item->DisplayName : item->Name;
        entry.path = relative;
        entry.directory = folder;
        if (folder) {
            std::string cover;
            const std::string candidate = relative + "/folder.jpg";
            if (WithinSource(src, candidate, cover, false))
                entry.cover = candidate;
        } else if (IsPhoto(relative)) {
            entry.cover = relative;
        } else {
            continue;
        }
        request.entries.push_back(std::move(entry));
    }
    request.success = true;
    return true;
}

bool LoadThumbnail(imagenext::Thumbnail &request)
{
    request.ready = false;
    request.width = request.height = 0;
    request.argb.clear();
    if (request.maxWidth < 1 || request.maxWidth > 512 ||
        request.maxHeight < 1 || request.maxHeight > 512 ||
        !IsPhoto(request.path))
        return true;
    std::string absolute;
    if (!WithinSource(GetSource(request.source), request.path, absolute, false))
        return true;
    std::shared_ptr<cImage> image =
        ImageNextCachedThumbnail(absolute.c_str(), request.maxWidth, request.maxHeight);
    if (!image || !image->Data() || image->Width() <= 0 || image->Height() <= 0)
        return true; // thumb loader is still working; caller polls on kNone
    const size_t pixels = static_cast<size_t>(image->Width()) * image->Height();
    request.argb.assign(image->Data(), image->Data() + pixels);
    request.width = image->Width();
    request.height = image->Height();
    request.ready = true;
    return true;
}

bool StartPlayback(imagenext::Play &request)
{
    request.started = false;
    std::string absolute;
    cFileSource *src = GetSource(request.source);
    if (!WithinSource(src, request.path, absolute, false) ||
        !IsPhoto(request.path))
        return true;

    const size_t slash = request.path.find_last_of('/');
    const std::string subdir = slash == std::string::npos ? "" : request.path.substr(0, slash);
    const std::string name = slash == std::string::npos ? request.path :
                              request.path.substr(slash + 1);
    cDirItem item(src, subdir.empty() ? nullptr : subdir.c_str(),
                  name.c_str(), itFile);
    cSlideShow *slideshow = new cSlideShow(&item);
    if (!slideshow->Load() || !slideshow->Count()) {
        delete slideshow;
        return true;
    }
    // Control stays owned by image_next, not SuiteOSD.
    cImageControl::SetSlideShow(slideshow, request.slideshow);
    request.started = true;
    return true;
}

} // namespace

bool HandleImageNextSuiteService(const char *id, void *data)
{
    if (!id) return false;
    if (!strcmp(id, imagenext::kBrowse))
        return data ? BrowseSource(*static_cast<imagenext::Browse*>(data)) : true;
    if (!strcmp(id, imagenext::kThumbnail))
        return data ? LoadThumbnail(*static_cast<imagenext::Thumbnail*>(data)) : true;
    if (!strcmp(id, imagenext::kPlay))
        return data ? StartPlayback(*static_cast<imagenext::Play*>(data)) : true;
    return false;
}
