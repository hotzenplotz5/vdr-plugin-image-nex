/*
 * Image plugin to VDR (C++)
 *
 * (C) 2006-2011 Andreas Brachold    <vdr07 at deltab.de>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#ifndef __MENU_EXIF_H
#define __MENU_EXIF_H

#include "image.h"
#include <vdr/menuitems.h>
#include <vdr/interface.h>
#include <string>


class cImageMenuExif : public cOsdMenu 
{
  bool        m_bDisplayTags; 
  std::string m_strTextTag;
  std::string m_strTextMarker;
protected:
	virtual void Display(void);
public:
  cImageMenuExif(const char *szFileName);
  virtual eOSState ProcessKey(eKeys Key);
};

enum ImageExifOrientation { 
	IMAGE_EXIF_ORIENTATION_NONE = 0,
	IMAGE_EXIF_ORIENTATION_TOP_LEFT,
	IMAGE_EXIF_ORIENTATION_TOP_RIGHT,
	IMAGE_EXIF_ORIENTATION_BOTTOM_RIGHT,
	IMAGE_EXIF_ORIENTATION_BOTTOM_LEFT,
	IMAGE_EXIF_ORIENTATION_LEFT_TOP,
	IMAGE_EXIF_ORIENTATION_RIGHT_TOP,
	IMAGE_EXIF_ORIENTATION_RIGHT_BOTTOM,
	IMAGE_EXIF_ORIENTATION_LEFT_BOTTOM
};

enum ImageMirror { 
	NONE = 0,
	MIRROR,
	FLIP
};

ImageExifOrientation GetRotationFromExifData (const char *filename, int& rotate, ImageMirror& mirror);

bool ExtractExifThumbnail(const char* jpegFile, const char* thumbOut);

#endif //__MENU_EXIF_H
