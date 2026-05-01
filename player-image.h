/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold    <vdr07 at deltab.de>
 *     2003 Kai Tobias Burwieck <kai-at-burwieck.net>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#ifndef ___DVB_IMAGE_H
#define ___DVB_IMAGE_H

#include <vector>
#define __STL_CONFIG_H

#include <memory>
#include <vdr/thread.h>
#include <vdr/player.h>
#include <liboutput/stillimage-player.h>

class cImageData;
class cSlideShow;    

struct cDecodeRequest {
    
  char* szSource;
  char  szNumber;
  unsigned int nOffLeft;
  unsigned int nOffTop;
  unsigned int nTargetWidth; // Target width on OSD
  unsigned int nTargetHeight; // Target height on OSD
  bool bClearBackground;
  // Zoom/Crop parameters
  int nZoomFactor; // 0 for no zoom, >0 for zoom level
  int nCropX;      // X offset for cropping in original image pixels (or zoomed image pixels for zoom mode)
  int nCropY;      // Y offset for cropping in original image pixels (or zoomed image pixels for zoom mode)
  int nRotationAngle; // 0, 90, 180, 270

  cDecodeRequest()
  : szSource(nullptr) // Initialize all members
  , szNumber('\0')
  , nOffLeft(0)
  , nOffTop(0)
  , nTargetWidth(0)
  , nTargetHeight(0)
  , bClearBackground(false)
  , nZoomFactor(0)
  , nCropX(0)
  , nCropY(0)
  , nRotationAngle(0)
  {
  }

  virtual ~cDecodeRequest() {
    if(szSource)
      free(szSource);
  }
};

struct cDecodeRequestQueue
 : public std::vector<std::unique_ptr<cDecodeRequest>>
{
  virtual ~cDecodeRequestQueue()
  {
    clear();
  }

  inline size_t max_size() const 
  {
    return 64;
  }

  inline bool add(std::unique_ptr<cDecodeRequest> pCmd) {
    if(size()<max_size())
    {
      if(nullptr == pCmd->szSource || !pCmd->bClearBackground) //Pregeneration or Index
        push_back(std::move(pCmd));
      else { 
        // Remove all other viewed images from queue
        iterator i = begin();
        while(end()!=i) {
          if((*i)->szSource) {
            i = erase(i);
          }
          else
            ++i;
          }
        //Place next viewed image at front of the queue
        insert(begin(),std::move(pCmd));
      }
      return true;
    }  
    return false;
  }
  
};

class cImagePlayer
: public cStillImagePlayer
{
    
  volatile bool               m_bConvertRunning;

  cMutex                      m_Mutex;
  cDecodeRequestQueue         m_queue;
  cMutex                      m_MutexErr;
  char*                       m_szError;
  // Store dimensions of the last decoded source image
  int                         m_nSourceWidth;
  int                         m_nSourceHeight;
protected:
  void Exec(std::unique_ptr<cDecodeRequest> pCmd);

  bool DecodeNative(cDecodeRequest* pShell);
  /** Show Errorimage if operation failed*/
  void ExecFailed(cDecodeRequest* pShell,const char* szErr);

  virtual void Activate(bool On);
  virtual bool Worker(bool bDoIt);
public:

  cImagePlayer(cSlideShow *pCurSlideShow);
  virtual ~ cImagePlayer();
  bool IsConvertRunning() const { return m_bConvertRunning; };
  bool NextImage(int Step);
  bool PrevImage(int Step);
  bool GotoImage(unsigned int nNewPictureIndex);
  
  /** Deliver the filename from the current number of viewed Image */
  const char* FileName(void) const;

  bool Convert(const char *szChange);
  bool ConvertZoom(const char *szChange, int nZoomFaktor,
			       int nLeftPos, int nTopPos);
  bool ConvertJump(int Step);
  
  /** Returns the current and total frame index, optionally snapped to the
  nearest I-frame.*/
  virtual bool GetIndex(int &Current, int &Total, bool SnapToIFrame);
  /** ThreadSafe Method to show messages from Worker thread*/
  void ErrorMsg();

  int SourceWidth() const { return m_nSourceWidth; }
  int SourceHeight() const { return m_nSourceHeight; }
};

#endif				//__DVB_IMAGE_H
