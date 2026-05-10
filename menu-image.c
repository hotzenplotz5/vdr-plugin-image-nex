/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold    <vdr07 at deltab.de>
 * based on (C) 2003 Kai Tobias Burwieck <kai-at-burwieck.net>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <typeinfo>
#include <string>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <functional>
#include <algorithm>

#include "image.h"
#include "menu.h"
#include "data-image.h"
#include "menu-image.h"
#include "control-image.h"
#include <vdr/i18n.h>

#include <vdr/osd.h>
#include <vdr/font.h>
#include <vdr/status.h>
#include <vdr/themes.h>
#include <vdr/device.h>
#include "setup-image.h"
#include <vdr/remote.h>
#include <memory>

#ifdef HAVE_LIBEXIF
#include "exif.h"
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

static std::shared_ptr<cImage> LoadThumbnail(const char* path, int maxWidth, int maxHeight, bool fastOnly = false) {
    uint64_t tStart = cTimeMs::Now();
    esyslog("imageplugin: ---> Start loading thumbnail: %s", path);

    char tempThumbPath[256];
    bool useTempThumb = false;
    const char* loadPath = path;

#ifdef HAVE_LIBEXIF
    // EXIF Thumbnails are instantly loaded compared to 24 Megapixel JPEGs
    const char *ext = strrchr(path, '.');
    if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)) {
        std::hash<std::string> hasher;
        snprintf(tempThumbPath, sizeof(tempThumbPath), "/tmp/vdr_thumb_%u_%zu.jpg", (unsigned int)getpid(), hasher(path));
        if (ExtractExifThumbnail(path, tempThumbPath)) {
            loadPath = tempThumbPath;
            useTempThumb = true;
        }
    }
#endif

    if (fastOnly && !useTempThumb) {
        // Haupt-Thread Schutz: Wenn kein EXIF-Bild vorhanden ist, blocken wir das 
        // langsame FFmpeg-Dekodieren ab, um das VDR-OSD nicht einzufrieren!
        return nullptr;
    }

    AVFormatContext *fmt_ctx = nullptr;
    AVDictionary *opts = nullptr;
    // Sicherheitsnetz: Verhindert, dass FFmpeg ewig in großen JPEGs liest
    av_dict_set(&opts, "probesize", "32768", 0);
    av_dict_set(&opts, "analyzeduration", "1000000", 0);
    if (avformat_open_input(&fmt_ctx, loadPath, nullptr, &opts) < 0) {
        if (opts) av_dict_free(&opts);
        if (useTempThumb) unlink(tempThumbPath);
        return nullptr;
    }
    if (opts) av_dict_free(&opts);

    int video_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    
    if (video_stream_idx == -1 && avformat_find_stream_info(fmt_ctx, nullptr) >= 0) {
        for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
            if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream_idx = i;
                break;
            }
        }
    }

    if (video_stream_idx == -1) {
        avformat_close_input(&fmt_ctx); 
        if (useTempThumb) unlink(tempThumbPath);
        return nullptr; 
    }

    AVCodecParameters *codecpar = fmt_ctx->streams[video_stream_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) { 
        avformat_close_input(&fmt_ctx); 
        if (useTempThumb) unlink(tempThumbPath);
        return nullptr; 
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        avformat_close_input(&fmt_ctx);
        if (useTempThumb) unlink(tempThumbPath);
        return nullptr;
    }
    
    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        if (useTempThumb) unlink(tempThumbPath);
        return nullptr;
    }

    // Speed up decoding for full JPEGs by rendering at lower resolution (1/8)
    if (codec_ctx->codec_id == AV_CODEC_ID_MJPEG) {
        codec_ctx->lowres = std::min((int)codec->max_lowres, 3);
    }

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        if (useTempThumb) unlink(tempThumbPath);
        return nullptr;
    }

    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    if (!frame || !pkt) {
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        if (useTempThumb) unlink(tempThumbPath);
        return nullptr;
    }
    bool decoded = false;

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            if (avcodec_send_packet(codec_ctx, pkt) < 0) {
                av_packet_unref(pkt);
                break;
            }
            while (true) {
                int ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == 0) {
                    decoded = true;
                    break;
                }
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret < 0) {
                    break;
                }
            }
        }
        av_packet_unref(pkt);
        if (decoded) break;
    }

    // FFmpeg Decoder Flush: Zwingend nötig, sonst gibt FFmpeg das decodierte Bild nicht heraus!
    if (!decoded) {
        avcodec_send_packet(codec_ctx, nullptr);
        while (true) {
            int ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == 0) {
                decoded = true;
                break;
            }
            if (ret == AVERROR_EOF || ret < 0) {
                break;
            }
        }
    }
    av_packet_free(&pkt);

    std::shared_ptr<cImage> retImage = nullptr;
    if (decoded && frame->width > 0 && frame->height > 0) {
        double aspect = (double)frame->height / frame->width;
        int newWidth = maxWidth;
        int newHeight = newWidth * aspect;
        if (newHeight > maxHeight) {
            newHeight = maxHeight;
            if (aspect > 0.0) newWidth = newHeight / aspect;
        }
        if (newWidth <= 0) newWidth = 1;
        if (newHeight <= 0) newHeight = 1;

        esyslog("imageplugin: Decoding finished, scaling to %dx%d...", newWidth, newHeight);

        retImage = std::make_shared<cImage>(cSize(newWidth, newHeight));
        if (retImage && retImage->Data()) {
            SwsContext *sws_ctx = sws_getContext(
                frame->width, frame->height, (AVPixelFormat)frame->format,
                newWidth, newHeight, AV_PIX_FMT_BGRA,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );

            if (sws_ctx) {
                uint8_t *dest[4] = { (uint8_t*)const_cast<tColor*>(retImage->Data()), nullptr, nullptr, nullptr };
                int dest_linesize[4] = { newWidth * 4, 0, 0, 0 };
                sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, dest, dest_linesize);
                sws_freeContext(sws_ctx);
            }
        }
    }

    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    if (useTempThumb) unlink(tempThumbPath);
    esyslog("imageplugin: <--- Finished thumbnail: %s (took %llu ms)", path, (unsigned long long)(cTimeMs::Now() - tStart));
    return retImage;
}

