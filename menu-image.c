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
    myOsd = NULL;
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
    if (myOsd) delete myOsd;
}

bool cMenuImageGrid::LoadDir(const char *dir)
{
    currentIndex = 0;
    return list->Load(source, dir);
}

void cMenuImageGrid::Display(void)
{
    if (!myOsd) {
        double aspect;
        cDevice::PrimaryDevice()->GetOsdSize(osdWidth, osdHeight, aspect);
        myOsd = cOsdProvider::NewOsd(0, 0);
        if (myOsd) {
            tArea Area = { 0, 0, osdWidth - 1, osdHeight - 1, 32 };
            myOsd->SetAreas(&Area, 1);
        }
    }
    columns = (osdWidth > 1900) ? 6 : 4;
    if (osdWidth > 3000) columns = 8; // 4K Support

    DrawGrid();
}

void cMenuImageGrid::DrawGrid()
{
    if (!myOsd) return;

    myOsd->DrawRectangle(0, 0, osdWidth - 1, osdHeight - 1, clrBackground);

    int margin = 50;
    int padding = 20;
    int kachelBreite = (osdWidth - (2 * margin) - ((columns - 1) * padding)) / columns;
    int kachelHoehe = kachelBreite * 3 / 4;

    int totalItems = list->Count();

    char titleBuf[256];
    snprintf(titleBuf, sizeof(titleBuf), "%s - %s", tr("Image Grid"), currentdir ? currentdir : "/");
    myOsd->DrawText(margin, 10, titleBuf, clrWhite, clrBackground, cFont::GetFont(fontOsd));

    int visibleRows = (osdHeight - 100) / (kachelHoehe + padding);
    if (visibleRows < 1) visibleRows = 1;
    int startRow = currentIndex / columns;
    if (startRow > visibleRows - 1) startRow = startRow - visibleRows + 1;
    else startRow = 0;

    for (int i = 0; i < totalItems; i++) {
        int row = (i / columns) - startRow;
        if (row < 0 || row >= visibleRows) continue;

        int col = i % columns;
        int x = margin + col * (kachelBreite + padding);
        int y = 80 + row * (kachelHoehe + padding);

        tColor bgColor = (i == currentIndex) ? clrYellow : clrBlue;
        tColor textColor = (i == currentIndex) ? clrBlack : clrWhite;
        myOsd->DrawRectangle(x, y, x + kachelBreite, y + kachelHoehe, bgColor);

        cDirItem *item = list->Get(i);
        if (item) {
            myOsd->DrawText(x + 5, y + kachelHoehe - 30, item->Name, textColor, bgColor, cFont::GetFont(fontSml));
            if (item->Type == itDir || item->Type == itParent) {
                myOsd->DrawText(x + 5, y + 5, "[DIR]", textColor, bgColor, cFont::GetFont(fontSml));
            } else if (item->HasFolderJpg) {
                myOsd->DrawText(x + 5, y + 5, "[IMG]", textColor, bgColor, cFont::GetFont(fontSml));
            }
        }
    }
    myOsd->Flush();
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
            DrawGrid();
            return osContinue;
        case kLeft:
            if (currentIndex > 0) currentIndex--;
            DrawGrid();
            return osContinue;
        case kDown:
            if (currentIndex + columns < totalItems) currentIndex += columns;
            else currentIndex = totalItems - 1;
            DrawGrid();
            return osContinue;
        case kUp:
            if (currentIndex - columns >= 0) currentIndex -= columns;
            else currentIndex = 0;
            DrawGrid();
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
        DrawGrid();
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
        DrawGrid();
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
