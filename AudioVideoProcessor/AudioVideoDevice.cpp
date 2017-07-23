#include "AudioVideoProcessor.h"
#include "AudioVideoGlobal.h"

#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
extern "C"
{
#endif
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#ifdef __cplusplus
}
#endif

#include <Windows.h>

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>

std::string UnicodeToAnsi(const wchar_t* buf)
{
    int len = ::WideCharToMultiByte(CP_ACP, 0, buf, -1, NULL, 0, NULL, NULL);
    if (len == 0) return "";

    std::vector<char> utf8(len);
    ::WideCharToMultiByte(CP_ACP, 0, buf, -1, &utf8[0], len, NULL, NULL);

    return &utf8[0];
}

std::wstring Utf8ToUnicode(const char* buf)
{
    int len = ::MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
    if (len == 0) return L"";

    std::vector<wchar_t> unicode(len);
    ::MultiByteToWideChar(CP_UTF8, 0, buf, -1, &unicode[0], len);

    return &unicode[0];
}

std::wstring AnsiToUnicode(const char* buf)
{
    int len = ::MultiByteToWideChar(CP_ACP, 0, buf, -1, NULL, 0);
    if (len == 0) return L"";

    std::vector<wchar_t> unicode(len);
    ::MultiByteToWideChar(CP_ACP, 0, buf, -1, &unicode[0], len);

    return &unicode[0];
}

std::string UnicodeToUtf8(const wchar_t* buf)
{
    int len = ::WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
    if (len == 0) return "";

    std::vector<char> utf8(len);
    ::WideCharToMultiByte(CP_UTF8, 0, buf, -1, &utf8[0], len, NULL, NULL);

    return &utf8[0];
}

std::string Utf8ToAnsi(const char* buf)
{
    std::wstring tmp = Utf8ToUnicode(buf);
    return UnicodeToAnsi(tmp.c_str());
}

static std::stringstream sstrm;
static std::ofstream ofstrm;

static void logCallbackForDshowInfo(void* ptr, int level, const char* fmt, va_list vl)
{
    char buf[1024];
    vsprintf(buf, fmt, vl);
    //std::string str = Utf8ToAnsi(buf);
    //sstrm << str;
    sstrm << buf;
}

static bool getInsideQuote(const std::string& src, std::string& dst)
{
    dst.clear();
    std::string::size_type posBeg = src.find("\"");
    if (posBeg == std::string::npos)
        return false;
    std::string::size_type posEnd = src.find("\"", posBeg + 1);
    if (posEnd == std::string::npos)
        return false;
    dst = src.substr(posBeg + 1, posEnd - posBeg - 1);
    return true;
}

static bool parseFrameSizeAndFrameRate(const std::string& line, int& minWidth, int& minHeight,
    int& maxWidth, int& maxHeight, double& minFrameRate, double& maxFrameRate)
{
    if (line.find("unknown compression type") != std::string::npos)
        return false;

    std::string::size_type posMin = line.find("min");
    if (posMin == std::string::npos)
        return false;

    sscanf(line.substr(posMin).c_str(), "min s=%dx%d fps=%lg max s=%dx%d fps=%lg",
        &minWidth, &minHeight, &minFrameRate, &maxWidth, &maxHeight, &maxFrameRate);
    return true;
}

static bool parseNumChannelsAndSampleRate(const std::string& line, int& minNumChannels, int& maxNumChannels,
    int& minSampleRate, int& maxSampleRate)
{
    std::string::size_type posMin = line.find("min");
    if (posMin == std::string::npos)
        return false;

    std::string::size_type posMax = line.find("max");
    if (posMax == std::string::npos)
        return false;

    std::string::size_type posChanEqual, posRateEqual, posBlanck;

    int minNumBits, maxNumBits;
    sscanf(line.substr(posMin).c_str(), "min ch=%d bits=%d rate=%6d max ch=%d bits=%d rate=%6d",
        &minNumChannels, &minNumBits, &minSampleRate, &maxNumChannels, &maxNumBits, &maxSampleRate);
    return true;
}

