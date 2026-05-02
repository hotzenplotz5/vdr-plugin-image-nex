/*
 * image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold   <vdr07 at deltab.de>
 * (C) 2004	"Interpohl"  <interpohl-at-vdr-portal.de> 
 * (C) 2004	O.Kreuzinger <Onno-at-Kreuzinger.biz>
 * (C) 2003 Kai Tobias Burwieck <kai-at-burwieck.net>
 * (C) 2001,2002 Stefan Huelswitt <huels-at-iname.com>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include "menu.h"
#include <vdr/i18n.h>

// --- cMenuBrowseItem ----------------------------------------------------------

class cMenuBrowseItem:public cOsdItem {
private:
  cDirItem * item;
  virtual void Set(void);
public:
  cMenuBrowseItem(cDirItem * Item);
  cDirItem *Item(void) { return item; }
};

cMenuBrowseItem::cMenuBrowseItem(cDirItem * Item)
{
  item = Item;
  Set();
}

void cMenuBrowseItem::Set(void)
{
  char *buffer = 0;
  asprintf(&buffer, item->Type == itFile ? "%s" : "[%s]", item->DisplayName);
  SetText(buffer, false);
}

// --- cMenuBrowse ------------------------------------------------------

cDirItem *cMenuBrowse::lastselect = 0;

cMenuBrowse::cMenuBrowse(cFileSource * Source, bool Dirselect,const char *title)
: cOsdMenu(title)
{
  currentdir = parent = 0;
  delete lastselect;
  lastselect = 0;
  list = new cDirList;

  dirselectable = Dirselect;

  SetSource(Source);
  LoadDir(currentdir);
  SetButtons();
}

cMenuBrowse::~cMenuBrowse()
{
  free(parent);
  free(currentdir);
  delete list;
}

cDirItem *cMenuBrowse::CurrentItem(void)
{
  cMenuBrowseItem *item = (cMenuBrowseItem *) Get(Current());
  return item ? item->Item() : 0;
}

void cMenuBrowse::SetButtons(void)
{
  SetHelp(tr("Select"), 0, 0, currentdir ? tr("Parent") : 0);
  Display();
}

void cMenuBrowse::SetSource(cFileSource * Source)
{
  source = Source;
  free(currentdir);
  currentdir = 0;
  free(parent);
  parent = 0;
  source->GetRemember(currentdir, parent);
}

bool cMenuBrowse::LoadDir(const char *dir)
{
  Clear();

  //FIXME use a nonblocking way
  //OSD_InfoMsg(tr("Scanning directory..."));

  if(!list->Load(source, dir))
    return false;
  
  cDirItem *item = list->First();
  while(item)
  {
    Add(new cMenuBrowseItem(item),
      (parent && !strcmp(item->Name, parent)));
    item = list->Next(item);
  }
  return true;
}

bool cMenuBrowse::NewDir(const char *dir)
{
  if(LoadDir(dir))
  {
    char *ncur = dir ? strdup(dir) : 0;	// don't free currentdir first, as (new)dir may
    free(currentdir);
    currentdir = ncur;	// be a reference to somewhere inside currentdir as well
  
    cDirItem *item = CurrentItem();
    source->SetRemember(currentdir, item ? item->Name : 0);
  
    SetButtons();
    return true;
  }
  OSD_ErrorMsg(tr("Error scanning directory!"));
  return false;
}

eOSState cMenuBrowse::Parent(void)
{
  eOSState res = osContinue;
  
  if(currentdir)
  {
    free(parent);
    char *ss = strrchr(currentdir, '/');
    if(ss)
    {
      *ss++ = 0;
      parent = strdup(ss);
      ss = currentdir;
    }
    else
      parent = strdup(currentdir);
  
    if(!NewDir(ss))
      res = osEnd;
  }
  return res;
}

eOSState cMenuBrowse::Select(bool isred)
{
    eOSState res = osContinue;
    cDirItem *item;

    if((item = CurrentItem()))
	{
	    switch (item->Type)
		{
		case itParent:
		    if(!isred || !dirselectable)
			res = Parent();
		    break;
		case itDir:
		    if(!isred || !dirselectable)
			{
			    char *d = item->Path();
			    if(!NewDir(d))
				res = osEnd;
			    free(d);
			    break;
			}
		    // fall through to itFile
		case itFile:
		    lastselect =
			new cDirItem(source, currentdir, item->Name,
				     item->Type);
		    res = osBack;
		    break;
		default:
		    break;
		}
	}
    return res;
}

eOSState cMenuBrowse::ProcessKey(eKeys Key)
{
  eOSState state = cOsdMenu::ProcessKey(Key);
  
  if(state == osUnknown)
  {
	    switch (Key)
		{
		case kOk:
		    state = Select(false);
		    break;
		case kRed:
		    state = Select(true);
		    break;
		case kBlue:
		    state = Parent();
		    break;
		case kMenu:
		    state = osEnd;
		    break;
		default:
		    break;
		}
  }
  if(state == osEnd || state == osBack)
  {
    cDirItem *item = CurrentItem();
    if(item)
      source->SetRemember(currentdir, item->Name);
  }
  return state;
}

// --- cMenuSourceItem ----------------------------------------------------------

class cMenuSourceItem:public cOsdItem {
  private:
    cFileSource * source;
    virtual void Set(void);
  public:
    cMenuSourceItem(cFileSource * Source);
    cFileSource *Source(void) { return source; }
};

cMenuSourceItem::cMenuSourceItem(cFileSource * Source)
{
    source = Source;
    Set();
}

void cMenuSourceItem::Set(void)
{
    char *buffer = 0;
    asprintf(&buffer, "%s\t%s\t%s",
	     source->NeedsMount()? (source->Status()? "*" : ">") : "",
	     source->Description(), source->BaseDir());
    SetText(buffer, false);
}

// --- cMenuSource --------------------------------------------------

cFileSource *cMenuSource::selected = 0;

cMenuSource::cMenuSource(cFileSources * Sources,
			 const char *title):cOsdMenu(title, 2, 20)
{
  selected = 0;
  current = Sources->GetSource();
  cFileSource *source = Sources->First();
  while(source) {
    cOsdMenu::Add(new cMenuSourceItem(source), source == current);
    source = Sources->Next(source);
  }
  
  SetHelp(tr("Select"), tr("Mount"), tr("Unmount"), tr("Eject"));
  Display();
}

bool cMenuSource::DoMount(cFileSource * src)
{
  bool res = src->Mount();
  RefreshCurrent();
  DisplayCurrent(true);
  return res;
}

bool cMenuSource::CheckMount(void)
{
  cFileSource *src = selected ? selected : current;
  if(src->NeedsMount() && !src->Status()) {
    OSD_ErrorMsg(tr("Selected data medium is not mounted!"));
    return false;
  }
  return true;
}

eOSState cMenuSource::Select(void)
{
  if(HasSubMenu() || Count() == 0)
    return osContinue;

  cFileSource *src = ((cMenuSourceItem *) Get(Current()))->Source();
  if(src->NeedsMount() && !src->Status())
  {
    if(!DoMount(src))
      OSD_ErrorMsg(tr("Mount failed!"));
  }
  if(!src->NeedsMount() || src->Status())
	{
    selected = src;
    return osBack;
	}
  return osContinue;
}

eOSState cMenuSource::Mount(void)
{
  if(HasSubMenu() || Count() == 0)
    return osContinue;

  cFileSource *src = ((cMenuSourceItem *) Get(Current()))->Source();
  if(src->NeedsMount() && !src->Status())
  {
    if(DoMount(src))
      OSD_InfoMsg(tr("Mount succeeded"));
    else
      OSD_ErrorMsg(tr("Mount failed!"));
  }
  return osContinue;
}

eOSState cMenuSource::Unmount(void)
{
  if(HasSubMenu() || Count() == 0)
    return osContinue;
  
  cFileSource *src = ((cMenuSourceItem *) Get(Current()))->Source();
  if(src->NeedsMount() && src->Status())
  {
    bool res = src->Unmount();
    RefreshCurrent();
    DisplayCurrent(true);
    if(res)
      OSD_InfoMsg(tr("Unmount succeeded!"));
    else
      OSD_ErrorMsg(tr("Unmount failed!"));
  }
  return osContinue;
}

eOSState cMenuSource::Eject(void)
{
  if(HasSubMenu() || Count() == 0)
    return osContinue;
  
  cFileSource *src = ((cMenuSourceItem *) Get(Current()))->Source();
  if(src->NeedsMount())
  {
    bool res = src->Eject();
    RefreshCurrent();
    DisplayCurrent(true);
    if(!res)
      OSD_ErrorMsg(tr("Eject failed!"));
  }
  return osContinue;
}

eOSState cMenuSource::ProcessKey(eKeys Key)
{
  eOSState state = cOsdMenu::ProcessKey(Key);
  
  if(state == osBack && !CheckMount())
    state = osContinue;
  if(state == osUnknown)
  {
	    switch (Key)
		{
		case kOk:
		case kRed:
		    return Select();
		case kGreen:
		    return Mount();
		case kYellow:
		    return Unmount();
		case kBlue:
		    return Eject();
		case kMenu:
		    CheckMount();
		    return osEnd;
		default:
		    break;
		}
	}
  return state;
}