// Globaler Mutex für den Cache
static cMutex ThumbCacheMutex;

struct ThumbRequest {
    std::string path;
    int w, h;
    uint64_t generation;
};

static std::atomic<bool> g_ThumbnailsUpdated(false);
static std::atomic<bool> g_NeedsRedraw(true);
static std::atomic<uint64_t> g_CacheGeneration(0);

struct CacheEntry {
    std::shared_ptr<cImage> image;
    std::list<std::string>::iterator lruIt;
};

// Hintergrund-Thread: Lädt langsame JPEGs ruckelfrei im Hintergrund!
class cThumbLoaderThread : public cThread {
private:
    std::list<ThumbRequest> queue;
    cMutex queueMutex;
    cCondVar cond;
public:
    cThumbLoaderThread() : cThread("ImageThumbLoader") {}
    void Add(const std::string& path, int w, int h, uint64_t generation);
    void Clear();
    void StopThread();
    virtual void Action();
};

static cThumbLoaderThread* ThumbLoader = nullptr;

class cThumbCache {
public:
    static const size_t MAX_CACHE_SIZE = 100;
    static std::list<std::string> lruList;
    static std::unordered_map<std::string, CacheEntry> Cache;
    static std::unordered_set<std::string> Loading;

    static std::shared_ptr<cImage> Get(const char* path, int maxWidth, int maxHeight) {
        char keyBuf[1024];
        snprintf(keyBuf, sizeof(keyBuf), "%s_%dx%d", path, maxWidth, maxHeight);
        std::string key = keyBuf;
        
        {
            cMutexLock lock(&ThumbCacheMutex);
            auto it = Cache.find(key);
            if (it != Cache.end()) {
                // Fast O(1) LRU update
                lruList.splice(lruList.begin(), lruList, it->second.lruIt);
                return it->second.image;
            }
            
            if (Loading.find(key) == Loading.end()) {
                Loading.insert(key);
                if (!ThumbLoader) ThumbLoader = new cThumbLoaderThread();
                ThumbLoader->Add(path, maxWidth, maxHeight, g_CacheGeneration.load());
            }
        }
        
        return nullptr;
    }
    static void Clear() {
        g_CacheGeneration++; // Invalidate pending requests
        if (ThumbLoader) ThumbLoader->Clear();
        cMutexLock lock(&ThumbCacheMutex);
        Cache.clear();
        lruList.clear();
        Loading.clear();
    }
};
std::list<std::string> cThumbCache::lruList;
std::unordered_map<std::string, CacheEntry> cThumbCache::Cache;
std::unordered_set<std::string> cThumbCache::Loading;

