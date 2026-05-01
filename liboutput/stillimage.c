/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold    <vdr07 at deltab.de>
 *  Created: Thu Aug  5 2004
 *
 *  parts of the code (c) Peter Seyringer 
 *                    (c) Marcel Wiesweg
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#include <string.h>
#include <unistd.h>

#include "stillimage.h"
#include "stillimage-player.h"
#include <vdr/plugin.h>


cStillImage::cStillImage(cStillImagePlayer *pl,bool bUseDeviceStillPicture) 
: cEncode(bUseDeviceStillPicture ? 2 : 4)
, player(pl)
, m_bEncodeRequired(false)
, m_bUseDeviceStillPicture(bUseDeviceStillPicture)
{

}

cStillImage::~cStillImage() 
{

}

void cStillImage::Stop()
{
   Cancel(3);
}

void cStillImage::Action(void) 
{
  bool bMPEGValid = false;
  bool bQueueEmpty = false;
  bool bFreeze = true;
  unsigned int nFrame = 0,nFrameOff=0;
  int nMircoSekWait;
  while (Running()) {      
      
      nMircoSekWait = 10000;
      bQueueEmpty = player->Worker(false);
      if(!bQueueEmpty)
      {
        if(!bFreeze)
        {  
          player->DeviceFreeze();
          bFreeze = true;
        }
        player->Worker(true);
      }
      if(m_bEncodeRequired 
        && bQueueEmpty)
      {        
        //convert data to MPEG      
        Lock();
        bMPEGValid = Encode();
        Unlock();
      
        if (!Running())
           break;
        m_bEncodeRequired = false;
        nFrame = 0;
        nFrameOff=0;
        if(bFreeze)
        {  
          player->DeviceClear();
          player->DevicePlay();
          bFreeze = false;
        }

      }      

      
      if(bMPEGValid 
        && bQueueEmpty)
      {  
        /* Methode A ************************************************************/
        /*timeval timenow,diff,timestart;
        gettimeofday(&timestart, 0);
        
        for (int i=1;i<=25 && player->active;i++) {
           gettimeofday(&timenow, 0);
           timersub(&timenow, &timestart, &diff);
           BuildPesPacket(Data(), Size(),(int)(diff.tv_sec*90000.0 + (diff.tv_usec*90000.0)/1000000.0));
           player->Wait();
        }*/
      
        /* Methode B ************************************************************/
        //for (int i=1;i<= 25 && player->m_bActive;i++) {
        //   BuildPesPacket(Data(), Size(),i);
        //}
      
        /* Methode C ************************************************************/
        //BuildPesPacket(Data(), Size(),1);
        
        if(m_bUseDeviceStillPicture) {  
            player->DeviceStillPicture(Data(), Size());
            if(!bFreeze)
            {  
              player->DeviceFreeze();
              bFreeze = true;
            }
            bMPEGValid = false;
        } else {  

          unsigned int nFrameSize = GetFrameSize(nFrame);
          if(nFrameSize) // Skip empty Frames
          {  
            BuildPesPacket(Data() + nFrameOff, nFrameSize,1);
            nFrameOff += nFrameSize;
          }
          if(++nFrame>=GetFrames())
          {
            nFrame = 0; 
            nFrameOff = 0; 
          }      
        }
      
        nMircoSekWait = (1000000/(GetFrameRate()*4)); // Wait duration off 1/4 frame
        
        if (!Running())
           break;
      }
      //Reduce CPU load!!!
      usleep(max(10000,nMircoSekWait));
   }
}


//taken from mp1osd.c
void cStillImage::BuildPesPacket(const unsigned char *data, int len, int timestamp) {
#define PES_MAX_SIZE 2048
    int ptslen = timestamp ? 5 : 1;
    unsigned char pes_header[PES_MAX_SIZE]; // Removed 'static' for thread-safety!

    // startcode:
    pes_header[0] = pes_header[1] = 0;
    pes_header[2] = 1;
    pes_header[3] = 0xe0;
    int hdr;
    int type = 1;
    if (cPluginManager::GetPlugin("reelbox") != NULL) type = 2;
    if (cPluginManager::GetPlugin("softhddevice") != NULL) type = 2;
    if (cPluginManager::GetPlugin("softhdcuvid") != NULL) type = 2;
    if (cPluginManager::GetPlugin("softhdvaapi") != NULL) type = 2;

    while (len > 0)
    {
        int payload_size = len; // data + PTS

        if (type == 2) hdr = 3;
        else hdr = ptslen == 5 ? 0 : 1;

        if (6 + ptslen + payload_size + hdr > PES_MAX_SIZE)
            payload_size = PES_MAX_SIZE - (6 + ptslen + hdr);

        // construct PES header:  (code from ffmpeg's libav)
        // packetsize:
        pes_header[4] = (hdr + ptslen + payload_size) >> 8;
        pes_header[5] = (hdr + ptslen + payload_size) & 255;

        if (ptslen == 5)
        {
           if (type == 2){
                pes_header[6] = 0x81;
                pes_header[7] = 0x80;
                pes_header[8] = ptslen;
            }
            int x;

            // presentation time stamp:
            x = (0x02 << 4) | (((timestamp >> 30) & 0x07) << 1) | 1;
            pes_header[6+hdr] = x;
            x = ((((timestamp >> 15) & 0x7fff) << 1) | 1);
            pes_header[7+hdr] = x >> 8;
            pes_header[8+hdr] = x & 255;
            x = ((((timestamp) & 0x7fff) << 1) | 1);
            pes_header[9+hdr] = x >> 8;
            pes_header[10+hdr] = x & 255;
        }
        else
        {
            // stuffing and header bits:
            pes_header[6] = 0x0f;
            if (type == 2){
                pes_header[6] = 0x81;
                pes_header[7] = 0x00;
                pes_header[8] = 0x00;
            }
        }

        memcpy (&pes_header[6 + ptslen + hdr], data, payload_size);
        player->Wait();
        player->Play(pes_header, 6 + ptslen + hdr + payload_size);

        len -= payload_size;
        data += payload_size;
//        ptslen = 1;             // store PTS only once, at first packet! - not need???!
 
    }
}
