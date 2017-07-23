#include "AudioVideoProcessor.h"

#ifdef __cplusplus
extern "C"
{
#endif
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/log.h>
#ifdef __cplusplus
}
#endif

#include <mutex>

namespace avp
{

static std::mutex initMutex;
static int hasInit = 0;

int initFFMPEG()
{
    if (!hasInit)
    {
        std::lock_guard<std::mutex> lg(initMutex);
        if (!hasInit)
        {
            hasInit = 1;
            av_register_all();
            avformat_network_init();    
            avdevice_register_all();
        }
    }    
    return 1;
}

int dumpInput = 1;

void setDumpInput(bool dump)
{
    dumpInput = dump ? 1 : 0;
}

FFmpegLogCallbackFunc ffmpegLogCallback = av_log_default_callback;

void defaultLogCallback(const char* format, va_list vl)
{
    vprintf(format, vl);
}

LogCallbackFunc logCallback = defaultLogCallback;

static std::mutex logMutex;

void lprintf(const char* format, ...)
{
    std::lock_guard<std::mutex> lg(logMutex);
    if (logCallback)
    {
        va_list vl;
        va_start(vl, format);
        logCallback(format, vl);
        va_end(vl);
    }
}

FFmpegLogCallbackFunc setFFmpegLogCallback(FFmpegLogCallbackFunc func)
{
    FFmpegLogCallbackFunc oldCallback = ffmpegLogCallback;
    ffmpegLogCallback = func;
    av_log_set_callback(ffmpegLogCallback);
    return oldCallback;
}

LogCallbackFunc setLogCallback(LogCallbackFunc func)
{
    LogCallbackFunc oldCallback = logCallback;
    logCallback = func;
    return oldCallback;
}

bool supportsEncoder(int type)
{
    if (type < 0 || type >= EncoderTypeCount)
        return false;

    if (type == EncoderTypeMP3 || type == EncoderTypeAAC || type == EncoderTypeLibX264)
        return true;

    if (type == EncoderTypeIntelQsvH264)
        return false;

    if (type == EncoderTypeNvEncH264)
        return false;

    return false;
}

}