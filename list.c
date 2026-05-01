/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold <vdr07 at deltab.de>
 *  
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#include <string.h>
 
#include "list.h"
#include "data-image.h"
#include "setup-image.h"

#include <vdr/tools.h>

cActivSlideShow theSlideShow;

cActivSlideShow::cActivSlideShow()
: m_pCurSlideShow(NULL)
, m_nCurrentImage(0)
, m_nTotalImages(0)
, m_pCurImage(NULL)
{

}

void cActivSlideShow::Remove(cFileSource* src){

  if(m_pCurSlideShow) {
    cImageData* p = m_pCurSlideShow->cList < cImageData >::First();
    while (p) {
    if(p->CompareBaseDir(src)) {
      bool bRemoveCurrent = (m_pCurImage == p); 
      cImageData* next = m_pCurSlideShow->cList < cImageData >::Next(p);
      m_pCurSlideShow->cList < cImageData >::Del(p);
      p = next;
      if(bRemoveCurrent) {
        m_nCurrentImage = 1;
        m_pCurImage = m_pCurSlideShow->cList < cImageData >::First();
      }
    } else {
      p = m_pCurSlideShow->cList < cImageData >::Next(p);
    }
    }
    m_nTotalImages = m_pCurSlideShow->cList < cImageData >::Count();
  }
} 

void cActivSlideShow::Assign(cSlideShow *pCurSlideShow)
{
  Shutdown();
  m_nCurrentImage = 0;
  m_nTotalImages = 0;
  m_pCurSlideShow = pCurSlideShow;
  if(m_pCurSlideShow) {

    m_nTotalImages = m_pCurSlideShow->Count();
    // get real Index of FirstImage
    unsigned int i = 0;
    bool bFound = false;
    if(m_pCurSlideShow->FirstImage()) {

      cImageData* p = m_pCurSlideShow->cList < cImageData >::First();

        for (;p && i < m_nTotalImages; ++i) {
        // find out first image
        if(0 == strcmp(::basename(p->Name()),m_pCurSlideShow->FirstImage())) {
          m_pCurImage = p;
          bFound = true;
          break;
        }
        p = m_pCurSlideShow->cList < cImageData >::Next(p);
      }
    }
    if(bFound) {
      m_nCurrentImage = i+1;
    }
    else {
      m_pCurImage = m_pCurSlideShow->cList < cImageData >::First();
      m_nCurrentImage = 1;
    }
  }
}

void cActivSlideShow::Shutdown(void)
{
  if(m_pCurSlideShow) {
    delete m_pCurSlideShow;
    m_pCurSlideShow = 0;
    m_pCurImage = 0;
    m_nCurrentImage = 0;
    m_nTotalImages = 0;
  }
}

cSlideShow *cActivSlideShow::SlideShow(void)
{
  return m_pCurSlideShow;
}


cImageData* cActivSlideShow::GetImage()
{
  return m_pCurSlideShow?m_pCurImage:NULL;
}


bool cActivSlideShow::NextImage(int nOffset)
{
  if(m_pCurImage && m_nCurrentImage < m_nTotalImages) {
    cImageData *pNewActiv;
    for (int i = 0; (i < nOffset) && (m_nCurrentImage < m_nTotalImages); ++i) {
      pNewActiv = m_pCurSlideShow->cList < cImageData >::Next(m_pCurImage);
      m_pCurImage = pNewActiv;
      ++m_nCurrentImage;
    }
    return m_pCurImage != NULL;
  } 
  else if(ImageSetup.m_bAutoRepeat) {
    cImageData *pNewActiv;
    
    pNewActiv = m_pCurSlideShow->cList < cImageData >::First();
    m_pCurImage = pNewActiv;
    m_nCurrentImage = 1;
    
    return m_pCurImage != NULL;
  }
  return false;
}



bool cActivSlideShow::PrevImage(int nOffset)
{
  if(m_pCurImage && m_nCurrentImage > 1) {
    cImageData *pNewActiv;

    for (int i = 0; (i < nOffset) && (m_nCurrentImage > 1); ++i) {
        pNewActiv = m_pCurSlideShow->cList < cImageData >::Prev(m_pCurImage);
        m_pCurImage = pNewActiv;
        --m_nCurrentImage;
    }
    return m_pCurImage != NULL;
  }
  else if(ImageSetup.m_bAutoRepeat) {
    cImageData *pNewActiv;

    pNewActiv = m_pCurSlideShow->cList < cImageData >::Last();
    m_pCurImage = pNewActiv;
    m_nCurrentImage = m_nTotalImages;
      return m_pCurImage != NULL;
  }
  return false;
}


bool cActivSlideShow::GotoImage(unsigned int nNewPictureIndex)
{
  if(m_pCurImage && nNewPictureIndex > 0 && nNewPictureIndex < m_nTotalImages) {
    cImageData *pNewActiv;
    pNewActiv = m_pCurSlideShow->cList < cImageData >::First();
    m_pCurImage = pNewActiv;
    m_nCurrentImage = 0;
    for (unsigned int i = 0; i < nNewPictureIndex; ++i) {
      pNewActiv = m_pCurSlideShow->cList < cImageData >::Next(m_pCurImage);
      m_pCurImage = pNewActiv;
      ++m_nCurrentImage;
    }
    return m_pCurImage != NULL;
	}
  return false;
}

int cActivSlideShow::GetJumpNames(int nOffset,cImageData* pImage[],const unsigned int nMAX_BILDER)
{
  unsigned int i;
  cImageData *pNewActiv;
  int nBilder = 0;

  if(m_pCurImage && m_nCurrentImage > 0) {
  
	  int nJumpFirst = m_nCurrentImage + nOffset;

      if(nJumpFirst > ((int)(m_nTotalImages - nMAX_BILDER + 1)))
		  nJumpFirst = ((int)(m_nTotalImages - nMAX_BILDER + 1));
	  if(nJumpFirst < 1)
		  nJumpFirst = 1;
	  
	  m_nCurrentImage = nJumpFirst;
      if(m_nCurrentImage > m_nTotalImages)
		  m_nCurrentImage = m_nTotalImages;
	  if(m_nCurrentImage < 1)
		  m_nCurrentImage = 1;

    pNewActiv = m_pCurSlideShow->cList < cImageData >::First();
    for (int n = 1; n < nJumpFirst && pNewActiv; ++n) 
      pNewActiv = m_pCurSlideShow->cList < cImageData >::Next(pNewActiv);
	  for (i = 0; (i < nMAX_BILDER) && ((nJumpFirst + i) <= m_nTotalImages) && pNewActiv; ++i,++nBilder) {

      pImage[i] = pNewActiv;
	    dsyslog("imageplugin: File%d: %s", i, pNewActiv->Name());
	    if((nJumpFirst + i) == m_nCurrentImage)
		    m_pCurImage = pNewActiv;
	    pNewActiv = m_pCurSlideShow->cList < cImageData >::Next(pNewActiv);
	  }
  }
  return nBilder;
}
