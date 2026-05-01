/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold <vdr07 at deltab.de>
 * based on (C) 2003 Kai Tobias Burwieck <kai -at- burwieck.net>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "data-image.h"
#include "data.h"
#include "setup-image.h"

#ifdef HAVE_LIBEXIF
#include "exif.h"
#endif

cFileSources ImageSources;

// --- cImageData -------------------------------------------------------------

cImageData::cImageData(const char *szName, cFileSource * pSource)
: m_pSource(pSource)
, m_szFileName(NULL)
#ifdef HAVE_LIBEXIF
, m_nDefaultRotate(0)
#endif
{
  if(szName) {
    m_szFileName = m_pSource->BuildName(szName);
#ifdef HAVE_LIBEXIF
    ImageMirror mirror = NONE;
    GetRotationFromExifData (m_szFileName, m_nDefaultRotate, mirror);
    if(mirror != NONE)
        isyslog("imageplugin: automated rotation from exif data with mirroring (%d) is'nt supported, found at file '%s' \n", mirror, m_szFileName);
#endif
  }
  m_pSource->Block();

}

cImageData::~cImageData()
{
  Clear();
  m_pSource->Unblock();
}

void cImageData::Clear(void)
{
  if(m_szFileName)
  {
    free(m_szFileName);
    m_szFileName = NULL;
  }
}

bool cImageData::CompareBaseDir(const cFileSource * pSource) const
{
  return 0 == strcmp(m_pSource->BaseDir(),pSource->BaseDir());

}
// -- cSlideShow --------------------------------------------------------------

cSlideShow::cSlideShow(cDirItem * Item)
: m_szFirstImageName(NULL)
, m_DirItem(Item->Source, Item->Subdir, Item->Name, Item->Type)
{
    
}

cSlideShow::~cSlideShow()
{
  if(m_szFirstImageName)
    free(m_szFirstImageName);
}

bool cSlideShow::Load(void)
{
  bool res = false;
  Clear();
  switch (m_DirItem.Type)
	{
	case itFile:
    {
    dsyslog("imageplugin: SlideShow: file %s\n", m_DirItem.Name);
		const char *path = m_DirItem.Subdir;
		res =
		    ScanDir(m_DirItem.Source, path, stFile, m_DirItem.Source->Include(),
			    0, true);
        if(res)
        {
               m_szFirstImageName = strdup(m_DirItem.Name); 
        }
        else
        {
             DoItem(m_DirItem.Source, m_DirItem.Subdir, m_DirItem.Name);
             res = true;
        }
		break;
        }
	case itDir:
	    {
		dsyslog("imageplugin: SlideShow: dir name:%s\n", m_DirItem.Name);
		char *path = m_DirItem.Path();
		res =
		    ScanDir(m_DirItem.Source, path, stFile, m_DirItem.Source->Include(),
			    0, true);
 		free(path);
		break;
	    }
	case itBase:
	    dsyslog("imageplugin: SlideShow: base\n");
	    res =
		ScanDir(m_DirItem.Source, 0, stFile, m_DirItem.Source->Include(), 0,
			true);
	    break;
	default:
	    break;
	}
  return res;
}

void cSlideShow::DoItem(cFileSource * src, const char *subdir,
			 const char *name)
{
  char *path = (char *)name;
  if(subdir)
    path = AddPath(subdir, name);
  Add(new cImageData(path, src));
  if(subdir)
    free(path);
}