void cThumbLoaderThread::Add(const std::string& path, int w, int h, uint64_t generation) {
    cMutexLock lock(&queueMutex);
    for (auto const& req : queue) {
        if (req.path == path && req.w == w && req.h == h) return;
    }
    queue.push_back({path, w, h, generation});
    cond.Broadcast();
    if (!Active()) Start();
}

void cThumbLoaderThread::Clear() {
    cMutexLock lock(&queueMutex);
    queue.clear();
}

void cThumbLoaderThread::StopThread() {
    cond.Broadcast();
    Cancel(3);
}

void StopThumbLoader() {
    if (ThumbLoader) {
        ThumbLoader->StopThread();
        delete ThumbLoader;
        ThumbLoader = nullptr;
    }
}

void cThumbLoaderThread::Action() {
    while (Running()) {
        ThumbRequest req;
        {
            cMutexLock lock(&queueMutex);
            if (queue.empty()) {
                cond.TimedWait(queueMutex, 100);
                continue;
            }
            req = queue.front();
            queue.pop_front();
        }
        if (!Running()) break;

        std::shared_ptr<cImage> img = nullptr;
        if (req.generation == g_CacheGeneration.load()) {
            img = LoadThumbnail(req.path.c_str(), req.w, req.h, false);
        }

        // Frühzeitiger Abbruch bei Generation-Mismatch (verhindert stale cache entries)
        if (req.generation != g_CacheGeneration.load()) {
            cCondWait::SleepMs(5);
            continue;
        }

        char keyBuf[1024];
        snprintf(keyBuf, sizeof(keyBuf), "%s_%dx%d", req.path.c_str(), req.w, req.h);
        std::string key = keyBuf;
        
        {
            cMutexLock cacheLock(&ThumbCacheMutex);
            cThumbCache::Loading.erase(key);
            
            if (img) {
                if (cThumbCache::Cache.find(key) == cThumbCache::Cache.end()) {
                    cThumbCache::lruList.push_front(key);
                    if (cThumbCache::lruList.size() > cThumbCache::MAX_CACHE_SIZE) {
                        cThumbCache::Cache.erase(cThumbCache::lruList.back());
                        cThumbCache::lruList.pop_back();
                    }
                    cThumbCache::Cache[key] = { img, cThumbCache::lruList.begin() };
                }
                g_ThumbnailsUpdated.store(true);
            }
        }
        
        // Throttling yield to prevent pinning the CPU on big collections
        cCondWait::SleepMs(5);
    }
}

// --- cMenuImageBrowse ---------------------------------------------------------

cMenuImageBrowse::cMenuImageBrowse(void)
: cMenuBrowse(ImageSources.GetSource(), true,tr("Image browser"))
{
  sourcing = false;
  SetButtons();
}

void cMenuImageBrowse::SetButtons(void)
{
  SetHelp(tr("Play"), 0, tr("Data medium"), currentdir ? tr("Parent") : 0);
  Display();
}

eOSState cMenuImageBrowse::Source(bool second)
{
  if(HasSubMenu())
  	return osContinue;

  if(!second) {
    sourcing = true;
    return AddSubMenu(new
          cMenuSource(&ImageSources, tr("Image source")));
  }
  sourcing = false;
  cFileSource *src = cMenuSource::GetSelected();
  if(src) {
    ImageSources.SetSource(src);
    SetSource(src);
    NewDir(0);
  }
  return osContinue;
}

eOSState cMenuImageBrowse::ProcessKey(eKeys Key)
{
  eOSState state = cMenuBrowse::ProcessKey(Key);

  if(!HasSubMenu() && state == osContinue) {	
    // eval the return value from submenus
    if(sourcing)
      return Source(true);
	}

  if(state == osBack && lastselect) {
    char *name = lastselect->Path();
    cDirItem *item = cMenuBrowse::GetSelected();
    if(item) {
    
      //FIXME use a nonblocking way
      //OSD_InfoMsg(tr("Building slide show..."));
    
      cSlideShow *newss = new cSlideShow(item);
      if(newss->Load() && newss->Count()) {

        cImageControl::SetSlideShow(newss);
        state = osEnd;
      } 
			else {
				OSD_ErrorMsg(tr("No files!"));
				delete newss;
				state = osContinue;
			}
		}
    delete lastselect;
    lastselect = nullptr;
  free(name);
  }
  if(state == osUnknown && Key == kYellow)
    return Source(false);
  return state;
}

