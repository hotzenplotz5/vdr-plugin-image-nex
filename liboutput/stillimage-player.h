/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold    <vdr07 at deltab.de>
 *  Created: Thu Aug  5 2004
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */
 
#ifndef STILLIMAGEPLAYER_H
#define STILLIMAGEPLAYER_H

#include <vdr/thread.h>
#include <vdr/player.h>

#include "stillimage.h"

class cStillImagePlayer
  : public cPlayer {
  friend class cStillImage;
protected:  
  cStillImage m_StillImage;
public:
  cStillImagePlayer(ePlayMode PlayMode,bool bUseDeviceStillPicture);
  virtual ~cStillImagePlayer();

  inline unsigned int UseWidth() const           
    { return m_StillImage.GetWidth() - (m_StillImage.GetBorderWidth()*2);  } 
  inline unsigned int UseHeight() const          
    { return m_StillImage.GetHeight() - (m_StillImage.GetBorderHeight()*2); } 
protected:
  virtual void Activate(bool On);
  void Play(const uchar *Data, int Length);
  bool Wait();
  
  virtual bool Worker(bool bDoIt) = 0;
  // The derived image player can decode frames for the foreground-thread OSD.
  virtual bool DirectOutput(void) const { return false; }
};


#endif
