#pragma once

#include <string>
#include <vector>
#include <memory>

namespace avp
{

void setDumpInput(bool dump);

typedef void(*FFmpegLogCallbackFunc)(void*, int, const char*, va_list);

typedef void(*LogCallbackFunc)(const char*, va_list);

FFmpegLogCallbackFunc setFFmpegLogCallback(FFmpegLogCallbackFunc func);

LogCallbackFunc setLogCallback(LogCallbackFunc func);

enum MediaType
{
    UNKNOWN = -1, AUDIO, VIDEO
};

enum PixelType
{
    PixelTypeUnknown = -1,
    PixelTypeBGR24 = 3,
    PixelTypeBGR32 = 298,
    PixelTypeYUV420P = 0,
    PixelTypeNV12 = 25
};

typedef std::pair<std::string, std::string> Option;

enum SampleType
{
    SampleTypeUnknown = -1,
    SampleType8U,
    SampleType16S,
    SampleType32S,
    SampleType32F,
    SampleType64F,
    SampleType8UP,
    SampleType16SP,
    SampleType32SP,
    SampleType32FP,
    SampleType64FP
};

enum EncoderType
{
    EncoderTypeMP3,
    EncoderTypeAAC,
    EncoderTypeLibX264,
    EncoderTypeNvEncH264,
    EncoderTypeIntelQsvH264,
    EncoderTypeCount
};

bool supportsEncoder(int type);

struct AudioVideoFrame
{
    AudioVideoFrame(unsigned char* data_ = 0, int step_ = 0, int mediaType_ = UNKNOWN, 
        int pixelType_ = PixelTypeUnknown, int width_ = 0, int height_ = 0,
        int sampleType_ = SampleTypeUnknown, int numChannels_ = 0, int channelLayout_ = 0,
        int numSamples_ = 0, long long int timeStamp_ = -1LL, int frameIndex_ = -1)
        : data(data_), step(step_), mediaType(mediaType_), 
          pixelType(pixelType_), width(width_), height(height_),
          sampleType(sampleType_), numChannels(numChannels_), channelLayout(channelLayout_),
          numSamples(numSamples_), timeStamp(timeStamp_), frameIndex(frameIndex_) {}
    unsigned char* data;
    int step;
    int mediaType;
    int pixelType;
    int width, height;
    int sampleType;
    int numChannels;
    int channelLayout;
    int numSamples;    
    long long int timeStamp;
    int frameIndex;
};

inline AudioVideoFrame audioFrame(unsigned char* data, int step, int sampleType, 
    int numChannels, int channelLayout, int numSamples, long long int timeStamp = -1LL, int frameIndex = -1)
{
    AudioVideoFrame frame;
    frame.data = data;
    frame.step = step;
    frame.mediaType = AUDIO;
    frame.sampleType = sampleType;
    frame.numChannels = numChannels;
    frame.channelLayout = channelLayout;
    frame.numSamples = numSamples;    
    frame.timeStamp = timeStamp;
    frame.frameIndex = frameIndex;
    return frame;
}

inline AudioVideoFrame videoFrame(unsigned char* data, int step, int pixelType, int width, int height, 
    long long int timeStamp = -1LL, int frameIndex = -1)
{
    AudioVideoFrame frame;
    frame.data = data;
    frame.step = step;
    frame.mediaType = VIDEO;
    frame.pixelType = pixelType;
    frame.width = width;
    frame.height = height;
    frame.timeStamp = timeStamp;
    frame.frameIndex = frameIndex;
    return frame;
}

struct AudioVideoFrame2
{
    AudioVideoFrame2(unsigned char** data = 0, int* steps = 0, int mediaType = UNKNOWN,
    int pixelType = PixelTypeUnknown, int width = 0, int height = 0,
    int sampleType = SampleTypeUnknown, int numChannels = 0, int channelLayout = 0,
    int numSamples = 0, long long int timeStamp = -1LL, int frameIndex = -1);