// --- cMenuImageGrid ---------------------------------------------------------

cMenuImageGrid::cMenuImageGrid(cFileSource *Source)
: cOsdObject(true)
{
    source = Source;
    list = new cDirList;
    currentIndex = 0;
    currentdir = NULL;
    myOsd = NULL;
    g_NeedsRedraw.store(true);

    char *parent = NULL;
    source->GetRemember(currentdir, parent);

    LoadDir(currentdir);

    // Restore cursor position in Grid-View
    if (parent) {
        for (int i = 0; i < list->Count(); i++) {
            cDirItem *item = list->Get(i);
            if (item && item->Name && strcmp(item->Name, parent) == 0) {
                currentIndex = i;
                break;
            }
        }
        free(parent);
    }
}

cMenuImageGrid::~cMenuImageGrid()
{
    cDirItem *item = CurrentItem();
    if (item && source) source->SetRemember(currentdir, item->Name);

    delete list;
    free(currentdir);
    if (myOsd) {
        delete myOsd;
        myOsd = NULL;
    }

    // Thumbnail-Cache leeren, um ein unbegrenztes Anwachsen des RAMs zu verhindern
    cThumbCache::Clear();
#ifdef HAVE_LIBEXIF
    ClearExifExtractorTasks();
#endif
}

bool cMenuImageGrid::LoadDir(const char *dir)
{
#ifdef HAVE_LIBEXIF
    ClearExifExtractorTasks();
#endif
    currentIndex = 0;
    return list->Load(source, dir);
}

void cMenuImageGrid::Show(void)
{
    if (!myOsd) {
        int left = cOsd::OsdLeft();
        int top = cOsd::OsdTop();
        int osdWidth = cOsd::OsdWidth();
        int osdHeight = cOsd::OsdHeight();

        if (osdWidth <= 0 || osdHeight <= 0) {
            osdWidth = 1920; 
            osdHeight = 1080;
        }

        // Exklusives Level 0 anfordern, da Hardware keine Overlays unterstützt!
        myOsd = cOsdProvider::NewOsd(left, top, 0);
        if (myOsd) {
            tArea Area = { 0, 0, osdWidth - 1, osdHeight - 1, 32 };
            if (myOsd->SetAreas(&Area, 1) != oeOk) {
                delete myOsd;
                myOsd = NULL;
            }
        }

        if (!myOsd) {
            g_NeedsRedraw.store(true);
            return; // Hardware Layer blockiert
        }
    }

    if (myOsd && g_NeedsRedraw.load()) {
        DrawGrid();
        myOsd->Flush();
        g_NeedsRedraw.store(false);
    }
}

