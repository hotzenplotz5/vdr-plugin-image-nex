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
#include <map>
#include <string>
#include <list>

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

static cImage* LoadThumbnail(const char* path, int maxWidth, int maxHeight, bool fastOnly = false) {
    uint64_t tStart = cTimeMs::Now();
    esyslog("imageplugin: ---> Start loading thumbnail: %s", path);

    char tempThumbPath[256];
    bool useTempThumb = false;
    const char* loadPath = path;

#ifdef HAVE_LIBEXIF
    // EXIF Thumbnails are instantly loaded compared to 24 Megapixel JPEGs
    const char *ext = strrchr(path, '.');
    if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)) {
        snprintf(tempThumbPath, sizeof(tempThumbPath), "/tmp/vdr_thumb_%u_%p.jpg", (unsigned int)getpid(), path);
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
    avcodec_parameters_to_context(codec_ctx, codecpar);

    // Speed up decoding for full JPEGs by rendering at lower resolution (1/8)
    if (codec_ctx->codec_id == AV_CODEC_ID_MJPEG) {
        codec_ctx->lowres = codec->max_lowres < 3 ? codec->max_lowres : 3; 
    }

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        if (useTempThumb) unlink(tempThumbPath);
        return nullptr;
    }

    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    bool decoded = false;

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            avcodec_send_packet(codec_ctx, pkt);
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

    cImage* retImage = nullptr;
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

        retImage = new cImage(cSize(newWidth, newHeight));
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
};

static bool g_ThumbnailsUpdated = false;
static bool g_NeedsRedraw = true;

// Hintergrund-Thread: Lädt langsame JPEGs ruckelfrei im Hintergrund!
class cThumbLoaderThread : public cThread {
private:
    std::list<ThumbRequest> queue;
    cMutex queueMutex;
    cCondVar cond;
public:
    cThumbLoaderThread() : cThread("ImageThumbLoader") {}
    void Add(const std::string& path, int w, int h);
    void Clear();
    void StopThread();
    virtual void Action();
};

static cThumbLoaderThread* ThumbLoader = nullptr;

class cThumbCache {
private:
    static const size_t MAX_CACHE_SIZE = 100;
    static std::list<std::string> lruList;
public:
    static std::map<std::string, std::unique_ptr<cImage>> Cache;
    static cImage* Get(const char* path, int maxWidth, int maxHeight) {
        char keyBuf[1024];
        snprintf(keyBuf, sizeof(keyBuf), "%s_%dx%d", path, maxWidth, maxHeight);
        std::string key = keyBuf;
        
        {
            cMutexLock lock(&ThumbCacheMutex);
            if (Cache.find(key) != Cache.end()) {
                lruList.remove(key);
                lruList.push_front(key);
                return Cache[key].get();
            }
        }
        
        bool hasExif = false;
        const char *ext = strrchr(path, '.');
        if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)) {
            hasExif = true;
        }
        
        if (hasExif) {
            cImage* thumb = LoadThumbnail(path, maxWidth, maxHeight, true);
            if (thumb) {
                cMutexLock lock(&ThumbCacheMutex);
                Cache[key] = std::unique_ptr<cImage>(thumb);
                lruList.push_front(key);
                if (Cache.size() > MAX_CACHE_SIZE) {
                    std::string last = lruList.back();
                    lruList.pop_back();
                    Cache.erase(last);
                }
                return thumb;
            }
        }
        
        if (!ThumbLoader) ThumbLoader = new cThumbLoaderThread();
        ThumbLoader->Add(path, maxWidth, maxHeight);
        return nullptr;
    }
    static void Clear() {
        if (ThumbLoader) ThumbLoader->Clear();
        cMutexLock lock(&ThumbCacheMutex);
        Cache.clear();
        lruList.clear();
    }
};
std::list<std::string> cThumbCache::lruList;
std::map<std::string, std::unique_ptr<cImage>> cThumbCache::Cache;

