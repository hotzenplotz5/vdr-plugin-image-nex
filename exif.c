/*
 * Image plugin to VDR (C++)
 *
 * (C) 2006-2011 Andreas Brachold    <vdr07 at deltab.de>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

extern "C" {
#include <libexif/exif-data.h>
#include <libexif/exif-ifd.h>
#include <libexif/exif-loader.h>
}

#include <sstream>

#include "player-image.h"
#include "control-image.h"
#include "exif.h"
#include <vdr/i18n.h>


// for backward compatible to libexif-0.6.12
#ifndef exif_entry_get_ifd
#define exif_entry_get_ifd(e) exif_content_get(e)
#define exif_tag_get_title_in_ifd(a,b) exif_tag_get_title(a)
#endif

static void callbackShowEntry (ExifEntry *e, void *pData)
{
  if (!pData)
    return;
  char v[128];

  std::ostream* po = (std::ostream*)pData;
  if(exif_entry_get_value (e, v, 59)) {
    (*po) <<  exif_tag_get_title_in_ifd (e->tag, exif_entry_get_ifd (e));
    (*po) << ":\t";
    (*po) << v;
    (*po) << std::endl;
  }
}

static void callbackShowIFD (ExifContent *content, void *pData)
{
	exif_content_foreach_entry (content, callbackShowEntry, pData);
}

void TagList (std::ostream& o, ExifData *ed)
{
	if (!ed)
		return;

	exif_data_foreach_content (ed, callbackShowIFD, &o);
}

static bool MarkerNodes (std::string& s, ExifData *d)
{
  unsigned int i, c;
  char v[1024], *p;
  ExifMnoteData *md;

  md = exif_data_get_mnote_data (d);
  if (!md) {
    return false;
  }

  c = exif_mnote_data_count (md);
  for (i = 0; i < c; i++) {
    p = exif_mnote_data_get_value (md, i, v, sizeof (v));
    if (p && strlen(v)) { 
      const char * t = exif_mnote_data_get_title (md, i);
      s += t ? t : "";
      s += ":\t";
      s +=  v;
      s += "\n";
    }
  }
  return true;
}

cImageMenuExif::cImageMenuExif(const char *szFileName)
: cOsdMenu(tr("Show exif informations from image"))
{
  std::ostringstream o;
  ExifData *d;

  d = exif_data_new_from_file (szFileName);
  if (d) {
      MarkerNodes (m_strTextMarker, d);

      TagList(o, d);
      exif_data_unref (d);
  } else  {
    o << tr("Could not load exif data from image!") << std::endl;
  }
  m_strTextTag = o.str();
  m_bDisplayTags = true;
  Display();
}

eOSState cImageMenuExif::ProcessKey(eKeys nKey)
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
       case kRed:
          m_bDisplayTags = !m_bDisplayTags;
          Display();
       default:  nState = osContinue;
       }
     }
  return nState;
}

void cImageMenuExif::Display(void)
{
  const char* szButton = NULL;
  if(m_bDisplayTags) {
      if(m_strTextMarker.length())
        szButton = tr("Marker");
  } else {
      if(m_strTextTag.length())
        szButton = tr("Tags");
  }
  SetHelp(szButton, NULL, NULL, tr("Back"));

  cOsdMenu::Display();
  DisplayMenu()->SetText(m_bDisplayTags 
                        ? m_strTextTag.c_str()
                        : m_strTextMarker.c_str(),true);
}


cOsdObject *cImageControl::GetInfo(void)
{
  const char* szFile = FileName();
  if(szFile)
    return new cImageMenuExif(szFile);
  return NULL;
}

//////////////////////////////////////////////////////////////////////////////
/** get value from exif tag
@return ExifShort - requestet value or 0 if failed
@param const char *filename - used file name 
@param ExifTag etag - wanted exif Tag, see exif-tag.h*/
static ExifShort GetExifTag (const char *filename, ExifTag etag)
{
	ExifData     *edata;
	unsigned int  i, j;

	edata = exif_data_new_from_file (filename);

	if (edata == NULL) 
		return 0;

	for (i = 0; i < EXIF_IFD_COUNT; i++) {
		ExifContent *content = edata->ifd[i];

		if (! edata->ifd[i] || ! edata->ifd[i]->count) 
			continue;

		for (j = 0; j < content->count; j++) {
			ExifEntry *e = content->entries[j];

			if (! content->entries[j]) 
				continue;

			if (e->tag == etag) {
				ExifByteOrder o = exif_data_get_byte_order (e->parent->parent);
				ExifShort retval = exif_get_short (e->data, o);
				exif_data_unref (edata);
				return retval;
			}
		}
	}

	exif_data_unref (edata);

	return 0;
}

//////////////////////////////////////////////////////////////////////////////
/** get rotation from exif data
@return ImageExifOrientation - requestet value or 0 if failed
@param int& rotate - reference for rotation
@param  ImageMirror& mirror  - reference for mirroring image */
ImageExifOrientation GetRotationFromExifData (const char *filename, int& rotate, ImageMirror& mirror)
{
    ImageExifOrientation ieo =  (ImageExifOrientation)GetExifTag (filename, EXIF_TAG_ORIENTATION);

	switch (ieo) {
	case IMAGE_EXIF_ORIENTATION_TOP_RIGHT:
		rotate = 0;
		mirror = MIRROR;
		break;
	case IMAGE_EXIF_ORIENTATION_BOTTOM_LEFT:
		rotate = 180;
		mirror = MIRROR;
		break;
	case IMAGE_EXIF_ORIENTATION_LEFT_TOP:
		rotate = 90;
		mirror = MIRROR;
		break;
	case IMAGE_EXIF_ORIENTATION_RIGHT_BOTTOM:
		rotate = 90;
		mirror = FLIP;
		break;
	case IMAGE_EXIF_ORIENTATION_TOP_LEFT:
		rotate = 0;
		mirror = NONE;
		break;
	case IMAGE_EXIF_ORIENTATION_RIGHT_TOP:
		rotate = 90;
		mirror = NONE;
		break;
	case IMAGE_EXIF_ORIENTATION_BOTTOM_RIGHT:
		rotate = 180;
		mirror = NONE;
		break;
	case IMAGE_EXIF_ORIENTATION_LEFT_BOTTOM:
		rotate = 270;
		mirror = NONE;
		break;
	default:
		rotate = 0;
		mirror = NONE;
		break;
	}
    return ieo;
}

bool ExtractExifThumbnail(const char* jpegFile, const char* thumbOut) {
    ExifData *edata = exif_data_new_from_file(jpegFile);
    if (edata && edata->data && edata->size > 0) {
        FILE *f = fopen(thumbOut, "wb");
        if (f) {
            fwrite(edata->data, 1, edata->size, f);
            fclose(f);
            exif_data_unref(edata);
            return true;
        }
    }
    if (edata) exif_data_unref(edata);
    return false;
}
