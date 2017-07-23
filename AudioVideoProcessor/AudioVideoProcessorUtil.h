#pragma once

#include "AudioVideoProcessor.h"
#include <memory>

namespace avp
{

struct AudioFrameAdaptor
{
    AudioFrameAdaptor();
    void clear();
    bool setDstInfo(unsigned char* data[8], int sampleRate, int sampleType, int numChannels, int numSamples);
    bool setSrcInfo(unsigned char* data[8], int sampleRate, int sampleType, int numChannels, int numSamples, long long int timeStamp);
    bool getFullDstFrame();
    long long int getLastFullDstFrameTimeStamp() const;

    unsigned char* srcData[8], *dstData[8];
    int sampleType, elemNumBytes, numChannels;
    int srcNumSamples, dstNumSamples;
    int sampleRate;
    long long int srcTimeStamp, dstTimeStamp;
    int srcPos, dstPos;
    int setSrcSuccess, setDstSuccess;
};

}