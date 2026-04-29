/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold    <vdr07 at deltab.de>
 * based on (C) 2003 Kai Tobias Burwieck      <kai-at-burwieck.net>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#ifndef ___SETUP_IMAGE_H
#define ___SETUP_IMAGE_H

extern const char *g_szConvertScript;
extern const char *g_szConfigDirectory;

#include <vdr/menuitems.h>

#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

class cImageSetup 
{
public:
  /* It Slideshow activ */
  int m_bSlideShow;
  /* If Slideshow activ, how many seconds between slides */
  int m_nSSsec;
  /* Where stored temp. files */
  char m_szTempDir[MAX_PATH];
  /* Should slideshow automated repeat */
  int m_bAutoRepeat;
  /* Should show overlay numbers on indexdump */
  int m_bShowNumbers;
  /* Don't mute audio, on show images */
  int m_bLiveAudio;
  /* Activate own house keeping system for temp. files, avoid pregenerated images */
  int m_bHousekeeping;
  /* Remove temp files immediately   */
  int m_bRemoveImmediately;
  /* Use as output DeviceStillPicture */
  int m_bUseDeviceStillPicture;
  
  /* 0 = List, 1 = Grid View */
  int m_nDisplayMode;
  /* Number of columns in Grid (0 = Auto) */
  int m_nGridColumns;

  /** Minimum Value for Slideshow */
  static const int m_cSSMin;
  /** Maximum Value for Slideshow */
  static const int m_cSSMax;

  /** Range for BorderHeight */
  static const int m_cnMinBorderHeight;
  static const int m_cnMaxBorderHeight;
  int m_nBorderHeight;

  /** Range for BorderWidth */
  static const int m_cnMinBorderWidth;
  static const int m_cnMaxBorderWidth;
  int m_nBorderWidth;
  
  /** Show entry in Main menu */
  int m_bHideMenu;


public:
  cImageSetup(void);
  void SetEnv(void) const;
  
  bool SetupParse(const char *szName, const char *szValue);
};


// ----------------------------------------------------------------

class cMenuSetupImage
	:public cMenuSetupPage 
{
  cImageSetup m_tmpSetup;
protected:
  virtual void Store(void);
public:
  cMenuSetupImage(void);
};


extern cImageSetup ImageSetup;

#endif				//___SETUP_IMAGE_H
