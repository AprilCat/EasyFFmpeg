#pragma once

#include "AudioVideoProcessor.h"

namespace avp
{

int initFFMPEG();

extern int dumpInput;

extern FFmpegLogCallbackFunc ffmpegLogCallback;

void defaultLogCallback(const char* format, va_list vl);

void lprintf(const char* format, ...);

inline bool isInterfaceSampleType(int type)
{
    return (type > SampleTypeUnknown) && (type <= SampleType64FP);
}

inline bool isInterfacePixelType(int type)
{
    return (type == PixelTypeBGR24) ||
        (type == PixelTypeBGR32) ||
        (type == PixelTypeNV12) ||
        (type == PixelTypeYUV420P);
}
}
