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
 
#ifndef _ENCODE_H
#define _ENCODE_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include "../setup-image.h"
//#define TESTCODE

class cEncode
{
    static const AVCodec *m_pavCodec;
    unsigned int m_nMaxMPEGSize;
    uint8_t *m_pImageFilled; 
    uint8_t *m_pImageYUV;

protected:
    const unsigned int m_nNumberOfFramesToEncode;
    uint8_t *m_pMPEG; 
    uint8_t *m_pImageRGB;
    bool m_bUsePAL;
    unsigned int m_nMPEGSize;
    unsigned int m_nWidth;
    unsigned int m_nHeight;
    unsigned int *m_pFrameSizes;

public:
    cEncode(unsigned int nNumberOfFramesToEncode);
    virtual ~cEncode();

    /*Load Shared Library and Register Codec*/
    static bool Register();
    /*UnLoad Shared*/
    static void UnRegister();

    bool Encode();
    inline const uint8_t *Data() const       { return m_pMPEG; }
    inline unsigned int Size() const         { return m_nMPEGSize; }
    inline unsigned int GetFrames() const    { return m_nNumberOfFramesToEncode; } 
    inline unsigned int GetFrameRate() const { return m_bUsePAL?25:30; } 

    inline unsigned int GetFrameSize(unsigned int nframeNumber) const 
      { return *(m_pFrameSizes + nframeNumber); } 

    inline unsigned int GetHeight() const       { return m_nHeight; } 
    inline unsigned int GetWidth() const        { return m_nWidth;  } 
    inline unsigned int GetBorderHeight() const { return ImageSetup.m_nBorderHeight; } 
    inline unsigned int GetBorderWidth() const  { return ImageSetup.m_nBorderWidth; } 

    inline uint8_t *GetRGBMem() { return m_pImageRGB; }
    void  ClearRGBMem ();

#ifdef TESTCODE
    bool Load(const char* szFileName);
    bool Save(const char* szFileName) const;
#endif

private:
    void AllocateBuffers();
    void ReleaseBuffers();
    void SetupEncodingParameters(AVCodecContext *context);
    bool ConvertImageToFrame(AVFrame *frame);
    bool EncodeFrames(AVCodecContext *context, AVFrame *frame);
};

#endif /* _ENCODE_H */
