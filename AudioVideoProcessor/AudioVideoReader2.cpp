#include "AudioVideoProcessor.h"
#include "AudioVideoGlobal.h"
#include "AudioVideoStream.h"
#include "FFmpegUtil.h"

#ifdef __cplusplus
#define __STDC_CONSTANT_MACROS
extern "C"
{
#endif
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#ifdef __cplusplus
}
#endif

namespace avp
{

struct AudioVideoReader2::Impl
{
    Impl();
    ~Impl();

    void initAll();
    bool open(const char* fileName, bool openAudio, int sampleType, bool openVideo, int pixelType,
        const char* formatName, const std::vector<Option>& options);
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

    AVFormatContext* fmtCtx;
    int videoStreamIndex, audioStreamIndex;
    AVStream* aStream, * vStream;
    std::unique_ptr<AudioStreamReader> audioStream;
    std::unique_ptr<VideoStreamReader> videoStream;
    
    AVPixelFormat origPixelFormat;
    int pixelType;
    int width;
    int height;
    double frameRate;
    int videoNumFrames;

    AVSampleFormat origSampleFormat;
    int sampleType;
    int numChannels;
    int channelLayout;
    int sampleRate;
    int audioNumFrames;

    int isOpened;
};

AudioVideoReader2::Impl::Impl()
{
    initAll();
}

AudioVideoReader2::Impl::~Impl()
{
    close();
}

void AudioVideoReader2::Impl::initAll()
{
    fmtCtx = 0;
    videoStream.reset(0);
    audioStream.reset(0);
    videoStreamIndex = -1;
    audioStreamIndex = -1;
    vStream = 0;
    aStream = 0;

    origPixelFormat = AV_PIX_FMT_NONE;
    pixelType = PixelTypeUnknown;
    width = 0;
    height = 0;
    frameRate = 0;
    videoNumFrames = 0;

    origSampleFormat = AV_SAMPLE_FMT_NONE;
    sampleType = SampleTypeUnknown;
    numChannels = 0;
    channelLayout = 0;
    sampleRate = 0;
    audioNumFrames = 0;

    isOpened = 0;
}

bool AudioVideoReader2::Impl::open(const char* fileName, bool openAudio, int splType, bool openVideo,
    int pixType, const char* formatName, const std::vector<Option>& options)
{
    close();

    if (!openAudio && !openVideo)
    {
        lprintf("Error in %s, at least audio or video should be opened\n", __FUNCTION__);
        return false;
    }

    AVInputFormat* inputFormat = av_find_input_format(formatName);
    if (inputFormat)
    {
        lprintf("Info in %s, input format %s found, used to open file %s\n", __FUNCTION__, formatName, fileName);
    }

    AVDictionary* dict = NULL;
    cvtOptions(options, &dict);

    int ret;

    /* open input file, and allocate format context */
    if (avformat_open_input(&fmtCtx, fileName, inputFormat, &dict) < 0)
    {
        lprintf("Error in %s, could not open source file %s\n", __FUNCTION__, fileName);
        av_dict_free(&dict);
        return false;
    }
    av_dict_free(&dict);

    /* retrieve stream information */
    if (avformat_find_stream_info(fmtCtx, NULL) < 0)
    {
        lprintf("Error in %s, could not find stream information\n", __FUNCTION__);
        goto FAIL;
    }

    if (openAudio)
    {
        ret = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, 0, 0);
        if (ret < 0)
        {
            lprintf("Error in %s, could not find audio stream\n", __FUNCTION__);
            goto FAIL;
        }
        audioStreamIndex = ret;
        aStream = fmtCtx->streams[ret];
        audioStream.reset(new AudioStreamReader);
        bool ok = audioStream->open(fmtCtx, ret, splType);
        if (!ok)
        {
            lprintf("Error in %s, could not open audio stream\n", __FUNCTION__);
            goto FAIL;
        }
        origSampleFormat = audioStream->origSampleFormat;
        sampleType = audioStream->sampleType;
        numChannels = audioStream->numChannels;
        channelLayout = audioStream->channelLayout;
        sampleRate = audioStream->sampleRate;
        audioNumFrames = audioStream->numFrames;
    }