namespace avp
{
// IMPORTANT NOTICE!!!!!!
// If DirectShow devices are running and another thread call listDirectShowDevices function,
// the log may be like the follows:
//DirectShow video devices(some may be both video and audio devices)
//    dshow passing through packet of type video size  1843200 timestamp 1139377540000 orig timestamp 1139377422893 graph timestamp 1139377540000 diff 117107 XI100DUSB - HDMI Video
//    "XI100DUSB-HDMI Video"
//    Alternative name "@device_pnp_\\?\usb#vid_2935&pid_0001&mi_00#6&32a92423&0&0000#{65e8773d-8f56-11d0-a3b9-00a0c9223196}\global"
//    DirectShow audio devices
//    "数字音频接口 (XI100DUSB-HDMI Audio)"
//    Alternative name "@device_cm_{33D9A762-90C8-11D0-BD43-00A0C911CE86}\数字音频接口 (XI100DUSB-HDMI Audio)"
// There is one line dshow passing through ...
// In order to skip the line, I rewrite the parsing code.
// The safest way of avoiding the appearance of extra lines is to stop all DirectShow devices 
// before calling listDirectShowDevices function.
void listDirectShowDevices(std::vector<Device>& devices)
{
    devices.clear();

    initFFMPEG();

    AVFormatContext *formatCtx = avformat_alloc_context();
    if (!formatCtx)
        return;

    std::string line, name;
    Device d;

    std::map<std::string, int> mapNameCount;
    int numDevices = 0;

    AVInputFormat *iformat = av_find_input_format("dshow");
    if (!iformat)
        goto END;

    AVDictionary* options = NULL;    
    
    sstrm = std::stringstream();
    av_log_set_callback(logCallbackForDshowInfo);
    av_dict_set(&options, "list_devices", "true", 0);
    avformat_open_input(&formatCtx, "dummy", iformat, &options);
    av_log_set_callback(ffmpegLogCallback);

    std::cout << sstrm.str() << std::endl;
    
    while (true)
    {
        if (sstrm.eof())
            break;
        std::getline(sstrm, line);
        if (line.find("DirectShow video") != std::string::npos)
            break;
    }

    bool beginParse = true;
    while (true)
    {
        d.deviceType = VIDEO;
        std::getline(sstrm, line);
        if (!line.size())
            break;
        if (line.find("Could not enumerate") != std::string::npos)
            continue;
        if (line.find("DirectShow audio") != std::string::npos)
            break;
        if (beginParse)
        {
            if (getInsideQuote(line, name))
            {
                d.shortName = name;
                beginParse = false;
            }
            if (sstrm.eof())
            {
                devices.clear();
                goto END;
            }
        }
        else
        {
            if (getInsideQuote(line, name))
            {
                d.longName = name;
                beginParse = true;
                devices.push_back(d);
            }
        }
        if (sstrm.eof())
            break;
    }

    beginParse = true;
    while (true)
    {
        d.deviceType = AUDIO;
        std::getline(sstrm, line);
        if (!line.size())
            break;
        if (line.find("Could not enumerate") != std::string::npos)
            continue;
        if (beginParse)
        {
            if (getInsideQuote(line, name))
            {
                d.shortName = name;
                beginParse = false;
            }
            if (sstrm.eof())
            {
                devices.clear();
                goto END;
            }
        }
        else
        {
            if (getInsideQuote(line, name))
            {
                d.longName = name;
                beginParse = true;
                devices.push_back(d);
            }
        }
        if (sstrm.eof())
            break;
    }

    numDevices = devices.size();
    for (int i = 0; i < numDevices; i++)
    {
        std::map<std::string, int>::iterator itr = mapNameCount.find(devices[i].shortName);
        if (itr == mapNameCount.end())
        {
            devices[i].numString = std::to_string(0);
            mapNameCount[devices[i].shortName] = 0;
        }
        else
        {
            ++(itr->second);
            devices[i].numString = std::to_string(itr->second);            
        }
    }

END:
    avformat_close_input(&formatCtx);
    av_dict_free(&options);
}

void keepAudioDirectShowDevices(const std::vector<Device>& src, std::vector<Device>& dst)
{
    dst.clear();
    if (src.empty())
        return;

    int srcSize = src.size();
    for (int i = 0; i < srcSize; i++)
    {
        if (src[i].deviceType == AUDIO)
            dst.push_back(src[i]);
    }
}

void keepVideoDirectShowDevices(const std::vector<Device>& src, std::vector<Device>& dst)
{
    dst.clear();
    if (src.empty())
        return;

    int srcSize = src.size();
    for (int i = 0; i < srcSize; i++)
    {
        if (src[i].deviceType == VIDEO)
            dst.push_back(src[i]);
    }
}

void listAudioDirectShowDeviceOptions(const Device& device, std::vector<AudioDeviceOption>& options)
{
    options.clear();

    if (device.deviceType != AUDIO)
        return;

    initFFMPEG();

    AVFormatContext *formatCtx = avformat_alloc_context();
    if (!formatCtx)
        return;

    std::string line;
    Device d;

    AVInputFormat *iformat = av_find_input_format("dshow");
    if (!iformat)
        goto END;

    AVDictionary* opts = NULL;

    //sstrm.clear();
    sstrm = std::stringstream();
    av_log_set_callback(logCallbackForDshowInfo);
    av_dict_set(&opts, "list_options", "true", 0);
    av_dict_set(&opts, "audio_device_number", device.numString.c_str(), 0);
    line = "audio=" + device.shortName;
    avformat_open_input(&formatCtx, line.c_str(), iformat, &opts);
    av_log_set_callback(ffmpegLogCallback);

    AudioDeviceOption option;
    while (!sstrm.eof())
    {
        std::getline(sstrm, line);
        if (line.empty())
            continue;
        if (parseNumChannelsAndSampleRate(line, 
            option.minNumChannels, option.maxNumChannels, option.minSampleRate, option.maxSampleRate))
            options.push_back(option);
    }

END:
    avformat_close_input(&formatCtx);
    av_dict_free(&opts);
}

void listVideoDirectShowDeviceOptions(const Device& device, std::vector<VideoDeviceOption>& options)
{
    options.clear();

    if (device.deviceType != VIDEO)
        return;

    initFFMPEG();

    bool retVal = false;

    AVFormatContext *formatCtx = avformat_alloc_context();
    if (!formatCtx)
        return;

    std::string line;
    Device d;

    AVInputFormat *iformat = av_find_input_format("dshow");
    if (!iformat)
        goto END;

    AVDictionary* opts = NULL;

    //sstrm.clear();
    sstrm = std::stringstream();
    av_log_set_callback(logCallbackForDshowInfo);
    av_dict_set(&opts, "list_options", "true", 0);
    av_dict_set(&opts, "video_device_number", device.numString.c_str(), 0);
    line = "video=" + device.shortName;
    avformat_open_input(&formatCtx, line.c_str(), iformat, &opts);
    av_log_set_callback(ffmpegLogCallback);

    VideoDeviceOption option;
    while (!sstrm.eof())
    {
        std::getline(sstrm, line);
        if (line.empty())
            continue;
        if (parseFrameSizeAndFrameRate(line, option.minWidth, option.minHeight, 
            option.maxWidth, option.maxHeight, option.minFrameRate, option.maxFrameRate))
            options.push_back(option);
    }

END:
    avformat_close_input(&formatCtx);
    av_dict_free(&opts);
}

bool supports(std::vector<AudioDeviceOption>& options, int numChannels, int sampleRate)
{
    if (options.empty())
        return false;

    int size = options.size();
    for (int i = 0; i < size; i++)
    {
        if (options[i].maxNumChannels >= numChannels &&
            options[i].minNumChannels <= numChannels &&
            options[i].maxSampleRate >= sampleRate &&
            options[i].minSampleRate <= sampleRate)
            return true;
    }

    return false;
}

bool supports(std::vector<VideoDeviceOption>& options, int width, int height, double frameRate)
{
    if (options.empty())
        return false;

    int size = options.size();
    for (int i = 0; i < size; i++)
    {
        if (options[i].minWidth <= width && options[i].minHeight <= height &&
            options[i].maxWidth >= width && options[i].maxHeight >= height &&
            ((options[i].maxFrameRate > frameRate && options[i].minFrameRate < frameRate) ||
            fabs(options[i].maxFrameRate - frameRate) < 0.01 ||
            fabs(options[i].minFrameRate - frameRate) < 0.01))
            return true;
    }

    return false;
}

}