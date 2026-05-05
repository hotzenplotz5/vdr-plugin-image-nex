/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold    <vdr07 at deltab.de>
 * based on (C) 2003 Kai Tobias Burwieck      <kai-at-burwieck.net>
 *
 * based on MP3/MPlayer plugin to VDR (C++)
 * (C) 2001,2002 Stefan Huelswitt <huels-at-iname.com>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vdr/tools.h>
#include <vdr/config.h>
#include "setup-image.h"
#include <vdr/i18n.h>

cImageSetup ImageSetup;

const int cImageSetup::m_cSSMin = 2;
const int cImageSetup::m_cSSMax = 300;


const int cImageSetup::m_cnMinBorderHeight = 0;
const int cImageSetup::m_cnMaxBorderHeight = 480/2; //(Half NTSC)

const int cImageSetup::m_cnMinBorderWidth = 0;
const int cImageSetup::m_cnMaxBorderWidth = 720/2;  //(Half NTSC/PAL)

// --- cImageSetup -----------------------------------------------------------

cImageSetup::cImageSetup(void)

{
  m_bSlideShow = 0;
  m_nSSsec = 10;
  m_bAutoRepeat = 0;
  m_bShowNumbers = 1;
  m_bLiveAudio = 0;
  m_bUseDeviceStillPicture = 1;
  m_nDisplayMode = 0;
  m_nGridColumns = 0;

  m_nBorderHeight = 16;
  m_nBorderWidth = 16;
  
  m_bHideMenu = false;
}

#define ParseInteger(szTitle,nValue,nMin,nMax) \
  if(!strcasecmp(szName, szTitle))             \
  {                                            \
    nValue = atoi(szValue);                    \
    if(nValue < nMin) nValue = nMin;           \
    if(nValue > nMax) nValue = nMax;           \
  } 


bool cImageSetup::SetupParse(const char *szName, const char *szValue)
{
       ParseInteger("SlideShow",     m_bSlideShow,0,1)
  else ParseInteger("SSsec",         m_nSSsec,m_cSSMin,m_cSSMax)
  else ParseInteger("BorderHeight",  m_nBorderHeight,m_cnMinBorderHeight,m_cnMaxBorderHeight)
  else ParseInteger("BorderWidth",   m_nBorderWidth,m_cnMinBorderWidth,m_cnMaxBorderWidth)
  else ParseInteger("AutoRepeat",    m_bAutoRepeat,0,1)
  else ParseInteger("ShowNumbers",   m_bShowNumbers,0,1)
  else ParseInteger("LiveAudio",     m_bLiveAudio,0,1)
  else ParseInteger("HideMainMenu",  m_bHideMenu,0,1)
  else ParseInteger("UseDeviceStillPicture",  m_bUseDeviceStillPicture,0,1)
  else ParseInteger("DisplayMode",   m_nDisplayMode,0,2)
  else ParseInteger("GridColumns",   m_nGridColumns,0,20)
  else return false;
  return true;
}

// --- cMenuSetupImage --------------------------------------------------------
void cMenuSetupImage::Store(void)
{
  ImageSetup = m_tmpSetup;
  SetupStore("SlideShow",               ImageSetup.m_bSlideShow);
  SetupStore("SSsec",                   ImageSetup.m_nSSsec);
  SetupStore("AutoRepeat",              ImageSetup.m_bAutoRepeat);
  SetupStore("ShowNumbers",             ImageSetup.m_bShowNumbers);
  SetupStore("LiveAudio",               ImageSetup.m_bLiveAudio);
  SetupStore("BorderHeight",            ImageSetup.m_nBorderHeight);
  SetupStore("BorderWidth",             ImageSetup.m_nBorderWidth);
  SetupStore("HideMainMenu",            ImageSetup.m_bHideMenu);
  SetupStore("UseDeviceStillPicture",   ImageSetup.m_bUseDeviceStillPicture);
  SetupStore("DisplayMode",             ImageSetup.m_nDisplayMode);
  SetupStore("GridColumns",             ImageSetup.m_nGridColumns);
}

cMenuSetupImage::cMenuSetupImage(void)
: m_tmpSetup(ImageSetup)
{
  SetSection(tr("Images"));

  static const char *displayModes[3];
  displayModes[0] = tr("List");
  displayModes[1] = "Grid (C++)";
  displayModes[2] = "Grid (Skin XML)";
  Add(new cMenuEditStraItem(tr("Display Mode"), &m_tmpSetup.m_nDisplayMode, 3, displayModes));

  Add(new cMenuEditIntItem(tr("Grid columns (0=auto)"),
        &m_tmpSetup.m_nGridColumns,
        0, 20));

  Add(new cMenuEditBoolItem(tr("Slide show"),                    
        &m_tmpSetup.m_bSlideShow,    
        trVDR("no"), trVDR("yes")));

  Add(new cMenuEditIntItem (tr("Slide duration (sec)"),           
        &m_tmpSetup.m_nSSsec,        
        cImageSetup::m_cSSMin, cImageSetup::m_cSSMax));

  Add(new cMenuEditBoolItem(tr("Repeat slide show"),               
        &m_tmpSetup.m_bAutoRepeat,   
        trVDR("no"), trVDR("yes")));

  Add(new cMenuEditBoolItem(tr("Show numbers on index image"),    
        &m_tmpSetup.m_bShowNumbers,
        trVDR("no"), trVDR("yes")));

  Add(new cMenuEditBoolItem(tr("Hide main menu entry"),                    
          &m_tmpSetup.m_bHideMenu,    
          trVDR("no"), trVDR("yes")));

  Add(new cMenuEditBoolItem(tr("Output via DeviceStillPicture"),                    
          &m_tmpSetup.m_bUseDeviceStillPicture,    
          trVDR("yes"), trVDR("no")));

  Add(new cMenuEditBoolItem(tr("Live Audio from primary Device"), 
        &m_tmpSetup.m_bLiveAudio,
        trVDR("no"), trVDR("yes")));

  Add(new cMenuEditIntItem (tr("Border for Underscan (Height)"),
        &m_tmpSetup.m_nBorderHeight,
        cImageSetup::m_cnMinBorderHeight, cImageSetup::m_cnMaxBorderHeight));

  Add(new cMenuEditIntItem (tr("Border for Underscan (Width)"),
        &m_tmpSetup.m_nBorderWidth,
        cImageSetup::m_cnMinBorderWidth, cImageSetup::m_cnMaxBorderWidth));

}





void cImageSetup::SetEnv(void) const
{
  unsigned int i;
  struct cImageCommandEnv { 
    const char* szEnv;  
    const char* szValue;
  } nEnvironTable [] =
  {
    {"ASPECT_RATIO",Setup.VideoFormat?"16:9":"4:3"}, // Get from DVB-Setup
  };

  for(i=0;i < sizeof(nEnvironTable)/sizeof(*nEnvironTable);++i)
  {
    if(nEnvironTable[i].szValue) { 
      if(0!= setenv(nEnvironTable[i].szEnv, nEnvironTable[i].szValue,1))
        esyslog("imageplugin: can't setenv '%s = %s'", nEnvironTable[i].szEnv, nEnvironTable[i].szValue);
    }
    else { 
      if(0!= unsetenv(nEnvironTable[i].szEnv))
        esyslog("imageplugin: can't unsetenv '%s'", nEnvironTable[i].szEnv);
    }
  }
}