    if (openVideo)
    {
        ret = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, 0, 0);
        if (ret < 0)
        {
            lprintf("Error in %s, could not find video stream\n", __FUNCTION__);
            goto FAIL;
        }
        videoStreamIndex = ret;
        vStream = fmtCtx->streams[ret];
        //if (vStream->codec->codec_id == AV_CODEC_ID_H264)
        //    videoStream.reset(new QsvVideoStreamReader);
        //else
            videoStream.reset(new BuiltinCodecVideoStreamReader);
        bool ok = videoStream->open(fmtCtx, ret, pixType);
        if (!ok)
        {
            lprintf("Error in %s, could not open video stream\n", __FUNCTION__);
            goto FAIL;
        }
        origPixelFormat = videoStream->origPixelFormat;
        pixelType = videoStream->pixelType;
        width = videoStream->width;
        height = videoStream->height;
        frameRate = videoStream->frameRate;
        videoNumFrames = videoStream->numFrames;
    }

    /* dump input information to stderr */
    if (dumpInput)
        av_dump_format(fmtCtx, 0, fileName, 0);

    isOpened = 1;
    return true;

FAIL:
    close();
    return false;
}

bool AudioVideoReader2::Impl::read(AudioVideoFrame2& header)
{
    if (!isOpened)
        return false;

    AVPacket pkt;

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* read frames from the file */
    while (true)
    {
        int readPacketOK = (av_read_frame(fmtCtx, &pkt) >= 0);
        if (readPacketOK)
        {
            if (pkt.stream_index == audioStreamIndex)
            {
                if (audioStream->readFrame(pkt, header))
                    return true;
                // IMPORTANT NOTICE!!!
                // I have to add else continue here because the new stream added may cause a return false
                // meaning no frame would be obtained
                else
                    continue;
            }
            else if (pkt.stream_index == videoStreamIndex)
            {
                if (videoStream->readFrame(pkt, header))
                    return true;
                // IMPORTANT NOTICE!!!
                // I have to add else continue here because the new stream added may cause a return false
                // meaning no frame would be obtained
                else
                    continue;
            }
            else
            {
                av_free_packet(&pkt);
                continue;
            }
        }
        else
        {
            pkt.data = NULL;
            pkt.size = 0;
            if (audioStreamIndex >= 0)
            {
                if (audioStream->readFrame(pkt, header))
                    return true;
                if (videoStreamIndex < 0)
                    break;
            }
            if (videoStreamIndex >= 0)
            {
                if (videoStream->readFrame(pkt, header))
                    return true;
                break;
            }
        }
    }

    return false;
}

bool AudioVideoReader2::Impl::readTo(AudioVideoFrame2& audioFrame, AudioVideoFrame2& videoFrame, int& mediaType)
{
    if (!isOpened)
        return false;

    AVPacket pkt;

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* read frames from the file */
    while (true)
    {
        int readPacketOK = (av_read_frame(fmtCtx, &pkt) >= 0);
        if (readPacketOK)
        {
            if (pkt.stream_index == audioStreamIndex)
            {
                if (audioStream->readTo(pkt, audioFrame))
                {
                    mediaType = AUDIO;
                    return true;
                }
                // IMPORTANT NOTICE!!!
                // I have to add else continue here because the new stream added may cause a return false
                // meaning no frame would be obtained
                else
                    continue;
            }
            else if (pkt.stream_index == videoStreamIndex)
            {
                if (videoStream->readTo(pkt, videoFrame))
                {
                    mediaType = VIDEO;
                    return true;
                }
                // IMPORTANT NOTICE!!!
                // I have to add else continue here because the new stream added may cause a return false
                // meaning no frame would be obtained
                else
                    continue;
            }
            else
            {
                av_free_packet(&pkt);
                continue;
            }
        }
        else
        {
            pkt.data = NULL;
            pkt.size = 0;
            if (audioStreamIndex >= 0)
            {
                if (audioStream->readTo(pkt, audioFrame))
                {
                    mediaType = AUDIO;
                    return true;
                }
                if (videoStreamIndex < 0)
                    break;
            }
            if (videoStreamIndex >= 0)
            {
                if (videoStream->readTo(pkt, videoFrame))
                {
                    mediaType = VIDEO;
                    return true;
                }
                break;
            }
        }
    }

    return false;
}