    AudioVideoFrame2(unsigned char** data, int step, int sampleType, int numChannels, int channelLayout,
        int numSamples, long long int timeStamp = -1LL, int frameIndex = -1);

    AudioVideoFrame2(unsigned char** data, int* steps, int pixelType, int width, int height,
        long long int timeStamp = -1LL, int frameIndex = -1);

    AudioVideoFrame2(int sampleType, int numChannels, int channelLayout, int numSamples,
        long long int timeStamp = -1LL, int frameIndex = -1);

    AudioVideoFrame2(int pixelType, int width, int height, long long int timeStamp = -1LL, int frameIndex = -1);

    bool create(int sampleType, int numChannels, int channelLayout, int numSamples,
        long long int timeStamp = -1LL, int frameIndex = -1);

    bool create(int pixelType, int width, int height, long long int timeStamp = -1LL, int frameIndex = -1);

    bool copyTo(AudioVideoFrame2& frame) const;

    AudioVideoFrame2 clone() const;

    void release();

    std::shared_ptr<unsigned char> sdata;
    unsigned char* data[8];
    int steps[8];
    int mediaType;
    int pixelType;
    int width, height;
    int sampleType;
    int numChannels;
    int channelLayout;
    int numSamples;
    long long int timeStamp;
    int frameIndex;
};

struct StreamProperties
{
    StreamProperties() :
        mediaType(UNKNOWN), numFrames(0),
        sampleType(SampleTypeUnknown), sampleRate(0), numChannels(0), channelLayout(0), numSamples(0),
        pixelType(PixelTypeUnknown), width(0), height(0), frameRate(0)
    {};
    StreamProperties(int numFrames_, int sampleType_, int sampleRate_, int numChannels_, int channleLayout_, int numSamples_) :
        mediaType(AUDIO), numFrames(numFrames_),
        sampleType(sampleType_), sampleRate(sampleRate_), numChannels(numChannels_), channelLayout(channelLayout), numSamples(numSamples_),
        pixelType(PixelTypeUnknown), width(0), height(0), frameRate(0)
    {};
    StreamProperties(int numFrames_, int pixelType_, int width_, int height_, double frameRate_) :
        mediaType(VIDEO), numFrames(numFrames_),
        sampleType(SampleTypeUnknown), sampleRate(0), numChannels(0), channelLayout(0), numSamples(0),
        pixelType(pixelType_), width(width_), height(height_), frameRate(frameRate_)
    {};
    int mediaType;
    int numFrames;
    int sampleType;
    int sampleRate;
    int numChannels;
    int channelLayout;
    int numSamples;
    int pixelType;
    int width;
    int height;
    double frameRate;
};

void getStreamProperties(const std::string& fileName,
    std::vector<StreamProperties>& props, const std::string& formatName = std::string(),
    const std::vector<Option>& options = std::vector<Option>());

struct InputStreamProperties
{
    InputStreamProperties() :
    mediaType(UNKNOWN), numFrames(0),
    sampleType(SampleTypeUnknown), sampleRate(0), numChannels(0), channelLayout(0), numSamples(0),
    pixelType(PixelTypeUnknown), width(0), height(0), frameRate(0)
    {};
    InputStreamProperties(int numFrames_, int sampleType_, int sampleRate_, int numChannels_, int channleLayout_, int numSamples_) :
        mediaType(AUDIO), numFrames(numFrames_),
        sampleType(sampleType_), sampleRate(sampleRate_), numChannels(numChannels_), channelLayout(channelLayout), numSamples(numSamples_),
        pixelType(PixelTypeUnknown), width(0), height(0), frameRate(0)
    {};
    InputStreamProperties(int numFrames_, int pixelType_, int width_, int height_, double frameRate_) :
        mediaType(VIDEO), numFrames(numFrames_),
        sampleType(SampleTypeUnknown), sampleRate(0), numChannels(0), channelLayout(0), numSamples(0),
        pixelType(pixelType_), width(width_), height(height_), frameRate(frameRate_)
    {};
    int mediaType;
    int numFrames;
    int sampleType;
    int sampleRate;
    int numChannels;
    int channelLayout;
    int numSamples;
    int pixelType;
    int width;
    int height;
    double frameRate;
};

struct OutputStreamProperties
{
    OutputStreamProperties() :
    mediaType(UNKNOWN), sampleType(SampleTypeUnknown), channelLayout(0), sampleRate(0),
    pixelType(PixelTypeUnknown), width(0), height(0), frameRate(0), bitRate(0)
    {}
    OutputStreamProperties(const std::string& format_, int sampleType_, int channelLayout_, int sampleRate_, int bitRate_) :
        mediaType(AUDIO), format(format_),
        sampleType(sampleType_), channelLayout(channelLayout_), sampleRate(sampleRate_),
        pixelType(PixelTypeUnknown), width(0), height(0), frameRate(0), bitRate(bitRate_)
    {}
    OutputStreamProperties(const std::string& format_, int pixelType_, int width_, int height_, double frameRate_, int bitRate_) :
        mediaType(VIDEO), format(format_),
        sampleType(SampleTypeUnknown), channelLayout(0), sampleRate(0),
        pixelType(pixelType_), width(width_), height(height_), frameRate(frameRate_), bitRate(bitRate_)
    {}
    int mediaType;
    std::string format;
    int sampleType;
    int channelLayout;
    int sampleRate;
    int pixelType;
    int width;
    int height;
    double frameRate;
    int bitRate;
};

class AudioVideoReader
{
public:
    AudioVideoReader();
    bool open(const std::string& fileName, bool openAudio, bool openVideo, int pixelType,
        const std::string& formatName = std::string(),
        const std::vector<Option>& options = std::vector<Option>());
    bool read(AudioVideoFrame& frame);
    bool readTo(AudioVideoFrame& audioFrame, AudioVideoFrame& videoFrame, int& mediaType);
    bool seek(long long int timeStamp, int mediaType);
    bool seekByIndex(int frameIndex, int mediaType);
    void close();
    int getVideoWidth() const;
    int getVideoHeight() const;
    double getVideoFrameRate() const;
    int getVideoNumFrames() const;
    int getAudioSampleType() const;
    int getAudioSampleRate() const;
    int getAudioNumChannels() const;
    int getAudioChannelLayout() const;
    int getAudioNumSamples() const;
    int getAudioNumFrames() const;

private:
    struct Impl;
    std::shared_ptr<Impl> ptrImpl;
};

class AudioVideoReader2
{
public:
    AudioVideoReader2();
    bool open(const std::string& fileName, bool openAudio, int sampleType, bool openVideo, int pixelType,
        const std::string& formatName = std::string(),
        const std::vector<Option>& options = std::vector<Option>());
    bool read(AudioVideoFrame2& frame);
    bool readTo(AudioVideoFrame2& audioFrame, AudioVideoFrame2& videoFrame, int& mediaType);
    bool seek(long long int timeStamp, int mediaType);
    bool seekByIndex(int frameIndex, int mediaType);
    void close();
    int getVideoPixelType() const;
    int getVideoWidth() const;
    int getVideoHeight() const;
    double getVideoFrameRate() const;
    int getVideoNumFrames() const;
    int getAudioSampleType() const;
    int getAudioSampleRate() const;
    int getAudioNumChannels() const;
    int getAudioChannelLayout() const;
    int getAudioNumSamples() const;
    int getAudioNumFrames() const;

private:
    struct Impl;
    std::shared_ptr<Impl> ptrImpl;
};

class AudioVideoReader3
{
public:
    static void getStreamProperties(const std::string& fileName,
        std::vector<InputStreamProperties>& props, const std::string& formatName = std::string(),
        const std::vector<Option>& options = std::vector<Option>());
    AudioVideoReader3();
    bool open(const std::string& fileName, const std::vector<int>& indexes,
        int sampleType, int pixelType, const std::string& formatName = std::string(),
        const std::vector<Option>& options = std::vector<Option>());
    bool read(AudioVideoFrame2& frame, int& index);
    bool seek(long long int timeStamp, int index);
    void getProperties(int index, InputStreamProperties& prop);
    void close();

private:
    struct Impl;
    std::shared_ptr<Impl> ptrImpl;
};

class AudioVideoWriter
{
public:
    AudioVideoWriter();
    bool open(const std::string& fileName, const std::string& formatName, bool useExternTimeStamp,
        bool openAudio, const std::string& audioFormat, int sampleType, int channelLayout, int sampleRate, int audioBPS,
        bool openVideo, const std::string& videoFormat, int pixelType, int width, int height, double frameRate, int videoBPS,
        const std::vector<Option>& options = std::vector<Option>());
    bool write(AudioVideoFrame& frame);
    void close();

private:
    struct Impl;
    std::shared_ptr<Impl> ptrImpl;
};

class AudioVideoWriter2
{
public:
    AudioVideoWriter2();
    bool open(const std::string& fileName, const std::string& formatName, bool useExternTimeStamp,
        bool openAudio, const std::string& audioFormat, int sampleType, int channelLayout, int sampleRate, int audioBPS,
        bool openVideo, const std::string& videoFormat, int pixelType, int width, int height, double frameRate, int videoBPS,
        const std::vector<Option>& options = std::vector<Option>());
    bool write(AudioVideoFrame2& frame);
    void close();

private:
    struct Impl;
    std::shared_ptr<Impl> ptrImpl;
};

class AudioVideoWriter3
{
public:
    AudioVideoWriter3();
    bool open(const std::string& fileName, const std::string& formatName, bool useExternTimeStamp,
        const std::vector<OutputStreamProperties>& props, const std::vector<Option>& options = std::vector<Option>());
    bool write(const AudioVideoFrame2& frame, int index);
    void close();

private:
    struct Impl;
    std::shared_ptr<Impl> ptrImpl;
};

struct Device
{
    Device() : deviceType(UNKNOWN) {}
    int deviceType;
    std::string shortName;
    std::string longName;
    std::string numString;
};

void listDirectShowDevices(std::vector<Device>& devices);

void keepAudioDirectShowDevices(const std::vector<Device>& src, std::vector<Device>& dst);

void keepVideoDirectShowDevices(const std::vector<Device>& src, std::vector<Device>& dst);

struct VideoDeviceOption
{
    int minWidth;
    int maxWidth;
    int minHeight;
    int maxHeight;
    double minFrameRate;
    double maxFrameRate;
};

struct AudioDeviceOption
{
    int minNumChannels;
    int maxNumChannels;
    int minSampleRate;
    int maxSampleRate;
};

void listAudioDirectShowDeviceOptions(const Device& device, std::vector<AudioDeviceOption>& options);

void listVideoDirectShowDeviceOptions(const Device& device, std::vector<VideoDeviceOption>& options);

bool supports(std::vector<AudioDeviceOption>& options, int numChannels, int sampleRate);

bool supports(std::vector<VideoDeviceOption>& options, int width, int height, double frameRate);

struct SharedAudioVideoFrame
{
    SharedAudioVideoFrame();
    SharedAudioVideoFrame(const AudioVideoFrame& frame);
    operator AudioVideoFrame() const;

    std::shared_ptr<unsigned char> sharedData;
    unsigned char* data;
    int step;
    int mediaType;
    int pixelType;
    int width, height;
    int sampleType;
    int numChannels;
    int channelLayout;
    int numSamples;
    long long int timeStamp;
    int frameIndex;
};

SharedAudioVideoFrame sharedAudioFrame(int sampleType, int numChannels, int channelLayout, int numSamples,
    long long int timeStamp = -1LL, int frameIndex = -1);

SharedAudioVideoFrame sharedVideoFrame(int pixelType, int width, int height, long long int timeStamp = -1LL, int frameIndex = -1);

bool copy(const AudioVideoFrame& src, SharedAudioVideoFrame& dst);

inline bool copy(const SharedAudioVideoFrame& src, SharedAudioVideoFrame& dst)
{
    return copy(AudioVideoFrame(src), dst);
}

}