void cMenuImageGrid::DrawGrid()
{
    if (!myOsd) return;
    int osdWidth = myOsd->Width();
    int osdHeight = myOsd->Height();

    int columns = 4;
    if (ImageSetup.m_nGridColumns > 0) {
        columns = ImageSetup.m_nGridColumns;
    } else {
        columns = (osdWidth >= 1920) ? 6 : 4;
        if (osdWidth >= 3840) columns = 8;
    }

    int margin = 50;
    int padding = 20;
    int kachelBreite = (osdWidth - (2 * margin) - ((columns - 1) * padding)) / columns;
    if (kachelBreite < 10) kachelBreite = 10;
    int kachelHoehe = kachelBreite * 3 / 4;

    int totalItems = list->Count();
    
    tColor bgFull     = 0xFF151515;
    tColor textFg     = 0xFF00AAFF;
    tColor btnRed     = 0xFFCC0000;
    tColor btnRedFg   = 0xFFFFFFFF;
    tColor btnBlue    = 0xFF0000CC;
    tColor btnBlueFg  = 0xFFFFFFFF;
    tColor itemBg     = 0xFF333333;
    tColor itemFg     = 0xFFDDDDDD;
    tColor cursorBg   = 0xFF0055AA;
    tColor cursorFg   = 0xFFFFFFFF;
    tColor borderSel  = 0xFFFFCC00;
    tColor borderNorm = 0xFFFFFFFF;

    const cFont *fntTitle = cFont::GetFont(fontOsd);
    const cFont *fntSml   = cFont::GetFont(fontSml);

    int titleHeight = fntTitle->Height() * 2 + 30;
    int buttonAreaHeight = fntTitle->Height() + 60;
    
    int visibleRows = (osdHeight - titleHeight - buttonAreaHeight) / (kachelHoehe + padding);
    if (visibleRows < 1) visibleRows = 1;
    int startRow = (currentIndex / columns / visibleRows) * visibleRows;

    myOsd->DrawRectangle(0, 0, osdWidth - 1, osdHeight - 1, bgFull);
    char titleBuf[256];
    snprintf(titleBuf, sizeof(titleBuf), "  %s - %s", tr("Image Grid"), currentdir ? currentdir : "/");
    myOsd->DrawText(margin, 10, titleBuf, textFg, bgFull, fntTitle);

    int btnY = osdHeight - buttonAreaHeight;
    myOsd->DrawRectangle(margin - 10, btnY + 5, margin + 150, btnY + 5 + fntTitle->Height() + 10, btnRed);
    myOsd->DrawText(margin, btnY + 10, tr("Select"), btnRedFg, btnRed, fntTitle);
    myOsd->DrawRectangle(margin + 190, btnY + 5, margin + 350, btnY + 5 + fntTitle->Height() + 10, btnBlue);
    myOsd->DrawText(margin + 200, btnY + 10, tr("Back"), btnBlueFg, btnBlue, fntTitle);

    for (int i = 0; i < totalItems; i++) {
        int row = (i / columns) - startRow;
        if (row < 0 || row >= visibleRows) continue;

        int col = i % columns;
        int x = margin + col * (kachelBreite + padding);
        int y = titleHeight + row * (kachelHoehe + padding);
        
        tColor bgColor = (i == currentIndex) ? cursorBg : itemBg;
        tColor textColor = (i == currentIndex) ? cursorFg : itemFg;

        int b = (i == currentIndex) ? 4 : 1; 
        tColor actBorderColor = (i == currentIndex) ? borderSel : borderNorm;
        
        myOsd->DrawRectangle(x - b, y - b, x + kachelBreite + b - 1, y + kachelHoehe + b - 1, actBorderColor);
        myOsd->DrawRectangle(x, y, x + kachelBreite - 1, y + kachelHoehe - 1, bgColor); 

        cDirItem *item = list->Get(i);
        if (item) {
            bool thumbDrawn = false;
            char *dirPath = item->Path();
            char *fullDirPath = source->BuildName(dirPath);
            char *thumbPath = NULL;

            if (item->Type == itDir || item->Type == itParent) {
                thumbPath = AddPath(fullDirPath, "folder.jpg");
            } else if (item->Type == itFile) {
                thumbPath = strdup(fullDirPath);
            }

            if (item->Type == itFile && !item->HasFolderJpg) {
                item->HasFolderJpg = true;
            }
            
            if (thumbPath && item->HasFolderJpg) {
                std::shared_ptr<cImage> thumb = cThumbCache::Get(thumbPath, kachelBreite, kachelHoehe);
                if (thumb) {
                    int thumbX = x + (kachelBreite - thumb->Width()) / 2;
                    int thumbY = y + (kachelHoehe - thumb->Height()) / 2;
                    myOsd->DrawImage(cPoint(thumbX, thumbY), *thumb);
                    thumbDrawn = true;
                }
            }

            if (thumbPath) free(thumbPath);
            free(fullDirPath);
            free(dirPath);

            if (!thumbDrawn && (item->Type == itDir || item->Type == itParent)) {
                myOsd->DrawText(x + 5, y + 5, "[DIR]", textColor, bgColor, fntSml);
            } else if (!thumbDrawn && item->Type == itFile) {
                myOsd->DrawText(x + 5, y + 5, "[IMG]", textColor, bgColor, fntSml);
            }

            int textBarHeight = fntSml->Height() + 4;
            int textY = y + kachelHoehe - textBarHeight;
            if (textY < y) textY = y; 
            myOsd->DrawRectangle(x, textY, x + kachelBreite - 1, y + kachelHoehe - 1, bgColor); 

            myOsd->DrawText(x + 5, textY + 2, item->DisplayName, textColor, bgColor, fntSml, kachelBreite - 10);
        }
    }
}