bool AudioVideoReader2::Impl::seek(long long int timeStamp, int type)
{
    if (type == AUDIO)
    {
        if (!audioStream)
        {
            lprintf("Error in %s, failed to seek in audio stream, audio stream not opened\n", __FUNCTION__);
            return false;
        }

        // Seeking a frame directly after opening a file without any reading of a frame
        // MAY RESULT IN A SOUGHT FRAME WITH INACCURATE TIME STAMP 
        // Video stream seeking has been tested, audio not.
        AudioVideoFrame2 frame;
        while (read(frame))
        {
            if (frame.mediaType == avp::AUDIO)
                break;
        }

        int ret = 0;
        long long int streamTimeStamp = av_rescale_q(timeStamp, avrational(1, AV_TIME_BASE), aStream->time_base);
        ret = av_seek_frame(fmtCtx, aStream->index, streamTimeStamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0)
        {
            lprintf("Error in %s, seeking in audio stream failed\n", __FUNCTION__);
            return false;
        }
        audioStream->flushBuffer();
        if (videoStream)
            videoStream->flushBuffer();
        return true;
    }
    else if (type == VIDEO)
    {
        if (!videoStream)
        {
            lprintf("Error in %s, failed to seek in video stream, video stream not opened\n", __FUNCTION__);
            return false;
        }

        // Seeking a frame directly after opening a file without any reading of a frame
        // MAY RESULT IN A SOUGHT FRAME WITH INACCURATE TIME STAMP 
        // Video stream seeking has been tested, audio not
        // It would be better to have a read flage in this Impl class indicating
        // that whether a single frame has been read from a specific stream
        AudioVideoFrame2 frame;
        while (read(frame))
        {
            if (frame.mediaType == avp::VIDEO)
                break;
        }

        int ret = 0;
        long long int prevTimeStamp = timeStamp - 1000000.0 / frameRate;
        long long int streamTimeStamp = av_rescale_q(prevTimeStamp, avrational(1, AV_TIME_BASE), vStream->time_base);
        if (vStream->start_time != AV_NOPTS_VALUE && streamTimeStamp < vStream->start_time)
            streamTimeStamp = vStream->start_time;
        ret = av_seek_frame(fmtCtx, vStream->index, streamTimeStamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0)
        {
            lprintf("Error in %s, seeking in video stream failed\n", __FUNCTION__);
            return false;
        }
        videoStream->flushBuffer();
        if (audioStream)
            audioStream->flushBuffer();

        long long int tsIncUnit = 1000000.0 / frameRate + 0.5;
        long long int halfTsIncUnit = 500000.0 / frameRate + 0.5;
        long long int oneAndHalfTsIncUnit = tsIncUnit + halfTsIncUnit;
        int count = 0;
        bool directReturn = true;
        while (true)
        {
            AudioVideoFrame2 frame;
            if (!read(frame))
            {
                lprintf("Error in %s, seeking in video stream failed, "
                    "maybe cannot find target frame when file end met\n", __FUNCTION__);
                return false;
            }
            if (frame.mediaType == VIDEO)
            {
                count++;
                // This may happen if the first decoded video frame is key frame
                if (abs(timeStamp - frame.timeStamp) <= halfTsIncUnit)
                {
                    directReturn = false;
                    break;
                }
                // This is the usual case where the first decoded video frame is not key frame
                if (timeStamp - frame.timeStamp < oneAndHalfTsIncUnit)
                    break;
            }
        }
        if (directReturn)
            return true;
        else
        {
            ret = av_seek_frame(fmtCtx, vStream->index, streamTimeStamp, AVSEEK_FLAG_BACKWARD);
            videoStream->flushBuffer();
            if (audioStream)
                audioStream->flushBuffer();
            // NOTICE!!!
            // After calling av_seek_frame, the file handle may not directly point to video stream.
            // It is possible that when this seek function returns, the first call of read
            // returns an audio frame.
            for (int i = 0; i < count - 1;)
            {
                AudioVideoFrame2 frame;
                read(frame);
                if (frame.mediaType == VIDEO)
                    i++;
            }
            return true;
        }
    }

    return false;
}

bool AudioVideoReader2::Impl::seekByIndex(int frameIndex, int type)
{
    if (type == AUDIO)
    {
        if (!audioStream)
        {
            lprintf("Error in %s, failed to seek in audio stream, audio stream not opened\n", __FUNCTION__);
            return false;
        }
        long long int offset = 0;
        if (aStream->start_time != AV_NOPTS_VALUE)
        {
            offset = av_rescale_q(aStream->start_time, aStream->time_base, avrational(1, AV_TIME_BASE));
        }
        long long int timeStamp = frameIndex * 1000000.0 * audioStream->numSamples / sampleRate + offset + 499;
        return seek(timeStamp, AUDIO);
    }
    else if (type == VIDEO)
    {
        if (!videoStream)
        {
            lprintf("Error in %s, failed to seek in video stream, video stream not opened\n", __FUNCTION__);
            return false;
        }
        long long int offset = 0;
        if (vStream->start_time != AV_NOPTS_VALUE)
        {
            offset = av_rescale_q(vStream->start_time, vStream->time_base, avrational(1, AV_TIME_BASE));
        }
        long long int timeStamp = frameIndex * 1000000.0 / frameRate + offset + 499;
        return seek(timeStamp, VIDEO);
    }

    return false;
}

