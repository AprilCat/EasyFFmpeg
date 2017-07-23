#include "AudioVideoProcessor.h"

#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
extern "C"
{
#endif
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libavutil/mem.h>
#ifdef __cplusplus
}
#endif

static const int ptrAlignSize = 128;
static const int stepAlignSize = 128;

int alignedAllocImage(unsigned char** data, int* step, int width, int height, int pixelType)
{
    if (!data || !step || width <= 0 || height <= 0 || 
        (pixelType != avp::PixelTypeBGR24 && pixelType != avp::PixelTypeBGR32))
        return -1;

    *step = (width * (pixelType == avp::PixelTypeBGR24 ? 3 : 4) + stepAlignSize - 1) / stepAlignSize * stepAlignSize;
    *data = (unsigned char*)_aligned_malloc(*step * height, ptrAlignSize);
    return 0;
}

namespace avp
{

AudioVideoFrame2::AudioVideoFrame2(unsigned char** data_, int* steps_, int mediaType_,
    int pixelType_, int width_, int height_,
    int sampleType_, int numChannels_, int channelLayout_,
    int numSamples_, long long int timeStamp_, int frameIndex_)
    : mediaType(mediaType_),
    pixelType(pixelType_), width(width_), height(height_),
    sampleType(sampleType_), numChannels(numChannels_), channelLayout(channelLayout_),
    numSamples(numSamples_), timeStamp(timeStamp_), frameIndex(frameIndex_)
{
    memset(data, 0, sizeof(data));
    memset(steps, 0, sizeof(steps));
    if (data_)
    {
        if (mediaType == avp::VIDEO)
            memcpy(data, data_, 4 * sizeof(unsigned char*));
        else if (mediaType == avp::AUDIO)
            memcpy(data, data_, 8 * sizeof(unsigned char*));
    }
    if (steps_)
    {
        if (mediaType == avp::VIDEO)
            memcpy(steps, steps_, 4 * sizeof(int));
        else if (mediaType == avp::AUDIO)
            steps[0] = *steps_;
    }
}

AudioVideoFrame2::AudioVideoFrame2(unsigned char** data_, int step_, int sampleType_, int numChannels_, int channelLayout_,
    int numSamples_, long long int timeStamp_, int frameIndex_)
    : mediaType(AUDIO),
    pixelType(PixelTypeUnknown), width(0), height(0),
    sampleType(sampleType_), numChannels(numChannels_), channelLayout(channelLayout_),
    numSamples(numSamples_), timeStamp(timeStamp_), frameIndex(frameIndex_)
{
    memset(data, 0, sizeof(data));
    memset(steps, 0, sizeof(steps));
    if (data_)
        memcpy(data, data_, 8 * sizeof(unsigned char*));
    steps[0] = step_;
}

AudioVideoFrame2::AudioVideoFrame2(unsigned char** data_, int* steps_, int pixelType_, int width_, int height_,
    long long int timeStamp_, int frameIndex_)
    : mediaType(VIDEO),
    pixelType(pixelType_), width(width_), height(height_),
    sampleType(SampleTypeUnknown), numChannels(0), channelLayout(0),
    numSamples(0), timeStamp(timeStamp_), frameIndex(frameIndex_)
{
    memset(data, 0, sizeof(data));
    memset(steps, 0, sizeof(steps));
    if (data_)
        memcpy(data, data_, 4 * sizeof(unsigned char*));
    if (steps_)
        memcpy(steps, steps_, 4 * sizeof(int));
}

AudioVideoFrame2::AudioVideoFrame2(int sampleType_, int numChannels_, int channelLayout_, int numSamples_,
    long long int timeStamp_, int frameIndex_)
    : mediaType(UNKNOWN),
    pixelType(PixelTypeUnknown), width(0), height(0),
    sampleType(SampleTypeUnknown), numChannels(0), channelLayout(0),
    numSamples(0), timeStamp(-1LL), frameIndex(-1)
{
    create(sampleType_, numChannels_, channelLayout_, numSamples_, timeStamp_, frameIndex_);
}

AudioVideoFrame2::AudioVideoFrame2(int pixelType_, int width_, int height_,
    long long int timeStamp_, int frameIndex_)
    : mediaType(UNKNOWN),
    pixelType(PixelTypeUnknown), width(0), height(0),
    sampleType(SampleTypeUnknown), numChannels(0), channelLayout(0),
    numSamples(0), timeStamp(-1LL), frameIndex(-1)
{
    create(pixelType_, width_, height_, timeStamp_, frameIndex_);
}

bool AudioVideoFrame2::create(int sampleType_, int numChannels_, int channelLayout_, int numSamples_,
    long long int timeStamp_, int frameIndex_)
{
    if (mediaType == AUDIO && sampleType == sampleType_ && numChannels == numChannels_ &&  numSamples == numSamples_)
    {
        channelLayout = channelLayout_;
        timeStamp = timeStamp_;
        frameIndex = frameIndex_;
        return true;
    }

    release();

    mediaType = AUDIO;
    timeStamp = timeStamp_;
    frameIndex = frameIndex_;
    sampleType = sampleType_;
    numChannels = numChannels_;
    channelLayout = channelLayout_;
    numSamples = numSamples_;

    int step;
    int bufSize = av_samples_get_buffer_size(&step, numChannels,
        numSamples, (enum AVSampleFormat)sampleType, 0);
    unsigned char* rawPtr = (unsigned char*)av_malloc(bufSize);
    if (!rawPtr)
    {
        release();
        return false;
    }

    memset(data, 0, 8 * sizeof(unsigned char*));
    memset(steps, 0, 8 * sizeof(int));
    data[0] = rawPtr;
    steps[0] = step;
    if (sampleType >= SampleType8UP)
    {
        for (int i = 1; i < numChannels; i++)
            data[i] = rawPtr + i * step;
    }
    sdata.reset(rawPtr, av_free);

    return true;
}

bool AudioVideoFrame2::create(int pixelType_, int width_, int height_, long long int timeStamp_, int frameIndex_)
{
    if (mediaType == VIDEO && pixelType == pixelType_ && width == width_ && height == height_)
    {
        timeStamp = timeStamp_;
        frameIndex = frameIndex_;
        return true;
    }

    release();

    mediaType = VIDEO;
    timeStamp = timeStamp_;
    frameIndex = frameIndex_;
    pixelType = pixelType_;
    width = width_;
    height = height_;

    if (pixelType == PixelTypeBGR24 || pixelType == PixelTypeBGR32)
    {
        unsigned char* tempData;
        int tempStep;
        if (alignedAllocImage(&tempData, &tempStep, width, height, pixelType))
        {
            release();
            return false;
        }

        memset(data, 0, 8 * sizeof(unsigned char*));
        memset(steps, 0, 8 * sizeof(int));
        data[0] = tempData;
        steps[0] = tempStep;
        sdata.reset(tempData, _aligned_free);
        return true;
    }

    unsigned char* tempData[4] = { 0 };
    int tempSteps[4] = { 0 };
    if (av_image_alloc(tempData, tempSteps, width, height, (AVPixelFormat)pixelType, 16) < 0)
    {
        release();
        return false;
    }
    memset(data, 0, 8 * sizeof(unsigned char*));
    memset(steps, 0, 8 * sizeof(int));
    for (int i = 0; i < 4; i++)
    {
        data[i] = tempData[i];
        steps[i] = tempSteps[i];
    }
    sdata.reset(data[0], av_free);
    return true;
}

bool AudioVideoFrame2::copyTo(AudioVideoFrame2& frame) const
{
    if (mediaType == UNKNOWN)
    {
        frame.release();
        return false;
    }
    else if (mediaType == VIDEO)
    {
        if (!frame.create(pixelType, width, height, timeStamp, frameIndex))
            return false;
        av_image_copy(frame.data, frame.steps, (const unsigned char**)data, steps, (AVPixelFormat)pixelType, width, height);
        return true;
    }
    else if (mediaType == AUDIO)
    {
        if (!frame.create(sampleType, numChannels, channelLayout, numSamples, timeStamp, frameIndex))
            return false;
        av_samples_copy(frame.data, (unsigned char* const *)data, 0, 0, numSamples, numChannels, (AVSampleFormat)sampleType);
        return true;
    }
    return false;
}

AudioVideoFrame2 AudioVideoFrame2::clone() const
{
    AudioVideoFrame2 frame;
    if (mediaType == VIDEO || mediaType == AUDIO)
        copyTo(frame);
    return frame;
}

void AudioVideoFrame2::release()
{
    sdata.reset();
    memset(data, 0, 8 * sizeof(unsigned char*));
    memset(steps, 0, 8 * sizeof(int));
    mediaType = UNKNOWN;
    pixelType = PixelTypeUnknown;
    width = 0;
    height = 0;
    sampleType = SampleTypeUnknown;
    numChannels = 0;
    channelLayout = 0;
    numSamples = 0;
    timeStamp = -1LL;
    frameIndex = -1;
}

SharedAudioVideoFrame::SharedAudioVideoFrame() :
    data(0), step(0), mediaType(UNKNOWN), pixelType(PixelTypeUnknown), width(0), height(0),
    sampleType(SampleTypeUnknown), numChannels(0), channelLayout(0),
    numSamples(0), timeStamp(-1), frameIndex(-1)
{

}

SharedAudioVideoFrame::SharedAudioVideoFrame(const AudioVideoFrame& frame) :
    mediaType(frame.mediaType), pixelType(frame.pixelType), width(frame.width), height(frame.height),
    sampleType(frame.sampleType), numChannels(frame.numChannels), channelLayout(frame.channelLayout),
    numSamples(frame.numSamples), timeStamp(frame.timeStamp), frameIndex(frame.frameIndex)
{
    if (mediaType == AUDIO)
    {
        int bufSize = av_samples_get_buffer_size(&step, numChannels,
            numSamples, (enum AVSampleFormat)sampleType, 0);
        unsigned char* rawPtr = (unsigned char*)av_malloc(bufSize);
        if (sampleType < SampleType8UP)
            memcpy(rawPtr, frame.data, numChannels * numSamples * av_get_bytes_per_sample((enum AVSampleFormat)sampleType));
        else
        {
            for (int i = 0; i < numChannels; i++)
            {
                memcpy(rawPtr + i * step, frame.data + i * frame.step,
                    numSamples * av_get_bytes_per_sample((enum AVSampleFormat)sampleType));
            }
        }
        data = rawPtr;
        sharedData.reset(rawPtr, av_free);
    }
    else if (mediaType == VIDEO)
    {
        //unsigned char* bgrData[4];
        //int bgrLineSize[4];
        //if (pixelType == PixelTypeBGR24)
        //{
        //    av_image_alloc(bgrData, bgrLineSize, width, height, AV_PIX_FMT_BGR24, 16);
        //    for (int i = 0; i < height; i++)
        //        memcpy(bgrData[0] + i * bgrLineSize[0], frame.data + i * frame.step, width * 3);
        //}            
        //else if (pixelType == PixelTypeBGR32)
        //{
        //    av_image_alloc(bgrData, bgrLineSize, width, height, AV_PIX_FMT_BGR0, 16);
        //    for (int i = 0; i < height; i++)
        //        memcpy(bgrData[0] + i * bgrLineSize[0], frame.data + i * frame.step, width * 4);
        //}
        //data = bgrData[0];
        //step = bgrLineSize[0];
        //sharedData.reset(bgrData[0], av_free);
        if (pixelType == PixelTypeBGR24)
        {
            alignedAllocImage(&data, &step, width, height, PixelTypeBGR24);
            for (int i = 0; i < height; i++)
                memcpy(data + i * step, frame.data + i * frame.step, width * 3);
        }
        else if (pixelType == PixelTypeBGR32)
        {
            alignedAllocImage(&data, &step, width, height, PixelTypeBGR32);
            for (int i = 0; i < height; i++)
                memcpy(data + i * step, frame.data + i * frame.step, width * 4);
        }
        sharedData.reset(data, _aligned_free);
    }
}

SharedAudioVideoFrame::operator AudioVideoFrame() const
{
    if (mediaType == AUDIO)
        return audioFrame(data, step, sampleType, numChannels, channelLayout, numSamples, timeStamp, frameIndex);
    else if (mediaType == VIDEO)
        return videoFrame(data, step, pixelType, width, height, timeStamp, frameIndex);
    else
        return AudioVideoFrame();
}

SharedAudioVideoFrame sharedAudioFrame(int sampleType_, int numChannels_, int channelLayout_, 
    int numSamples_, long long int timeStamp_, int frameIndex_)
{
    SharedAudioVideoFrame frame;
    frame.mediaType = AUDIO;
    frame.sampleType = sampleType_;
    frame.numChannels = numChannels_;
    frame.channelLayout = channelLayout_;
    frame.numSamples = numSamples_;
    frame.timeStamp = timeStamp_;
    frame.frameIndex = frameIndex_;
    int bufSize = av_samples_get_buffer_size(&frame.step, frame.numChannels,
        frame.numSamples, (enum AVSampleFormat)frame.sampleType, 0);
    frame.data = (unsigned char*)av_malloc(bufSize);
    if (!frame.data)
        return SharedAudioVideoFrame();
    frame.sharedData.reset(frame.data, av_free);
    return frame;
}

SharedAudioVideoFrame sharedVideoFrame(int pixelType_, int width_, int height_, long long int timeStamp_, int frameIndex_)
{
    SharedAudioVideoFrame frame;
    frame.mediaType = VIDEO;
    frame.pixelType = pixelType_;
    frame.width = width_;
    frame.height = height_;
    frame.timeStamp = timeStamp_;
    frame.frameIndex = frameIndex_;
    int ret = alignedAllocImage(&frame.data, &frame.step, width_, height_, pixelType_);
    if (ret < 0)
        return SharedAudioVideoFrame();
    frame.sharedData.reset(frame.data, _aligned_free);
    return frame;
}

bool copy(const AudioVideoFrame& src, SharedAudioVideoFrame& dst)
{
    if (src.mediaType == AUDIO)
    {
        if (src.sampleType != dst.sampleType || src.numChannels != dst.numChannels ||
            src.channelLayout != dst.channelLayout || src.numSamples != dst.numChannels)
            dst = SharedAudioVideoFrame(src);
        else
        {
            dst.timeStamp = src.timeStamp;
            if (src.sampleType < SampleType8UP)
                memcpy(dst.data, src.data, src.numChannels * src.numSamples * av_get_bytes_per_sample((enum AVSampleFormat)src.sampleType));
            else
            {
                for (int i = 0; i < src.numChannels; i++)
                {
                    memcpy(dst.data + i * dst.step, src.data + i * src.step,
                        src.numSamples * av_get_bytes_per_sample((enum AVSampleFormat)src.sampleType));
                }
            }
        }
        return true;
    }
    else if (src.mediaType == VIDEO)
    {
        if (src.pixelType != dst.pixelType || src.width != dst.width || src.height != dst.height)
            dst = SharedAudioVideoFrame(src);
        else
        {
            dst.timeStamp = src.timeStamp;
            if (src.pixelType == PixelTypeBGR24)
            {
                for (int i = 0; i < src.height; i++)
                    memcpy(dst.data + i * dst.step, src.data + i * src.step, src.width * 3);
            }
            else if (src.pixelType == PixelTypeBGR32)
            {
                for (int i = 0; i < src.height; i++)
                    memcpy(dst.data + i * dst.step, src.data + i * src.step, src.width * 4);
            }
        }
        return true;
    }
    else
        return false;
}

}