#include "AudioVideoProcessorUtil.h"
#include "FFmpegUtil.h"

namespace avp
{
AudioFrameAdaptor::AudioFrameAdaptor()
{
    clear();
}

void AudioFrameAdaptor::clear()
{
    for (int i = 0; i < 8; i++)
    {
        srcData[i] = 0;
        dstData[i] = 0;
    }
    sampleType = SampleTypeUnknown;
    elemNumBytes = 0;
    numChannels = 0;
    srcNumSamples = 0;
    dstNumSamples = 0;
    sampleRate = 0;
    srcTimeStamp = -1;
    dstTimeStamp = -1;
    srcPos = 0;
    dstPos = 0;
    setSrcSuccess = 0;
    setDstSuccess = 0;
}

bool AudioFrameAdaptor::setDstInfo(unsigned char* data_[8], int sampleRate_, int sampleType_, int numChannels_, int numSamples_)
{
    clear();

    if (sampleType_ == AV_SAMPLE_FMT_NONE)
        return false;

    sampleRate = sampleRate_;
    sampleType = sampleType_;
    elemNumBytes = getSampleTypeNumBytes(sampleType);
    dstNumSamples = numSamples_;
    numChannels = numChannels_;
    for (int i = 0; i < 8; i++)
    {
        if (i < numChannels)
            dstData[i] = data_[i];
        else
            dstData[i] = 0;
    }
    dstPos = 0;

    setDstSuccess = 1;
    return true;
}

bool AudioFrameAdaptor::setSrcInfo(unsigned char* data_[8], int sampleRate_, int sampleType_, 
    int numChannels_, int numSamples_, long long int timeStamp_)
{
    if (sampleRate != sampleRate_ || sampleType != sampleType_ || numChannels != numChannels_)
    {
        setSrcSuccess = 0;
        return false;
    }

    for (int i = 0; i < 8; i++)
    {
        if (i < numChannels)
            srcData[i] = data_[i];
        else
            srcData[i] = 0;
    }
    srcPos = 0;
    srcNumSamples = numSamples_;
    srcTimeStamp = timeStamp_;
    setSrcSuccess = 1;
    return true;
}

bool AudioFrameAdaptor::getFullDstFrame()
{
    if (!setDstSuccess || !setSrcSuccess)
        return false;

    int srcLeft = srcNumSamples - srcPos;
    int dstLeft = dstNumSamples - dstPos;

    if (srcLeft == 0)
        return false;

    if (srcLeft < dstLeft)
    {
        copyAudioData((const unsigned char**)srcData, srcPos, dstData, dstPos, sampleType, numChannels, srcLeft);
        srcPos += srcLeft;
        dstPos += srcLeft;
        return false;
    }

    copyAudioData((const unsigned char**)srcData, srcPos, dstData, dstPos, sampleType, numChannels, dstLeft);
    dstPos = 0;
    srcPos += dstLeft;
    dstTimeStamp = srcTimeStamp + double(srcPos - dstNumSamples) / sampleRate * 1000000 + 0.5;
    return true;
}

long long int AudioFrameAdaptor::getLastFullDstFrameTimeStamp() const
{
    return dstTimeStamp;
}

}