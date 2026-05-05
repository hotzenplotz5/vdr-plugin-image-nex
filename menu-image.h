/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold    <vdr07 at deltab.de>
 * (C) 2003 Kai Tobias Burwieck <kai@burwieck.net>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#ifndef ___MENU_IMAGE_H
#define ___MENU_IMAGE_H

#include <vdr/osdbase.h>
#include <vdr/menuitems.h>

#include "menu.h"
#include "data.h"

// ----------------------------------------------------------------
class cMenuImageBrowse:public cMenuBrowse {
  private:
    bool sourcing;
    void SetButtons(void);
    eOSState Source(bool second);
  public:
     cMenuImageBrowse(void);
    virtual eOSState ProcessKey(eKeys Key);
};

class cMenuImageGrid : public cOsdMenu {
private:
    cFileSource *source;
    cDirList *list;
    int currentIndex;
    int columns;
    int osdWidth, osdHeight;
    char *currentdir;

    bool LoadDir(const char *dir);
    void DrawGrid();
    cDirItem *CurrentItem();
    eOSState Select(bool isred);
    eOSState Parent();
public:
    cMenuImageGrid(cFileSource *Source);
    virtual ~cMenuImageGrid();
    virtual void Display(void);
    virtual eOSState ProcessKey(eKeys Key);
};

void StopThumbLoader();

#endif				//___MENU_IMAGE_H
