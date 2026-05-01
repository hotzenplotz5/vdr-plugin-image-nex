/*
 * Image plugin to VDR (C++)
 *
 * (C) 2004-2011 Andreas Brachold    <vdr07 at deltab.de>
 * based on (C) 2003 Kai Tobias Burwieck <kai-at-burwieck.net>
 *
 * This code is distributed under the terms and conditions of the
 * GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
 *
 */

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <wait.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include "player-image.h"
#include "control-image.h"
#include "setup-image.h"
#include "data-image.h"
#include "image.h"
#include "list.h"
#include <vdr/i18n.h>

#include "libimage/xpm.h"

//----------cImagePlayer-------------




cImagePlayer::cImagePlayer(cSlideShow *pCurSlideShow)
: cStillImagePlayer(
 ((ImageSetup.m_bLiveAudio != 0)?pmAudioVideo:pmVideoOnly)
,((ImageSetup.m_bUseDeviceStillPicture != 0)?true:false)
)
, m_bConvertRunning(false)
, m_szError(NULL)
, m_nSourceWidth(0)
, m_nSourceHeight(0)
{
  theSlideShow.Assign(pCurSlideShow);
}

bool cImagePlayer::GetIndex(int &nCurrent, int &nTotal, bool /*bSnapToIFrame*/)
{
  // Notify other status-plugins with one picture as one second
  nCurrent = theSlideShow.ImageCurrent(); 
  // Notify other status-plugins count of picture as totalcount of seconds
  nTotal = theSlideShow.ImageTotal();

  return true;
}

cImagePlayer::~cImagePlayer()
{
  // Remove Slideshow
  theSlideShow.Shutdown();
}


void cImagePlayer::Activate(bool bOn)
{
  if(bOn) {
    if(theSlideShow.GetImage())
    {
      cStillImagePlayer::Activate(bOn);
    }
  }
  else
    cStillImagePlayer::Activate(false);
}


bool cImagePlayer::NextImage(int nOffset)
{
  return theSlideShow.NextImage(nOffset);
}



bool cImagePlayer::PrevImage(int nOffset)
{
  return theSlideShow.PrevImage(nOffset);
}


bool cImagePlayer::GotoImage(unsigned int nNewPictureIndex)
{
  return theSlideShow.GotoImage(nNewPictureIndex);
}


bool cImagePlayer::Convert(const char *szChange)
{
  cImageData* pImage = theSlideShow.GetImage();
  if(pImage)
  {
    std::unique_ptr<cDecodeRequest> pCmd(new cDecodeRequest);
    pCmd->bClearBackground = true;
    pCmd->nOffLeft = 0; // OSD offset will be handled in DecodeNative
    pCmd->nOffTop = 0;  // OSD offset will be handled in DecodeNative
    pCmd->nTargetWidth = UseWidth();
    pCmd->nTargetHeight = UseHeight();
    pCmd->nZoomFactor = 0; // No zoom
    pCmd->nCropX = 0;      // No crop
    pCmd->nCropY = 0;      // No crop
    
    int rot = 0;
    if (szChange) {
        if (strcmp(szChange, "right") == 0) rot = 90;
        else if (strcmp(szChange, "rotated") == 0) rot = 180;
        else if (strcmp(szChange, "left") == 0) rot = 270;
    }
    pCmd->nRotationAngle = rot;
  
    pCmd->szSource = strdup(pImage->Name());
  
    Exec(std::move(pCmd));
    return true;
  }
  return false;
}



bool cImagePlayer::ConvertJump(int nOffset)
{
  register unsigned int w,h;
  const unsigned int MAX_BILDER = 9;
  cImageData* pImage[MAX_BILDER];
  for (w = 0; w < MAX_BILDER; ++w)
    pImage[w] = NULL;
  int nBilder = theSlideShow.GetJumpNames(nOffset,pImage,MAX_BILDER);
  if(nBilder > 0 
    && pImage[0]) {
      
    unsigned int nMatrix = (nBilder < 5) ? 2 : 3;
  
    for (h = 0; h < nMatrix; ++h) 
      for (w = 0; w < nMatrix && pImage[(h*nMatrix)+w]; ++w) 
      {
        std::unique_ptr<cDecodeRequest> pCmd(new cDecodeRequest);
      
        pCmd->bClearBackground = (w == 0 && h == 0);  
        pCmd->nTargetWidth = UseWidth() / nMatrix;
        pCmd->nTargetHeight = UseHeight() / nMatrix;
        pCmd->nOffLeft = (pCmd->nTargetWidth * w) + m_StillImage.GetBorderWidth();
        pCmd->nOffTop =  (pCmd->nTargetHeight * h) + m_StillImage.GetBorderHeight();
      
        pCmd->szSource = strdup(pImage[(h*nMatrix)+w]->Name());
        pCmd->szNumber = '0'+((h*nMatrix)+w)+1;

        Exec(std::move(pCmd));
      }
      return true;
  }
  return false;
}