void cThumbLoaderThread::Add(const std::string& path, int w, int h) {
    cMutexLock lock(&queueMutex);
    for (auto const& req : queue) if (req.path == path) return;
    queue.push_back({path, w, h});
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

        cImage* img = LoadThumbnail(req.path.c_str(), req.w, req.h, false);
        if (img) {
            char keyBuf[1024];
            snprintf(keyBuf, sizeof(keyBuf), "%s_%dx%d", req.path.c_str(), req.w, req.h);
            {
                cMutexLock cacheLock(&ThumbCacheMutex);
                cThumbCache::Cache[keyBuf] = std::unique_ptr<cImage>(img);
            }
            g_ThumbnailsUpdated = true;
            cRemote::Put(kNone); // Force VDR to trigger ProcessKey and refresh OSD
        }
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
    g_NeedsRedraw = true;

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
            g_NeedsRedraw = true;
            return; // Hardware Layer blockiert
        }
    }

    if (myOsd && g_NeedsRedraw) {
        DrawGrid();
        myOsd->Flush();
        g_NeedsRedraw = false;
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
        // Auto-calculation based on resolution
        columns = (osdWidth >= 1920) ? 6 : 4;
        if (osdWidth >= 3840) columns = 8; // 4K Support
    }

    int margin = 50;
    int padding = 20;
    int kachelBreite = (osdWidth - (2 * margin) - ((columns - 1) * padding)) / columns;
    if (kachelBreite < 10) kachelBreite = 10; // Prevent negative/zero sizes on exotic skins
    int kachelHoehe = kachelBreite * 3 / 4;

    int totalItems = list->Count();
    const cFont *font = cFont::GetFont(fontOsd);
    int titleHeight = font->Height() * 2 + 30;  // Genug Platz für große Skin-Header lassen
    int buttonAreaHeight = font->Height() + 60; // Genug Platz für Skin-Buttons lassen

    // Sichere, fest definierte Farben verwenden, da VDR-Theme-Variablen versionsabhängig sind!
    tColor bgFull = 0xFF151515;   // Edles Dunkelgrau für den Hintergrund
    tColor textFg = 0xFF00AAFF;   // Hellblau für den Titel
    tColor btnRed = 0xFFCC0000;   // Klassisches Rot für die Taste
    tColor btnBlue = 0xFF0000CC;  // Klassisches Blau für die Taste
    tColor btnFg = 0xFFFFFFFF;    // Weiß für die Tastenschrift

    myOsd->DrawRectangle(0, 0, osdWidth - 1, osdHeight - 1, bgFull);
    char titleBuf[256];
    snprintf(titleBuf, sizeof(titleBuf), "  %s - %s", tr("Image Grid"), currentdir ? currentdir : "/");
    myOsd->DrawText(margin, 10, titleBuf, textFg, bgFull, font);

    int btnY = osdHeight - buttonAreaHeight;
    myOsd->DrawRectangle(margin - 10, btnY + 5, margin + 150, btnY + 5 + font->Height() + 10, btnRed);
    myOsd->DrawText(margin, btnY + 10, tr("Select"), btnFg, btnRed, font);
    myOsd->DrawRectangle(margin + 190, btnY + 5, margin + 350, btnY + 5 + font->Height() + 10, btnBlue);
    myOsd->DrawText(margin + 200, btnY + 10, tr("Back"), btnFg, btnBlue, font);

    int visibleRows = (osdHeight - titleHeight - buttonAreaHeight) / (kachelHoehe + padding);
    if (visibleRows < 1) visibleRows = 1;
    int startRow = (currentIndex / columns / visibleRows) * visibleRows;

    for (int i = 0; i < totalItems; i++) {
        int row = (i / columns) - startRow;
        if (row < 0 || row >= visibleRows) continue;
        int col = i % columns;
        int x = margin + col * (kachelBreite + padding);
        int y = titleHeight + row * (kachelHoehe + padding);

        // Vollständig deckende Farben (0xFF...) erzwingen, um unsichtbare Kacheln durch Alpha-Blending-Fehler zu vermeiden!
        tColor bgColor = (i == currentIndex) ? 0xFF0055AA : 0xFF333333;
        tColor textColor = (i == currentIndex) ? 0xFFFFFFFF : 0xFFDDDDDD;

        // Deutliche Markierung für das ausgewählte Bild! (Dickerer, farbiger Rahmen)
        tColor borderColor = (i == currentIndex) ? 0xFFFFCC00 : 0xFFFFFFFF; // Gelb/Orange für Fokus, sonst Weiß
        int b = (i == currentIndex) ? 4 : 1; // 4 Pixel dick, wenn ausgewählt, sonst 1 Pixel
        myOsd->DrawRectangle(x - b, y - b, x + kachelBreite + b - 1, y + kachelHoehe + b - 1, borderColor);
        myOsd->DrawRectangle(x, y, x + kachelBreite - 1, y + kachelHoehe - 1, bgColor); // Kachel-Hintergrund zeichnen

        cDirItem *item = list->Get(i);
        if (item) {
            bool thumbDrawn = false;
            char *dirPath = item->Path();
            char *fullDirPath = source->BuildName(dirPath);
            char *thumbPath = NULL;

            if (item->Type == itDir) {
                thumbPath = AddPath(fullDirPath, "folder.jpg");
            } else if (item->Type == itFile) {
                thumbPath = strdup(fullDirPath);
            }
            if (thumbPath && !item->HasFolderJpg && access(thumbPath, R_OK) == 0) {
                item->HasFolderJpg = true;
            }
            if (thumbPath && item->HasFolderJpg) {
                cImage* thumb = cThumbCache::Get(thumbPath, kachelBreite, kachelHoehe);
                if (thumb) {
                    // Center the image in the tile
                    int thumbX = x + (kachelBreite - thumb->Width()) / 2;
                    int thumbY = y + (kachelHoehe - thumb->Height()) / 2;
                    myOsd->DrawImage(cPoint(thumbX, thumbY), *thumb);
                    thumbDrawn = true;
                }
            }

            if (thumbPath) free(thumbPath);
            free(fullDirPath);
            free(dirPath);

            // If no thumbnail was drawn, draw the text icon
            if (!thumbDrawn && (item->Type == itDir || item->Type == itParent)) {
                myOsd->DrawText(x + 5, y + 5, "[DIR]", textColor, bgColor, font);
            } else if (!thumbDrawn && item->Type == itFile) {
                myOsd->DrawText(x + 5, y + 5, "[IMG]", textColor, bgColor, font);
            }

            // Draw the name at the bottom with a semi-transparent bar
            int textBarHeight = font->Height() + 4;
            int textY = y + kachelHoehe - textBarHeight;
            if (textY < y) textY = y; // Ensure text bar does not bleed out of the tile on tiny resolutions
            tColor textBg = (i == currentIndex) ? 0xDD0055AA : 0xA0000000; // Blau für Fokus, sonst Schwarz
            myOsd->DrawRectangle(x, textY, x + kachelBreite - 1, y + kachelHoehe - 1, textBg);

            // Limit the drawing width to prevent long names from bleeding into adjacent grid tiles
            myOsd->DrawText(x + 5, textY + 2, item->DisplayName, textColor, textBg, font, kachelBreite - 10);
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

    int visibleRows = 1;
    if (myOsd) {
        int kachelBreite = (myOsd->Width() - 100 - ((columns - 1) * 20)) / columns;
        if (kachelBreite < 10) kachelBreite = 10;
        int kachelHoehe = kachelBreite * 3 / 4;
        const cFont *font = cFont::GetFont(fontOsd);
        int titleHeight = font->Height() * 2 + 30;
        int buttonAreaHeight = font->Height() + 60;
        visibleRows = (myOsd->Height() - titleHeight - buttonAreaHeight - 20) / (kachelHoehe + 20);
        if (visibleRows < 1) visibleRows = 1;
    }
    int pageItems = columns * visibleRows;

    switch (Key & ~k_Repeat) {
        case kNone:
            if (g_ThumbnailsUpdated || !myOsd || g_NeedsRedraw) {
                g_ThumbnailsUpdated = false;
                g_NeedsRedraw = true;
                Show();
            }
            return osContinue;
        case kChanUp:
            if (currentIndex + pageItems < totalItems) currentIndex += pageItems;
            else currentIndex = totalItems - 1;
            g_NeedsRedraw = true;
            Show();
            return osContinue;
        case kChanDn:
            if (currentIndex >= pageItems) currentIndex -= pageItems;
            else currentIndex = 0;
            g_NeedsRedraw = true;
            Show();
            return osContinue;
        case kRight:
            if (currentIndex < totalItems - 1) currentIndex++;
            else currentIndex = 0;
            g_NeedsRedraw = true;
            Show();
            return osContinue;
        case kLeft:
            if (currentIndex > 0) currentIndex--;
            else currentIndex = totalItems - 1;
            g_NeedsRedraw = true;
            Show();
            return osContinue;
        case kDown:
            if (currentIndex + columns < totalItems) {
                currentIndex += columns;
            } else if ((currentIndex / columns) < ((totalItems - 1) / columns)) {
                // Jump to the last item only if there is a row below us, preventing horizontal jumps in the last row
                currentIndex = totalItems - 1;
            }
            g_NeedsRedraw = true;
            Show();
            return osContinue;
        case kUp:
            if (currentIndex >= columns) currentIndex -= columns;
            g_NeedsRedraw = true;
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

// --- cMenuImageSkinItem ---------------------------------------------------

class cMenuImageSkinItem : public cOsdItem {
private:
    cDirItem *item;
public:
    cMenuImageSkinItem(cDirItem *Item);
    cDirItem *Item(void) { return item; }
};

cMenuImageSkinItem::cMenuImageSkinItem(cDirItem *Item) : cOsdItem("") {
    item = Item;
    char *dirPath = item->Path();
    char *fullDirPath = item->Source->BuildName(dirPath);
    char *thumbPath = NULL;
    
    if (item->Type == itDir || item->Type == itParent) {
        thumbPath = AddPath(fullDirPath, "folder.jpg");
    } else if (item->Type == itFile) {
        thumbPath = strdup(fullDirPath);
    }
    
    int is_dir = (item->Type == itDir || item->Type == itParent) ? 1 : 0;
    
    char *buffer = NULL;
    if (asprintf(&buffer, "%s\t%s\t%d", thumbPath ? thumbPath : "", item->DisplayName ? item->DisplayName : "", is_dir) >= 0) {
        SetText(buffer, false); // false = cOsdItem übernimmt die Kontrolle über diesen reservierten Speicher
    }

    if (thumbPath) free(thumbPath);
    free(fullDirPath);
    free(dirPath);
}

// --- cMenuImageSkin -------------------------------------------------------

cMenuImageSkin::cMenuImageSkin(cFileSource *Source)
: cOsdMenu(tr("Image Grid"))
{
    source = Source;
    list = new cDirList;
    currentdir = NULL;

    char *parent = NULL;
    source->GetRemember(currentdir, parent);

    LoadDir(currentdir);

    if (parent) {
        for (int i = 0; i < Count(); i++) {
            cMenuImageSkinItem *item = (cMenuImageSkinItem *)Get(i);
            if (item && item->Item() && item->Item()->Name && strcmp(item->Item()->Name, parent) == 0) {
                SetCurrent(item);
                break;
            }
        }
        free(parent);
    }
    SetHelp(tr("Select"), "", "", tr("Back"));
    Display();
}

cMenuImageSkin::~cMenuImageSkin()
{
    cDirItem *item = CurrentItem();
    if (item && source) source->SetRemember(currentdir, item->Name);

    delete list;
    free(currentdir);
}

bool cMenuImageSkin::LoadDir(const char *dir)
{
    Clear();
    bool res = list->Load(source, dir);
    for (int i = 0; i < list->Count(); i++) {
        cDirItem *item = list->Get(i);
        if (item) {
            Add(new cMenuImageSkinItem(item));
        }
    }
    return res;
}

cDirItem *cMenuImageSkin::CurrentItem()
{
    cMenuImageSkinItem *item = (cMenuImageSkinItem *)Get(Current());
    return item ? item->Item() : NULL;
}

eOSState cMenuImageSkin::ProcessKey(eKeys Key)
{
    eOSState state = cOsdMenu::ProcessKey(Key);

    if (state == osUnknown) {
        switch (Key) {
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
    }
    return state;
}

eOSState cMenuImageSkin::Parent(void)
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

        for (int i = 0; i < Count(); i++) {
            cMenuImageSkinItem *item = (cMenuImageSkinItem *)Get(i);
            if (item && item->Item() && item->Item()->Name && strcmp(item->Item()->Name, lastDirName) == 0) {
                SetCurrent(item);
                break;
            }
        }
        free(lastDirName);
        Display();
    } else {
        return osEnd;
    }
    return osContinue;
}

eOSState cMenuImageSkin::Select(bool isred)
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
        Display();
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

        g_NeedsRedraw = true;
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
        g_NeedsRedraw = true;
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
