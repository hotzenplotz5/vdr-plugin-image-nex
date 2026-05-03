/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold    <vdr07 at deltab.de>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */


#include <errno.h>
#include "menu-commands.h"
#include <vdr/i18n.h>

cImageMenuCommands::cImageMenuCommands(const char *szTitle, cImageCommands *pCmds, const char *szFileName)
: cOsdMenu(m_szTitle = strdup(szTitle))
, m_pCmds( pCmds )
, m_szFileName(NULL)
, m_bImageChanged(false)
{
    SetHasHotkeys();
    if(szFileName)
        m_szFileName = strdup(szFileName);

    int i = 0;
    cImageCommand *p = NULL;
    for(;NULL != (p = m_pCmds->Get(i));++i)
    {
        Add(new cOsdItem(hk(p->Title())));
    }
    SetHelp(tr("Execute"), NULL, NULL, tr("Back"));
    Display();
}

cImageMenuCommands::~cImageMenuCommands()
{
    if(m_szTitle)
        free(m_szTitle);
    if(m_szFileName)
        free(m_szFileName);
    
    if(m_pCmds)
        delete m_pCmds;
}

eOSState cImageMenuCommands::Execute(void)
{
  errno = 0; // unset error
  char *sz = NULL;
  cImageCommand *p;
  struct stat stFile,stChanged;
  if(0 == stat(m_szFileName, &stFile) 
    && NULL != (p = m_pCmds->Get(Current())))
  {    
     bool bConfirmed = true;
     if (p->Confirm()) {
        if (asprintf(&sz, "%s?", p->Title()) >= 0) {
            bConfirmed = Interface->Confirm(sz);
            free(sz);
        }
        }
     if (bConfirmed) {
        if (asprintf(&sz, "%s...", p->Title()) >= 0) {
            OSD_InfoMsg(sz);
            free(sz);
        }
        const char *szResult = p->Execute(m_szFileName);
        if(0 == stat(m_szFileName, &stChanged))
        {
          m_bImageChanged = stFile.st_mtime != stChanged.st_mtime;
        }
        if (szResult)
           return AddSubMenu(new cImageMenuResult(p->Title(), szResult));
        return osEnd;
    }
    return osContinue;
  }
  const char* szErr = errno?strerror(errno):tr("Operation failed");
  if (asprintf(&sz, "%s (%s)", szErr, m_szFileName) >= 0) {
      OSD_ErrorMsg(sz);
      free(sz);
  }
  return osContinue;
}

eOSState cImageMenuCommands::ProcessKey(eKeys nKey)
{
  eOSState state = cOsdMenu::ProcessKey(nKey);

  if (state == osUnknown) {
     switch (nKey) {
       case kRed:
       case kOk:  return Execute();
       case kMenu:
       case kBack:
       case kBlue:
                  return osBack;
       default:   break;
       }
     }
  return state;
}



cImageMenuResult::cImageMenuResult(const char *szTitle, const char *szText)
: cOsdMenu(szTitle)
, m_szText(szText)
{
  SetHelp(NULL, NULL, NULL, tr("Back"));
}


eOSState cImageMenuResult::ProcessKey(eKeys nKey)
{
  switch (nKey & ~k_Repeat) {
    case kUp:
    case kDown:
    case kLeft:
    case kRight:
                  DisplayMenu()->Scroll(NORMALKEY(nKey) == kUp || NORMALKEY(nKey) == kLeft, NORMALKEY(nKey) == kLeft || NORMALKEY(nKey) == kRight);
                  return osContinue;
    default: break;
    }

  eOSState nState = cOsdMenu::ProcessKey(nKey);
  if (nState == osUnknown) {
     switch (nKey) {
       case kOk: 
       case kMenu:
       case kBack:
       case kBlue:
                 return osBack;
       default:  nState = osContinue;
       }
     }
  return nState;
}

void cImageMenuResult::Display(void)
{
  cOsdMenu::Display();
  DisplayMenu()->SetText(m_szText, true);
}
