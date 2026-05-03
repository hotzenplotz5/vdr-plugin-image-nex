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
                SetCurrent(Get(i));
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
    for (int i = 0; i < list->Count(); i++) {
        cDirItem *item = list->Get(i);
        if (item) {
            char *buffer = 0;
            if (asprintf(&buffer, item->Type == itFile ? "%s" : "[%s]", item->DisplayName) > 0) {
                Add(new cOsdItem(buffer));
                free(buffer);
            }
        }
    }
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
    // OSD drawing is currently removed to fix compilation errors
}

cDirItem *cMenuImageGrid::CurrentItem()
{
    currentIndex = Current();
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
        case kUp:
        case kDown:
        case kLeft:
        case kRight:
            return cOsdMenu::ProcessKey(Key);
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
                SetCurrent(Get(i));
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