cDirItem *cMenuImageGrid::CurrentItem()
{
    return list->Get(currentIndex);
}

eOSState cMenuImageGrid::ProcessKey(eKeys Key)
{
    int totalItems = list->Count();
    if (totalItems == 0) {
        if (Key == kBack || Key == kMenu) return osEnd;
        return osContinue;
    }

    int columns = 4;
    if (ImageSetup.m_nGridColumns > 0) {
        columns = ImageSetup.m_nGridColumns;
    } else {
        if (myOsd) {
            columns = (myOsd->Width() >= 1920) ? 6 : 4;
            if (myOsd->Width() >= 3840) columns = 8;
        }
    }
    
    int visibleRows = 3;
    if (myOsd) {
        int kachelBreite = (myOsd->Width() - 100 - ((columns - 1) * 20)) / columns;
        int kachelHoehe = kachelBreite * 3 / 4;
        const cFont *font = cFont::GetFont(fontOsd);
        int titleHeight = font->Height() * 2 + 30;
        int buttonAreaHeight = font->Height() + 60;
        visibleRows = (myOsd->Height() - titleHeight - buttonAreaHeight) / (kachelHoehe + 20);
        if (visibleRows < 1) visibleRows = 1;
    }
    int pageItems = columns * visibleRows;

    switch (Key & ~k_Repeat) {
        case kNone:
            if (g_ThumbnailsUpdated.exchange(false) || !myOsd || g_NeedsRedraw.load()) {
                g_NeedsRedraw.store(true);
                Show();
            }
            return osContinue;
        case kChanUp:
            if (currentIndex + pageItems < totalItems) currentIndex += pageItems;
            else currentIndex = totalItems - 1;
            g_NeedsRedraw.store(true);
            Show();
            return osContinue;
        case kChanDn:
            if (currentIndex >= pageItems) currentIndex -= pageItems;
            else currentIndex = 0;
            g_NeedsRedraw.store(true);
            Show();
            return osContinue;
        case kRight:
            if (currentIndex < totalItems - 1) currentIndex++;
            else currentIndex = 0;
            g_NeedsRedraw.store(true);
            Show();
            return osContinue;
        case kLeft:
            if (currentIndex > 0) currentIndex--;
            else currentIndex = totalItems - 1;
            g_NeedsRedraw.store(true);
            Show();
            return osContinue;
        case kDown:
            if (currentIndex + columns < totalItems) {
                currentIndex += columns;
            } else if ((currentIndex / columns) < ((totalItems - 1) / columns)) {
                currentIndex = totalItems - 1;
            }
            g_NeedsRedraw.store(true);
            Show();
            return osContinue;
        case kUp:
            if (currentIndex >= columns) currentIndex -= columns;
            else currentIndex = 0;
            g_NeedsRedraw.store(true);
            Show();
            return osContinue;
        case kOk:
        case kRed:
            return Select(Key == kRed);
        case kBlue:
            return Parent();
        case kBack:
        case kMenu:
            return osEnd;
        default: break;
    }
    return osContinue;
}

// --- cMenuImageSkinDesigner -----------------------------------------------

cMenuImageSkinDesigner::cMenuImageSkinDesigner(cFileSource *Source, skindesignerapi::cPluginStructure *plugStruct)
: skindesignerapi::cSkindesignerOsdObject(plugStruct)
{
    source = Source;
    list = new cDirList;
    currentdir = NULL;
    currentIndex = 0;
    needsRedraw = true;
    
    rootView = NULL;
    back = NULL;
    header = NULL;
    imagegrid = NULL;

    char *parent = NULL;
    source->GetRemember(currentdir, parent);

    LoadDir(currentdir);

    if (parent) {
        for (int i = 0; i < list->Count(); i++) {
            cDirItem *item = list->Get(i);
            if (item && item->Name && strcmp(item->Name, parent) == 0) {
                currentIndex = i;
                break;
            }
        }
        free(parent);
    }
}

