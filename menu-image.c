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

#include "image.h"
#include "menu.h"
#include "data-image.h"
#include "menu-image.h"
#include "control-image.h"
#include <vdr/i18n.h>
#include <vdr/image.h>

#include <vdr/osd.h>
#include <vdr/font.h>
#include <vdr/status.h>


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
    char *full = source->BuildName(name);
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
			lastselect = NULL;
		}
  free(full);
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

    char *parent = NULL;
    source->GetRemember(currentdir, parent);
    free(parent);

    LoadDir(currentdir);
}

cMenuImageGrid::~cMenuImageGrid()
{
    cDirItem *item = CurrentItem();
    if (item && source) source->SetRemember(currentdir, item->Name);

    delete list;
    free(currentdir);
}

bool cMenuImageGrid::LoadDir(const char *dir)
{
    currentIndex = 0;
    return list->Load(source, dir);
}

void cMenuImageGrid::Display(void)
{
    // Die Basisklasse cOsdMenu zeichnet den Titel und die Hilfs-Buttons.
    cOsdMenu::Display();
    // Wir zeichnen unseren Kachel-Inhalt darüber.
    DrawGrid();
    // Wichtig: Die Änderungen auf dem Bildschirm sichtbar machen.
    if (osd)
       osd->Flush();
}

void cMenuImageGrid::DrawGrid()
{
    if (!osd) return; // 'osd' aus der Basisklasse cOsdMenu verwenden

    int osdWidth = OsdWidth();
    int osdHeight = OsdHeight();

    columns = (osdWidth > 1900) ? 6 : 4;
    if (osdWidth > 3000) columns = 8; // 4K Support

    // Den Menübereich mit der Hintergrundfarbe des Skins leeren
    osd->DrawRectangle(0, 0, osdWidth - 1, osdHeight - 1, Theme.Color(clrMenuBg));

    int margin = 50;
    int padding = 20;
    int kachelBreite = (osdWidth - (2 * margin) - ((columns - 1) * padding)) / columns;
    int kachelHoehe = kachelBreite * 3 / 4;

    char titleBuf[256];
    snprintf(titleBuf, sizeof(titleBuf), "%s - %s", tr("Image Grid"), currentdir ? currentdir : "/");
    SetTitle(titleBuf); // Titel an cOsdMenu übergeben, damit der Skin ihn zeichnet

    int totalItems = list->Count();
    const cFont *font = cFont::GetFont(fontMenu);
    int titleHeight = font->Height() + 20; // Ungefähre Höhe des Titelbereichs

    int visibleRows = (osdHeight - titleHeight - 50) / (kachelHoehe + padding); // 50px Platz für untere Buttons
    if (visibleRows < 1) visibleRows = 1;
    int startRow = (currentIndex / columns / visibleRows) * visibleRows;

    for (int i = 0; i < totalItems; i++) {
        int row = (i / columns) - startRow;
        if (row < 0 || row >= visibleRows) continue;

        int col = i % columns;
        int x = margin + col * (kachelBreite + padding);
        int y = titleHeight + row * (kachelHoehe + padding);

        tColor bgColor = (i == currentIndex) ? Theme.Color(clrMenuHighlight) : Theme.Color(clrMenuBg);
        tColor textColor = (i == currentIndex) ? Theme.Color(clrMenuHighlightFg) : Theme.Color(clrMenuFg);
        osd->DrawRectangle(x, y, x + kachelBreite - 1, y + kachelHoehe - 1, bgColor); // Draw tile background

        cDirItem *item = list->Get(i);
        if (item) {
            bool thumbDrawn = false;
            if (item->HasFolderJpg) {
                char *dirPath = item->Path();
                char *fullDirPath = source->BuildName(dirPath);
                char *thumbPath = AddPath(fullDirPath, "folder.jpg");

                cImage thumb;
                if (thumb.Load(thumbPath)) {
                    // Scale image to fit the tile, preserving aspect ratio
                    double aspect = (double)thumb.Height() / thumb.Width();
                    int newWidth = kachelBreite;
                    int newHeight = newWidth * aspect;
                    if (newHeight > kachelHoehe) {
                        newHeight = kachelHoehe;
                        newWidth = newHeight / aspect;
                    }
                    thumb.Scale(cSize(newWidth, newHeight));

                    // Center the image in the tile
                    int thumbX = x + (kachelBreite - newWidth) / 2;
                    int thumbY = y + (kachelHoehe - newHeight) / 2;

                    osd->DrawImage(thumbX, thumbY, thumb);
                    thumbDrawn = true;
                }

                free(thumbPath);
                free(fullDirPath);
                free(dirPath);
            }

            // If no thumbnail was drawn, draw the text icon
            if (!thumbDrawn && (item->Type == itDir || item->Type == itParent)) {
                osd->DrawText(x + 5, y + 5, "[DIR]", textColor, bgColor, font);
            }

            // Draw the name at the bottom with a semi-transparent bar
            int textBarHeight = font->Height() + 4;
            int textY = y + kachelHoehe - textBarHeight;
            tColor textBg = 0xA0000000; // Semi-transparent black
            osd->DrawRectangle(x, textY, x + kachelBreite - 1, y + kachelHoehe - 1, textBg);
            osd->DrawText(x + 5, textY + 2, item->Name, textColor, textBg, font);
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

    switch (Key & ~k_Repeat) {
        case kRight:
            if (currentIndex < totalItems - 1) currentIndex++;
            Display();
            return osContinue;
        case kLeft:
            if (currentIndex > 0) currentIndex--;
            Display();
            return osContinue;
        case kDown:
            if (currentIndex + columns < totalItems) currentIndex += columns;
            else currentIndex = totalItems - 1;
            Display();
            return osContinue;
        case kUp:
            if (currentIndex - columns >= 0) currentIndex -= columns;
            else currentIndex = 0;
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
        free(currentdir);
        currentdir = parentDir;
        LoadDir(currentdir);
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
        char *newdir = currentdir ? AddPath(currentdir, path) : strdup(path);
        free(currentdir);
        currentdir = newdir;
        free(path);
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