void AudioVideoReader2::Impl::close()
{
    if (audioStream)
        audioStream->close();

    if (videoStream)
        videoStream->close();

    if (fmtCtx)
        avformat_close_input(&fmtCtx);

    initAll();
}

int AudioVideoReader2::Impl::getVideoPixelType() const
{
    return pixelType;
}

int AudioVideoReader2::Impl::getVideoWidth() const
{
    return width;
}

int AudioVideoReader2::Impl::getVideoHeight() const
{
    return height;
}

double AudioVideoReader2::Impl::getVideoFrameRate() const
{
    return frameRate;
}

int AudioVideoReader2::Impl::getVideoNumFrames() const
{
    return videoNumFrames;
}

int AudioVideoReader2::Impl::getAudioSampleType() const
{
    return sampleType;
}

int AudioVideoReader2::Impl::getAudioSampleRate() const
{
    return sampleRate;
}

int AudioVideoReader2::Impl::getAudioNumChannels() const
{
    return numChannels;
}

int AudioVideoReader2::Impl::getAudioChannelLayout() const
{
    return channelLayout;
}

int AudioVideoReader2::Impl::getAudioNumFrames() const
{
    return audioNumFrames;
}

int AudioVideoReader2::Impl::getAudioNumSamples() const
{
    return audioStream ? audioStream->numSamples : 0;
}

AudioVideoReader2::AudioVideoReader2()
{
    ptrImpl.reset(new Impl);
}

bool AudioVideoReader2::open(const std::string& fileName, bool openAudio, int sampleType, bool openVideo, int pixelType,
    const std::string& formatName, const std::vector<Option>& options)
{
    initFFMPEG();
    return ptrImpl->open(fileName.c_str(), openAudio, sampleType, openVideo, pixelType, formatName.c_str(), options);
}

bool AudioVideoReader2::read(AudioVideoFrame2& frame)
{
    return ptrImpl->read(frame);
}

bool AudioVideoReader2::readTo(AudioVideoFrame2& audioFrame, AudioVideoFrame2& videoFrame, int& mediaType)
{
    return ptrImpl->readTo(audioFrame, videoFrame, mediaType);
}

bool AudioVideoReader2::seek(long long int timeStamp, int mediaType)
{
    return ptrImpl->seek(timeStamp, mediaType);
}

bool AudioVideoReader2::seekByIndex(int index, int mediaType)
{
    return ptrImpl->seekByIndex(index, mediaType);
}

void AudioVideoReader2::close()
{
    ptrImpl->close();
}

int AudioVideoReader2::getVideoPixelType() const
{
    return ptrImpl->getVideoPixelType();
}

int AudioVideoReader2::getVideoWidth() const
{
    return ptrImpl->getVideoWidth();
}

int AudioVideoReader2::getVideoHeight() const
{
    return ptrImpl->getVideoHeight();
}

double AudioVideoReader2::getVideoFrameRate() const
{
    return ptrImpl->getVideoFrameRate();
}

int AudioVideoReader2::getVideoNumFrames() const
{
    return ptrImpl->getVideoNumFrames();
}

int AudioVideoReader2::getAudioSampleType() const
{
    return ptrImpl->getAudioSampleType();
}

int AudioVideoReader2::getAudioSampleRate() const
{
    return ptrImpl->getAudioSampleRate();
}

int AudioVideoReader2::getAudioNumChannels() const
{
    return ptrImpl->getAudioNumChannels();
}

int AudioVideoReader2::getAudioChannelLayout() const
{
    return ptrImpl->getAudioChannelLayout();
}

int AudioVideoReader2::getAudioNumSamples() const
{
    return ptrImpl->getAudioNumSamples();
}

int AudioVideoReader2::getAudioNumFrames() const
{
    return ptrImpl->getAudioNumFrames();
}

}