cMenuImageSkinDesigner::~cMenuImageSkinDesigner()
{
    cDirItem *item = CurrentItem();
    if (item && source) source->SetRemember(currentdir, item->Name);

    if (back) delete back;
    if (header) delete header;
    if (imagegrid) delete imagegrid;

    delete list;
    free(currentdir);
}

bool cMenuImageSkinDesigner::LoadDir(const char *dir)
{
    currentIndex = 0;
    needsRedraw = true;
    return list->Load(source, dir);
}

cDirItem *cMenuImageSkinDesigner::CurrentItem()
{
    return list->Get(currentIndex);
}

void cMenuImageSkinDesigner::Show(void)
{
    if (!SkindesignerAvailable()) return;
    
    rootView = GetOsdView();
    if (!rootView) return;
    
    back = rootView->GetViewElement(0); // background
    header = rootView->GetViewElement(1); // header
    imagegrid = rootView->GetViewGrid(0); // imagegrid
    
    if (back) back->Display();

    rootView->Activate();
    
    if (needsRedraw) {
        Draw();
        needsRedraw = false;
    }
}

void cMenuImageSkinDesigner::Draw()
{
    if (!rootView) return;

    if (header) {
        header->ClearTokens();
        header->Clear();
        header->AddStringToken(0, "Bildergalerie");
        header->Display();
    }

    if (imagegrid) {
        imagegrid->Clear();

        int totalItems = list->Count();
        if (totalItems == 0) {
            rootView->Display();
            return;
        }

        int columns = ImageSetup.m_nGridColumns > 0 ? ImageSetup.m_nGridColumns : 5;
        int rows = 3; // 3 Zeilen pro Seite für Estuary
        int itemsPerPage = columns * rows;
        
        double itemWidth = 100.0 / columns;
        double itemHeight = 100.0 / rows;

        int page = currentIndex / itemsPerPage;
        int startIdx = page * itemsPerPage;
        int endIdx = startIdx + itemsPerPage;
        if (endIdx > totalItems) endIdx = totalItems;

        for (int i = startIdx; i < endIdx; i++) {
            cDirItem *item = list->Get(i);
            if (!item) continue;
            
            char *dirPath = item->Path();
            char *fullDirPath = source->BuildName(dirPath);
            char *thumbPath = NULL;
            
            if (item->Type == itDir || item->Type == itParent) {
                thumbPath = AddPath(fullDirPath, "folder.jpg");
            } else if (item->Type == itFile) {
                thumbPath = strdup(fullDirPath);
            }
            
            int is_dir = (item->Type == itDir || item->Type == itParent) ? 1 : 0;
            
            // Position der Kachel auf der aktuellen Seite berechnen
            int idxOnPage = i - startIdx;
            double x = (idxOnPage % columns) * itemWidth;
            double y = (idxOnPage / columns) * itemHeight;
            
            imagegrid->ClearTokens();
            imagegrid->AddStringToken(0, thumbPath ? thumbPath : "");
            imagegrid->AddStringToken(1, item->DisplayName ? item->DisplayName : "");
            imagegrid->AddIntToken(0, is_dir);
            imagegrid->AddIntToken(1, i == currentIndex ? 1 : 0);
            
            imagegrid->SetGrid(idxOnPage, x, y, itemWidth, itemHeight);
            
            if (thumbPath) free(thumbPath);
            free(fullDirPath);
            free(dirPath);
        }

        imagegrid->SetCurrent(currentIndex - startIdx, true);
        imagegrid->Display();
    }
    rootView->Display();
}