bool cImagePlayer::ConvertZoom(const char *szChange, int nZoomFaktor,
			       int nLeftPos, int nTopPos)
{
  cImageData* pImage = theSlideShow.GetImage();
  if(pImage)
  {
    std::unique_ptr<cDecodeRequest> pCmd(new cDecodeRequest);
    pCmd->bClearBackground = true;
    pCmd->nOffLeft = 0; // OSD offset will be handled in DecodeNative
    pCmd->nOffTop = 0;  // OSD offset will be handled in DecodeNative
    pCmd->nTargetWidth = UseWidth();
    pCmd->nTargetHeight = UseHeight();
    pCmd->nZoomFactor = nZoomFaktor;
    pCmd->nCropX = nLeftPos; // These are pixel offsets in the *zoomed* image
    pCmd->nCropY = nTopPos; // These are pixel offsets in the *zoomed* image

    int rot = 0;
    if (szChange) {
        if (strcmp(szChange, "right") == 0) rot = 90;
        else if (strcmp(szChange, "rotated") == 0) rot = 180;
        else if (strcmp(szChange, "left") == 0) rot = 270;
    }
    pCmd->nRotationAngle = rot;
  
    pCmd->szSource = strdup(pImage->Name());
  
    Exec(std::move(pCmd));
    return true;
  }
  return false;
}

