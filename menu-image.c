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
#include <memory>


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

    char *parent = NULL;
    source->GetRemember(currentdir, parent);

    LoadDir(currentdir);

    // Restore cursor position in Grid-View
    if (parent) {
        for (int i = 0; i < list->Count(); i++) {
            cDirItem *item = list->Get(i);
            if (item && item->Name && strcmp(item->Name, parent) == 0) {
                currentIndex = i;
                DrawGrid();
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
    bool res = list->Load(source, dir);

    Clear();
    DrawGrid();
    return res;
}

void cMenuImageGrid::Display(void)
{
    char titleBuf[256];
    snprintf(titleBuf, sizeof(titleBuf), "%s - %s", tr("Image Grid"), currentdir ? currentdir : "/");
    SetTitle(titleBuf); // Titel an VDR übergeben, BEVOR die Basisklasse ihn zeichnet

    // Die Basisklasse cOsdMenu zeichnet den Titel und die Hilfs-Buttons.
    cOsdMenu::Display();
}

void cMenuImageGrid::DrawGrid()
{
    int cols = 3;
    int colWidth = 22; // Feste Zeichenbreite pro Spalte
    Clear();
    int totalItems = list->Count();

    for (int row = 0; row < (totalItems + cols - 1) / cols; row++) {
        std::string rowText = "";
        for (int col = 0; col < cols; col++) {
            int idx = row * cols + col;
            if (idx < totalItems) {
                cDirItem *item = list->Get(idx);
                if (item) {
                    bool isSelected = (idx == currentIndex);
                    std::string name = item->DisplayName ? item->DisplayName : "";
                    if (item->Type == itDir || item->Type == itParent) {
                        name = "[" + name + "]";
                    }

                    // Kürze den Namen, falls er für die Spalte zu lang ist
                    if (name.length() > (size_t)(colWidth - 4)) {
                        name = name.substr(0, colWidth - 7) + "...";
                    }

                    char buffer[64];
                    if (isSelected) {
                        snprintf(buffer, sizeof(buffer), " >%-*s< ", colWidth - 4, name.c_str());
                    } else {
                        snprintf(buffer, sizeof(buffer), "  %-*s  ", colWidth - 4, name.c_str());
                    }
                    rowText += buffer;
                }
            }
            if (col < cols - 1) {
                rowText += "\t";
            }
        }
        Add(new cOsdItem(rowText.c_str()));
    }
    SetCurrent(Get(currentIndex / cols));
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

    int cols = 3;
    switch (Key & ~k_Repeat) {
        case kUp:
            if (currentIndex >= cols) {
                currentIndex -= cols;
                DrawGrid();
                Display();
            }
            return osContinue;
        case kDown:
            if (currentIndex + cols < totalItems) {
                currentIndex += cols;
            } else {
                currentIndex = totalItems - 1; // springe zum letzten Element
            }
            DrawGrid();
            Display();
            return osContinue;
        case kLeft:
            if (currentIndex > 0) {
                currentIndex--;
                DrawGrid();
                Display();
            }
            return osContinue;
        case kRight:
            if (currentIndex < totalItems - 1) {
                currentIndex++;
                DrawGrid();
                Display();
            }
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
                DrawGrid();
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
