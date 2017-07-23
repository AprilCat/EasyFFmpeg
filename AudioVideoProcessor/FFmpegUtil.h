#pragma once

#include "AudioVideoProcessor.h"

#ifdef _WIN32
#define snprintf sprintf_s
#endif

#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
extern "C"
{
#endif
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#ifdef __cplusplus
}
#endif

inline struct AVRational avrational(int num, int den)
{
    struct AVRational r;
    r.num = num;
    r.den = den;
    return r;
}

void logPacket(const AVFormatContext* fmtCtx, const AVPacket* pkt);

int openCodecContext(int *streamIdx, const char *srcFileName,
    AVFormatContext *fmtCtx, enum AVMediaType type);

int decodeVideoPacket(AVPacket* pkt, 
    AVCodecContext* videoDecCtx, AVFrame* frame, 
    int width, int height, enum AVPixelFormat pix_fmt, 
    SwsContext* swsCtx, uint8_t* videoDstData[4], int videoDstLinesize[4], 
    int *videoFrameCount, int *gotFrame);

int decodeAudioPacket(AVPacket* pkt, AVCodecContext* audioDecCtx, AVFrame* frame,
    int sampleRate, enum AVSampleFormat sampleFmt, int numChannels, int* numSamples, 
    unsigned char** audioDstData, int* audioDstLineSize,
    int* audioFrameCount, int* gotFrame);

int decodeAudioPacket(AVPacket* pkt, AVCodecContext* audioDecCtx, AVFrame* frame,
    int sampleRate, enum AVSampleFormat sampleFmt, int numChannels, int numSamples,
    unsigned char* audioDstData, int audioDstLineSize,
    int* audioFrameCount, int* gotFrame);

int decodeAudioPacket(AVPacket* pkt, AVCodecContext* audioDecCtx, AVFrame* frame,
    int sampleRate, AVSampleFormat sampleFmt, int numChannels, int* numSamples,
    SwrContext* swrCtx, AVSampleFormat dstSampleFmt, unsigned char* audioDstData[8], int* audioDstLineSize,
    int* gotFrame);

int decodeAudioPacket(AVPacket* pkt, AVCodecContext* audioDecCtx, AVFrame* frame,
    int sampleRate, enum AVSampleFormat sampleFmt, int numChannels, int numSamples,
    SwrContext* swrCtx, AVSampleFormat dstSampleFmt, unsigned char* audioDstData[8],
    int* gotFrame);

int cvtFrameRate(double frameRate, int* frameRateNum, int* frameRateDen);

AVStream* addVideoStream(AVFormatContext* outFmtCtx, const char* codecName, enum AVCodecID codecID, AVDictionary* dict,
    AVPixelFormat pixFmt, int width, int height, int fpsNum, int fpsDen, int gop, int bps, int openCodec);

AVFrame* allocPicture(enum AVPixelFormat pix_fmt, int width, int height);

int writeVideoFrame(AVFormatContext* outFmtCtx, AVStream* stream, const AVFrame* frame);

int writeVideoFrame2(AVFormatContext* outFmtCtx, AVStream* stream, AVCodecContext* codecCtx, const AVFrame* frame);

AVStream* addAudioStream(AVFormatContext* outFmtCtx, const char* codecName, enum AVCodecID codecID, AVDictionary* dict,
        enum AVSampleFormat sampleFormat, int sampleRate, int channelLayout, int bps);

AVFrame* allocAudioFrame(enum AVSampleFormat sampleFormat, int sampleRate, int channeLayout, int numSamples);

int writeAudioFrame(AVFormatContext* outFmtCtx, AVStream* stream, const AVFrame* frame);

const char* getVideoEncodeSpeedString(int videoEncodeSpeed);

void cvtOptions(const std::vector<avp::Option>& src, AVDictionary** dst);

int getSampleTypeNumBytes(int sampleType);

void cvtPlanarToPacked(const unsigned char* const * srcData, int srcSampleFormat, 
    int numChannels, int numSamples, unsigned char* dstData);

void copyAudioData(const unsigned char* const * srcData, unsigned char* dstData, int dstStep,
    int sampleFormat, int numChannels, int numSamples);

void copyAudioData(const unsigned char* srcData, int srcStep, int srcPos, 
    unsigned char* dstData, int dstStep, int dstPos,
    int sampleFormat, int numChannels, int numSamples);

void copyAudioData(const unsigned char* srcData, int srcStep, int srcPos,
    unsigned char** dstData, int dstStep, int dstPos,
    int sampleFormat, int numChannels, int numSamples);

void copyAudioData(const unsigned char** srcData, int srcPos, unsigned char** dstData, int dstPos,
    int sampleFormat, int numChannels, int numSamples);

void setDataPtr(unsigned char* src, int srcStep, int numChannels, int sampleType, unsigned char* dst[8]);

struct H264BitStreamFilterContext 
{
    int32_t  sps_offset;
    int32_t  pps_offset;
    uint8_t  length_size;
    uint8_t  new_idr;
    uint8_t  idr_sps_seen;
    uint8_t  idr_pps_seen;
    int      extradata_parsed;

    uint8_t *spspps_buf;
    uint32_t spspps_size;

    H264BitStreamFilterContext()
    {
        memset(this, 0, sizeof(*this));
    }
    ~H264BitStreamFilterContext()
    {
        if (spspps_buf)
            free(spspps_buf);
    }

    void reset()
    {
        if (spspps_buf)
            free(spspps_buf);
        memset(this, 0, sizeof(*this));
    }
};

int h264_mp4toannexb_filter(H264BitStreamFilterContext *ctx,
    uint8_t *ext_extradata, int32_t ext_extradata_size, const char *args, int *got_output,
    uint8_t **poutbuf, int *poutbuf_size, int *poutbuf_capacity, const uint8_t *buf, int buf_size);