/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold <vdr07 at deltab.de>
 * (C) 2003 Kai Tobias Burwieck <kai@burwieck.net>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#ifndef ___DATA_IMAGE_H
#define ___DATA_IMAGE_H

#include "data.h"
#include <vdr/tools.h>

// ----------------------------------------------------------------

extern cFileSources ImageSources;

class cFileSource;

// ----------------------------------------------------------------

class cImageData
: public cListObject {

  cFileSource *m_pSource;
  char        *m_szFileName;
  
#ifdef HAVE_LIBEXIF
  int          m_nDefaultRotate;
#endif
protected:
  void Clear(void);
public:
  cImageData(const char *szName, cFileSource * pSource);
  virtual ~cImageData();
  bool CompareBaseDir(const cFileSource * pSource) const;

  inline const char *Name(void) const       
  { 
    return m_szFileName; 
  } 

#ifdef HAVE_LIBEXIF
  int DefaultRotate() const { return m_nDefaultRotate; }
#endif
};

// ----------------------------------------------------------------


class cSlideShow
: public cScanDir 
, public cList < cImageData > {

  char* m_szFirstImageName;
  cDirItem m_DirItem;

protected:

  virtual void DoItem(cFileSource * src, const char *subdir, const char *name);

public:
  cSlideShow(cDirItem * Item);
  virtual ~cSlideShow();
  virtual bool Load(void);

  inline const char* FirstImage() const { return m_szFirstImageName; } 
};

#endif				//___DATA_IMAGE_H