bool cImagePlayer::DecodeNative(cDecodeRequest* pShell)
{
    if(!pShell || pShell->bClearBackground)
      m_StillImage.ClearRGBMem();

    if (!pShell || !pShell->szSource) return false;

    if (pShell->nTargetWidth == 0) pShell->nTargetWidth = 1;
    if (pShell->nTargetHeight == 0) pShell->nTargetHeight = 1;

    AVFormatContext *fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, pShell->szSource, nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) { avformat_close_input(&fmt_ctx); return false; }

    int video_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    if (video_stream_idx == -1) { avformat_close_input(&fmt_ctx); return false; }

    AVCodecParameters *codecpar = fmt_ctx->streams[video_stream_idx]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt_ctx);
        return false;
    }
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        avformat_close_input(&fmt_ctx);
        return false;
    }
    avcodec_parameters_to_context(codec_ctx, codecpar);
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    if (!frame || !pkt) {
        if(frame) av_frame_free(&frame);
        if(pkt) av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }
    bool decoded = false;

    // Decode fully native from source into AVFrame
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            int ret = avcodec_send_packet(codec_ctx, pkt);
            // The official FFmpeg standard requires an inner loop here
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == 0) {
                    decoded = true;
                    break;
                }
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;
            }
        }
        av_packet_unref(pkt);
        if (decoded) break;
    }
    if (!decoded) {
        avcodec_send_packet(codec_ctx, nullptr);
        if (avcodec_receive_frame(codec_ctx, frame) == 0) {
            decoded = true;
        }
    }
    av_packet_free(&pkt);

    if (decoded) {
        // Image Scale and Aspect Ratio logic
        int src_w = frame->width;
        int src_h = frame->height;
        int rot_w = src_w;
        int rot_h = src_h;
        if (pShell->nRotationAngle == 90 || pShell->nRotationAngle == 270) {
            rot_w = src_h;
            rot_h = src_w;
        }

        int crop_x = 0;
        int crop_y = 0;
        int crop_w = rot_w;
        int crop_h = rot_h;

        if (pShell->nZoomFactor > 0) {
            // Store original dimensions for zoom calculations in control
            m_nSourceWidth = rot_w;
            m_nSourceHeight = rot_h;
            // Calculate the crop window in the *original* image dimensions
            // pShell->nCropX and pShell->nCropY are offsets in the *zoomed* image.
            // We need to convert these to offsets in the *original* image for sws_scale.
            crop_x = pShell->nCropX / pShell->nZoomFactor;
            crop_y = pShell->nCropY / pShell->nZoomFactor;

            // The width and height of the crop window in the original image
            // This is the portion of the original image that, when zoomed, fills the target area.
            crop_w = pShell->nTargetWidth / pShell->nZoomFactor;
            crop_h = pShell->nTargetHeight / pShell->nZoomFactor;

            // Ensure crop dimensions don't exceed original image dimensions
            if (crop_x < 0) crop_x = 0;
            if (crop_y < 0) crop_y = 0;
            if (crop_x + crop_w > rot_w) crop_w = rot_w - crop_x;
            if (crop_y + crop_h > rot_h) crop_h = rot_h - crop_y;
            if (crop_w <= 0) crop_w = 1; // Avoid zero dimension
            if (crop_h <= 0) crop_h = 1; // Avoid zero dimension
        }
        // If nZoomFactor is 0, crop_x, crop_y, crop_w, crop_h remain initialized to full image dimensions.
        else {
            m_nSourceWidth = rot_w;
            m_nSourceHeight = rot_h;
            if (crop_w <= 0) crop_w = 1;
            if (crop_h <= 0) crop_h = 1;
        }

        double aspect_src_cropped = (double)crop_w / crop_h;
        double aspect_dst = (double)pShell->nTargetWidth / pShell->nTargetHeight;
        int scaled_w = pShell->nTargetWidth;
        int scaled_h = pShell->nTargetHeight;
        
        // Calculate letterboxing or pillarboxing
        if (aspect_src_cropped > aspect_dst) {
            scaled_h = pShell->nTargetWidth / aspect_src_cropped;
        } else {
            scaled_w = pShell->nTargetHeight * aspect_src_cropped;
        }
        if (scaled_w <= 0) scaled_w = 1;
        if (scaled_h <= 0) scaled_h = 1;

        // Calculate final OSD offsets, including borders and centering
        int osd_offset_x = pShell->nOffLeft + m_StillImage.GetBorderWidth() + (pShell->nTargetWidth - scaled_w) / 2;
        int osd_offset_y = pShell->nOffTop + m_StillImage.GetBorderHeight() + (pShell->nTargetHeight - scaled_h) / 2;

        // Convert to RGB24 full image to safely crop & avoid planar chroma issues
        AVFrame *rgb_frame = av_frame_alloc();
        rgb_frame->format = AV_PIX_FMT_RGB24;
        rgb_frame->width = src_w;
        rgb_frame->height = src_h;
        if (av_frame_get_buffer(rgb_frame, 0) < 0) {
            av_frame_free(&rgb_frame);
            av_frame_free(&frame);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&fmt_ctx);
            return false;
        }

        SwsContext *sws_ctx_rgb = sws_getContext(
            src_w, src_h, (AVPixelFormat)frame->format,
            src_w, src_h, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        if (sws_ctx_rgb) {
            sws_scale(sws_ctx_rgb, frame->data, frame->linesize, 0, src_h, rgb_frame->data, rgb_frame->linesize);
            sws_freeContext(sws_ctx_rgb);
        } else {
            esyslog("imageplugin: failed to initialize SWS RGB context");
            av_frame_free(&rgb_frame);
            av_frame_free(&frame);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&fmt_ctx);
            return false;
        }

        AVFrame *rot_frame = rgb_frame;
        if (pShell->nRotationAngle > 0) {
            rot_frame = av_frame_alloc();
            rot_frame->format = AV_PIX_FMT_RGB24;
            rot_frame->width = rot_w;
            rot_frame->height = rot_h;
            if (av_frame_get_buffer(rot_frame, 0) == 0) {
                int src_linesize = rgb_frame->linesize[0];
                int dst_linesize = rot_frame->linesize[0];
                uint8_t *src_data = rgb_frame->data[0];
                uint8_t *dst_data = rot_frame->data[0];

                if (pShell->nRotationAngle == 90) {
                    for (int y = 0; y < src_h; y++) {
                        uint8_t *src_row = src_data + y * src_linesize;
                        int dst_x = src_h - 1 - y;
                        for (int x = 0; x < src_w; x++) {
                            memcpy(dst_data + x * dst_linesize + dst_x * 3, src_row + x * 3, 3);
                        }
                    }
                } else if (pShell->nRotationAngle == 180) {
                    for (int y = 0; y < src_h; y++) {
                        uint8_t *src_row = src_data + y * src_linesize;
                        int dst_y = src_h - 1 - y;
                        uint8_t *dst_row = dst_data + dst_y * dst_linesize;
                        for (int x = 0; x < src_w; x++) {
                            int dst_x = src_w - 1 - x;
                            memcpy(dst_row + dst_x * 3, src_row + x * 3, 3);
                        }
                    }
                } else if (pShell->nRotationAngle == 270) {
                    for (int y = 0; y < src_h; y++) {
                        uint8_t *src_row = src_data + y * src_linesize;
                        int dst_x = y;
                        for (int x = 0; x < src_w; x++) {
                            int dst_y = src_w - 1 - x;
                            memcpy(dst_data + dst_y * dst_linesize + dst_x * 3, src_row + x * 3, 3);
                        }
                    }
                }
                av_frame_free(&rgb_frame);
            } else {
                av_frame_free(&rot_frame);
                rot_frame = rgb_frame; // Fallback to unrotated if out of memory
            }
        }

        SwsContext *sws_ctx = sws_getContext(
            crop_w, crop_h, AV_PIX_FMT_RGB24,
            scaled_w, scaled_h, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        if (sws_ctx) {
            int linesize[4] = { (int)m_StillImage.GetWidth() * 3, 0, 0, 0 };
            uint8_t *dest[4] = { m_StillImage.GetRGBMem() + (osd_offset_y * m_StillImage.GetWidth() + osd_offset_x) * 3, nullptr, nullptr, nullptr };

            // Perform scaling and cropping
            uint8_t *src_slice_ptr[AV_NUM_DATA_POINTERS] = {0};
            src_slice_ptr[0] = rot_frame->data[0] + crop_y * rot_frame->linesize[0] + crop_x * 3;

            sws_scale(sws_ctx, src_slice_ptr, rot_frame->linesize, 0, crop_h, dest, linesize);
            sws_freeContext(sws_ctx);
        } else {
            esyslog("imageplugin: failed to initialize SWS scaling context");
            av_frame_free(&rot_frame);
            av_frame_free(&frame);
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&fmt_ctx);
            return false;
        }
        av_frame_free(&rot_frame);

        if(pShell->szNumber && ImageSetup.m_bShowNumbers) {
            cXPM::Overlay(pShell->szNumber, m_StillImage.GetRGBMem(),
              m_StillImage.GetWidth(), m_StillImage.GetHeight(),
              cXPM::TopRight, osd_offset_x, osd_offset_y, scaled_w, scaled_h);
        }
    }

    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    return decoded;
}

