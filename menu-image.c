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
#include <memory>

#ifdef HAVE_LIBEXIF
#include "exif.h"
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

static cImage* LoadThumbnail(const char* path, int maxWidth, int maxHeight) {
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

    struct stat st;
    if (!useTempThumb && stat(loadPath, &st) == 0 && st.st_size > 5000000) {
        // Nur zur Sicherheit: Keine Dateien > 5MB im OSD-Hauptthread scannen, um Freezes zu vermeiden.
        if (useTempThumb) unlink(tempThumbPath);
        return nullptr;
    }

    AVFormatContext *fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, loadPath, nullptr, nullptr) < 0) {
        if (useTempThumb) unlink(tempThumbPath);
        return nullptr;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) { 
        avformat_close_input(&fmt_ctx); 
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
            if (avcodec_receive_frame(codec_ctx, frame) == 0) {
                decoded = true;
                break;
            }
        }
        av_packet_unref(pkt);
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

        retImage = new cImage(cSize(newWidth, newHeight));

        SwsContext *sws_ctx = sws_getContext(
            frame->width, frame->height, (AVPixelFormat)frame->format,
            newWidth, newHeight, AV_PIX_FMT_BGRA, // VDR erwartet intern BGRA Format für ARGB32
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        if (sws_ctx) {
            uint8_t *dest[4] = { (uint8_t*)retImage->Data(), nullptr, nullptr, nullptr };
            int dest_linesize[4] = { newWidth * 4, 0, 0, 0 };
            sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, dest, dest_linesize);
            sws_freeContext(sws_ctx);
        } else {
            delete retImage;
            retImage = nullptr;
        }
    }

    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    if (useTempThumb) unlink(tempThumbPath);
    return retImage;
}

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
        
        // Wenn gefunden, Key in der LRU-Liste ganz nach vorne schieben
        if (Cache.find(key) != Cache.end()) {
            lruList.remove(key);
            lruList.push_front(key);
            return Cache[key].get();
        }
        
        cImage* thumb = LoadThumbnail(path, maxWidth, maxHeight);
        if (thumb) {
            Cache[key] = std::unique_ptr<cImage>(thumb);
            lruList.push_front(key);
            
            // Cache-Größenlimit erzwingen
            if (Cache.size() > MAX_CACHE_SIZE) {
                std::string last = lruList.back();
                lruList.pop_back();
                Cache.erase(last);
            }
            return thumb;
        }
        
        
        // Do NOT cache nullptrs. If the background thread is currently writing the EXIF thumbnail,
        // a premature load will fail. By not caching the failure, the UI will automatically retry 
        // and succeed once the background thread finishes writing the file.
        return nullptr;
    }
    static void Clear() {
        Cache.clear();
        lruList.clear();
    }
};
std::list<std::string> cThumbCache::lruList;
std::map<std::string, std::unique_ptr<cImage>> cThumbCache::Cache;

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
: cOsdMenu("Image Grid")
{
    source = Source;
    list = new cDirList;
    currentIndex = 0;
    currentdir = NULL;
    myOsd = NULL;

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

void cMenuImageGrid::Display(void)
{
    char titleBuf[256];
    snprintf(titleBuf, sizeof(titleBuf), "%s - %s", tr("Image Grid"), currentdir ? currentdir : "/");
    SetTitle(titleBuf);
    SetHelp(tr("Select"), "", "", tr("Back"));

    cOsdMenu::Display();

    if (!myOsd) {
        // Level 1: Wir legen unsere Kacheln als 100% transparentes Overlay ÜBER das Skindesigner-Menü
        myOsd = cOsdProvider::NewOsd(cOsd::OsdLeft(), cOsd::OsdTop(), 1);
        if (myOsd) {
            tArea Area = { 0, 0, cOsd::OsdWidth() - 1, cOsd::OsdHeight() - 1, 32 };
            if (myOsd->SetAreas(&Area, 1) != oeOk) {
                Area.bpp = 8; // Fallback falls die Grafikkarte/das Ausgabe-Plugin kein 32-Bit unterstützt
                myOsd->SetAreas(&Area, 1);
            }
        }
    }
    if (myOsd) {
        DrawGrid();
        myOsd->Flush();
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
    int titleHeight = font->Height() + 20; // Ungefähre Höhe des Titelbereichs
    int buttonAreaHeight = 50; // Ungefährer Platz für Farbtasten unten

    // OSD komplett transparent machen, damit das Skindesigner Theme ungestört sichtbar bleibt!
    tColor bgClear = 0x00000000;
    myOsd->DrawRectangle(0, 0, osdWidth - 1, osdHeight - 1, bgClear);

    int visibleRows = (osdHeight - titleHeight - 50) / (kachelHoehe + padding); // 50px Platz für untere Buttons
    if (visibleRows < 1) visibleRows = 1;
    int startRow = (currentIndex / columns / visibleRows) * visibleRows;

    for (int i = 0; i < totalItems; i++) {
        int row = (i / columns) - startRow;
        if (row < 0 || row >= visibleRows) continue;
        int col = i % columns;
        int x = margin + col * (kachelBreite + padding);
        int y = titleHeight + row * (kachelHoehe + padding);

        tColor bgColor = (i == currentIndex) ? 0xCC0055AA : 0xAA222222;
        tColor textColor = (i == currentIndex) ? 0xFFFFFFFF : 0xFFDDDDDD;

        myOsd->DrawRectangle(x, y, x + kachelBreite - 1, y + kachelHoehe - 1, bgColor); // Draw tile background

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
            }

            // Draw the name at the bottom with a semi-transparent bar
            int textBarHeight = font->Height() + 4;
            int textY = y + kachelHoehe - textBarHeight;
            if (textY < y) textY = y; // Ensure text bar does not bleed out of the tile on tiny resolutions
            tColor textBg = 0xA0000000; // Semi-transparent black
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
        visibleRows = (myOsd->Height() - font->Height() - 70) / (kachelHoehe + 20);
        if (visibleRows < 1) visibleRows = 1;
    }
    int pageItems = columns * visibleRows;

    switch (Key & ~k_Repeat) {
        case kChanUp:
            if (currentIndex + pageItems < totalItems) currentIndex += pageItems;
            else currentIndex = totalItems - 1;
            Display();
            return osContinue;
        case kChanDn:
            if (currentIndex >= pageItems) currentIndex -= pageItems;
            else currentIndex = 0;
            Display();
            return osContinue;
        case kRight:
            if (currentIndex < totalItems - 1) currentIndex++;
            else currentIndex = 0;
            Display();
            return osContinue;
        case kLeft:
            if (currentIndex > 0) currentIndex--;
            else currentIndex = totalItems - 1;
            Display();
            return osContinue;
        case kDown:
            if (currentIndex + columns < totalItems) {
                currentIndex += columns;
            } else if ((currentIndex / columns) < ((totalItems - 1) / columns)) {
                // Jump to the last item only if there is a row below us, preventing horizontal jumps in the last row
                currentIndex = totalItems - 1;
            }
            Display();
            return osContinue;
        case kUp:
            if (currentIndex >= columns) currentIndex -= columns;
            Display();
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

        Display();
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