eOSState cMenuImageSkinDesigner::ProcessKey(eKeys Key)
{
    int totalItems = list->Count();
    if (totalItems == 0) {
        if (Key == kBack || Key == kMenu) return osEnd;
        return osContinue;
    }

    int columns = ImageSetup.m_nGridColumns > 0 ? ImageSetup.m_nGridColumns : 5;
    int rows = 3;
    int itemsPerPage = columns * rows;

    switch (Key & ~k_Repeat) {
        case kNone:
            if (g_ThumbnailsUpdated.exchange(false) || needsRedraw) {
                needsRedraw = false;
                Draw();
            }
            return osContinue;
        case kChanUp:
            if (currentIndex + itemsPerPage < totalItems) currentIndex += itemsPerPage;
            else currentIndex = totalItems - 1;
            needsRedraw = true;
            return osContinue;
        case kChanDn:
            if (currentIndex >= itemsPerPage) currentIndex -= itemsPerPage;
            else currentIndex = 0;
            needsRedraw = true;
            return osContinue;
        case kRight:
            if (currentIndex < totalItems - 1) currentIndex++;
            else currentIndex = 0;
            needsRedraw = true;
            return osContinue;
        case kLeft:
            if (currentIndex > 0) currentIndex--;
            else currentIndex = totalItems - 1;
            needsRedraw = true;
            return osContinue;
        case kDown:
            if (currentIndex + columns < totalItems) {
                currentIndex += columns;
            } else {
                currentIndex = totalItems - 1;
            }
            needsRedraw = true;
            return osContinue;
        case kUp:
            if (currentIndex >= columns) currentIndex -= columns;
            else currentIndex = 0;
            needsRedraw = true;
            return osContinue;
        case kOk:
        case kRed:
            return Select(Key == kRed);
        case kBlue:
            return Parent();
        case kBack:
        case kMenu:
            return osEnd;
        default: break;
    }
    return osContinue;
}

eOSState cMenuImageSkinDesigner::Parent(void)
{
    if (currentdir) {
        char *parentDir = NULL;
        char *ss = strrchr(currentdir, '/');
        if (ss) {
            *ss = 0;
            parentDir = strdup(currentdir);
        }
        char* lastDirName = ss ? strdup(ss + 1) : strdup(currentdir);

        free(currentdir);
        currentdir = parentDir;
        LoadDir(currentdir);

        for (int i = 0; i < list->Count(); i++) {
            cDirItem *item = list->Get(i);
            if (item && item->Name && strcmp(item->Name, lastDirName) == 0) {
                currentIndex = i;
                break;
            }
        }
        free(lastDirName);
        needsRedraw = true;
    } else {
        return osEnd;
    }
    return osContinue;
}

eOSState cMenuImageSkinDesigner::Select(bool isred)
{
    cDirItem *item = CurrentItem();
    if (!item) return osContinue;

    if (item->Type == itParent) {
        return Parent();
    } else if (item->Type == itDir) {
        char *path = item->Path();
        free(currentdir);
        currentdir = path;
        LoadDir(currentdir);
        needsRedraw = true;
        return osContinue;
    } else if (item->Type == itFile) {
        cSlideShow *newss = new cSlideShow(item);
        if (newss->Load() && newss->Count()) {
            cImageControl::SetSlideShow(newss);
            return osEnd;
        }
        delete newss;
        OSD_ErrorMsg(tr("No files!"));
    }
    return osContinue;
}

eOSState cMenuImageGrid::Parent(void)
{
    if (currentdir) {
        char *parentDir = NULL;
        char *ss = strrchr(currentdir, '/');
        if (ss) {
            *ss = 0;
            parentDir = strdup(currentdir);
        }
        // Remember the directory we just left to restore cursor position
        char* lastDirName = ss ? strdup(ss + 1) : strdup(currentdir);

        free(currentdir);
        currentdir = parentDir;
        LoadDir(currentdir);

        // Automatically place cursor on the folder we just exited
        for (int i = 0; i < list->Count(); i++) {
            cDirItem *item = list->Get(i);
            if (item && item->Name && strcmp(item->Name, lastDirName) == 0) {
                currentIndex = i;
                break;
            }
        }
        free(lastDirName);

        g_NeedsRedraw.store(true);
        Show();
    } else {
        return osEnd;
    }
    return osContinue;
}

eOSState cMenuImageGrid::Select(bool isred)
{
    cDirItem *item = CurrentItem();
    if (!item) return osContinue;

    if (item->Type == itParent) {
        return Parent();
    } else if (item->Type == itDir) {
        char *path = item->Path();
        free(currentdir);
        currentdir = path; // path already contains the fully resolved absolute directory string
        LoadDir(currentdir);
        g_NeedsRedraw.store(true);
        Show();
        return osContinue;
    } else if (item->Type == itFile) {
        cSlideShow *newss = new cSlideShow(item);
        if (newss->Load() && newss->Count()) {
            cImageControl::SetSlideShow(newss);
            return osEnd;
        }
        delete newss;
        OSD_ErrorMsg(tr("No files!"));
    }
    return osContinue;
}