////////////////////////////////////////////////////////////////////////////////
/** void cImagePlayer::ExecFailed()
Show a precompiled Image if Exec can't find a converted Image
@param  - char* szErr
@return - nothing */
void cImagePlayer::ExecFailed(cDecodeRequest* pShell,const char* szErr)
{
    if(!pShell || pShell->bClearBackground)
      m_StillImage.ClearRGBMem();
    
    // Merge Errorimage with Encoder-Memory
    if(pShell && pShell->szNumber)
      cXPM::Overlay('s',m_StillImage.GetRGBMem(),
          m_StillImage.GetWidth(),m_StillImage.GetHeight(),
          cXPM::Center,pShell->nOffLeft,pShell->nOffTop,pShell->nTargetWidth,pShell->nTargetHeight);
    else
      cXPM::Error(m_StillImage.GetRGBMem(),
        m_StillImage.GetWidth(),m_StillImage.GetHeight());


  // Store Message for OSD-Thread
  {
    cMutexLock lock(&m_MutexErr);
      if(m_szError)
        free(m_szError);
      m_szError = strdup(szErr);
  }
}

////////////////////////////////////////////////////////////////////////////////
/** void cImagePlayer::ErrorMsg()
ThreadSafe Method to show messages from Worker thread,
this functions is called only from cImageControl::ProcessKey(kNone)
@return - nothing */
void cImagePlayer::ErrorMsg()
{
  char* szErr = nullptr;
  {
    cMutexLock lock(&m_MutexErr);
    szErr = m_szError;
    m_szError = nullptr;
  }
  if(szErr)
  {  
    OSD_ErrorMsg(szErr);
    free(szErr);
  }
}

const char* cImagePlayer::FileName(void) const
{ 
  cImageData* pImage = theSlideShow.GetImage();
  return pImage?pImage->Name():nullptr;
}

void cImagePlayer::Exec(std::unique_ptr<cDecodeRequest> pCmd)
{
  if(pCmd->szSource)
    m_bConvertRunning = true;
  if(pCmd) {
    cMutexLock lock(&m_Mutex);
    m_queue.add(std::move(pCmd)); // if full, pCmd is automatically deleted when out of scope
  }
}

bool cImagePlayer::Worker(bool bDoIt)
{
  bool bQueueEmpty;
  std::unique_ptr<cDecodeRequest> pShell;
  
  { // Protect the queue ++
    cMutexLock lock(&m_Mutex);
    if(bDoIt && !m_queue.empty()) {
      pShell = std::move(m_queue.front());
      m_queue.erase(m_queue.begin());
    }
    bQueueEmpty = m_queue.empty();
  } // ++

  if(!pShell) 
  {
    m_bConvertRunning = m_StillImage.EncodeRequired();
    return bQueueEmpty;
  }  

  if(pShell->szSource) {
    if(DecodeNative(pShell.get())) {
        m_StillImage.EncodeRequired(true);
    } else {
        esyslog("imageplugin: native decoding failed for '%s'", pShell->szSource);
        ExecFailed(pShell.get(), tr("Image couldn't load"));        
        m_StillImage.EncodeRequired(true);
    }
  } 
  return bQueueEmpty;
}
