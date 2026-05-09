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
#include <libskindesignerapi/skindesignerapi.h>
#include <libskindesignerapi/skindesignerosdbase.h>

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

class cMenuImageGrid : public cOsdObject {
private:
    cOsd *myOsd;
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
    virtual void Show(void);
    virtual eOSState ProcessKey(eKeys Key);
};

class cMenuImageSkinDesigner : public skindesignerapi::cSkindesignerOsdObject {
private:
    cFileSource *source;
    cDirList *list;
    char *currentdir;
    int currentIndex;
    bool needsRedraw;

    skindesignerapi::cOsdView *rootView;
    skindesignerapi::cViewElement *back;
    skindesignerapi::cViewElement *header;
    skindesignerapi::cViewGrid *imagegrid;

    bool LoadDir(const char *dir);
    cDirItem *CurrentItem();
    void Draw();
    eOSState Select(bool isred);
    eOSState Parent();
public:
    cMenuImageSkinDesigner(cFileSource *Source, skindesignerapi::cPluginStructure *plugStruct);
    virtual ~cMenuImageSkinDesigner();
    virtual void Show(void);
    virtual eOSState ProcessKey(eKeys Key);
    
    static void DefineTokensElements(int ve, skindesignerapi::cTokenContainer *tk);
    static void DefineTokensGrids(int vg, skindesignerapi::cTokenContainer *tk);
};

void StopThumbLoader();

#endif				//___MENU_IMAGE_H
