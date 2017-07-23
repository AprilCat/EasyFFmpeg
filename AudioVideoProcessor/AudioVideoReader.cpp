#include "AudioVideoProcessor.h"
#include "AudioVideoGlobal.h"
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
#ifdef __cplusplus
}
#endif

namespace avp
{

struct AudioVideoReader::Impl
{
    Impl();
    ~Impl();

    void initAll();
    bool open(const char* fileName, bool openAudio, bool openVideo, int pixelType,
        const char* formatName, const std::vector<Option>& options);
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

    AVFormatContext* fmtCtx;
    AVStream* videoStream, * audioStream;
    AVCodecContext* videoDecCtx, * audioDecCtx;
    AVFrame* frame;

    int width, height;
    enum AVPixelFormat pixFmt;
    int pixType;
    double frameRate;
    int videoNumFrames;
    int videoStreamIndex;
    unsigned char* bgrData[4];
    int bgrLineSize[4];
    int bgrBufSize;
    SwsContext* swsCtx;    
    int nextVideoFrameIndex;

    int numSamples;
    enum AVSampleFormat sampleFmt;
    int sampleType;
    int numChannels;
    int channelLayout;
    int sampleRate;
    int audioNumFrames;
    int audioStreamIndex;
    unsigned char* sampleData;
    int sampleLineSize;
    int nextAudioFrameIndex;

    int isOpened;
    std::string  theFileName;
    std::vector<Option> theOptions;
};

AudioVideoReader::Impl::Impl()
{
    initAll();
}

AudioVideoReader::Impl::~Impl()
{
    close();
}

void AudioVideoReader::Impl::initAll()
{
    fmtCtx = 0;
    videoStream = 0;
    audioStream = 0;
    videoDecCtx = 0;
    audioDecCtx = 0;
    frame = 0;

    width = 0;
    height = 0;
    pixFmt = AV_PIX_FMT_NONE;
    pixType = PixelTypeUnknown;
    frameRate = 0;
    videoNumFrames = 0;
    videoStreamIndex = -1;
    bgrData[0] = bgrData[1] = bgrData[2] = bgrData[3] = 0;
    bgrLineSize[0] = bgrLineSize[1] = bgrLineSize[2] = bgrLineSize[3] = 0;
    bgrBufSize = 0;
    swsCtx = 0;
    nextVideoFrameIndex = -1;

    numSamples = 0;
    sampleFmt = AV_SAMPLE_FMT_NONE;
    sampleType = SampleTypeUnknown;
    numChannels = 0;
    sampleRate = 0;
    audioStreamIndex = -1;
    sampleData = 0;
    sampleLineSize = 0;
    nextAudioFrameIndex = -1;

    isOpened = 0;
    theFileName.clear();
    theOptions.clear();
}

bool AudioVideoReader::Impl::open(const char* fileName, bool openAudio, bool openVideo, 
    int pixelType, const char* formatName, const std::vector<Option>& options)
{
    close();

    if (!openAudio && !openVideo)
        return false;

    if (openVideo && (pixelType != PixelTypeBGR24 && pixelType != PixelTypeBGR32))
        return false;
    pixType = pixelType;

    AVInputFormat* inputFormat = av_find_input_format(formatName);
    if (inputFormat)
    {
        lprintf("Input format %s found, used to open file %s\n", formatName, fileName);
    }

    AVDictionary* dict = NULL;
    cvtOptions(options, &dict);

    int ret;

    /* open input file, and allocate format context */
    if (avformat_open_input(&fmtCtx, fileName, inputFormat, &dict) < 0)
    {
        lprintf("Could not open source file %s\n", fileName);
        av_dict_free(&dict);
        return false;
    }
    av_dict_free(&dict);

    /* retrieve stream information */
    if (avformat_find_stream_info(fmtCtx, NULL) < 0)
    {
        lprintf("Could not find stream information\n");
        goto FAIL;
    }

    if (openAudio)
    {
        if (openCodecContext(&audioStreamIndex, fileName, fmtCtx, AVMEDIA_TYPE_AUDIO) >= 0)
        {
            audioStream = fmtCtx->streams[audioStreamIndex];
            audioDecCtx = audioStream->codec;
            numSamples = audioDecCtx->frame_size;
            sampleRate = audioDecCtx->sample_rate;
            sampleFmt = audioDecCtx->sample_fmt;
            sampleType = sampleFmt;
            numChannels = audioDecCtx->channels;
            channelLayout = audioDecCtx->channel_layout;
            audioNumFrames = audioStream->nb_frames;
            if (channelLayout == 0)
                channelLayout = av_get_default_channel_layout(numChannels);
            if (numSamples)
            {
                int bufSize = av_samples_get_buffer_size(&sampleLineSize, numChannels,
                    numSamples, sampleFmt, 0);
                sampleData = (unsigned char*)av_malloc(bufSize);
            }
        }
        else
        {
            lprintf("Could not open audio stream for reading.\n");
            goto FAIL;
        }
    }

    if (openVideo)
    {
        if ((openCodecContext(&videoStreamIndex, fileName, fmtCtx, AVMEDIA_TYPE_VIDEO) >= 0))
        {
            videoStream = fmtCtx->streams[videoStreamIndex];
            videoDecCtx = videoStream->codec;

            /* allocate image where the decoded image will be put */
            width = videoDecCtx->width;
            height = videoDecCtx->height;
            pixFmt = videoDecCtx->pix_fmt;
            if (pixType == PixelTypeBGR24)
            {
                ret = av_image_alloc(bgrData, bgrLineSize,
                    width, height, AV_PIX_FMT_BGR24, 16);
            }
            else if (pixType == PixelTypeBGR32)
            {
                ret = av_image_alloc(bgrData, bgrLineSize,
                    width, height, AV_PIX_FMT_BGR0, 16);
            }
            if (ret < 0)
            {
                lprintf("Could not allocate raw video buffer\n");
                goto FAIL;
            }
            bgrBufSize = ret;

            frameRate = av_q2d(videoStream->r_frame_rate);
            videoNumFrames = videoStream->nb_frames;

            /* allocate scale context */
            if (pixType == PixelTypeBGR24)
            {
                swsCtx = sws_getContext(width, height, pixFmt,
                    width, height, AV_PIX_FMT_BGR24,
                    SWS_BICUBIC, NULL, NULL, NULL);
            }
            else if (pixType == PixelTypeBGR32)
            {
                swsCtx = sws_getContext(width, height, pixFmt,
                    width, height, AV_PIX_FMT_BGR0,
                    SWS_BICUBIC, NULL, NULL, NULL);
            }
            if (!swsCtx)
            {
                lprintf("Could not allocate scale context\n");
                goto FAIL;
            }
        }
        else
        {
            lprintf("Could not open video stream for reading.\n");
            goto FAIL;
        }
    }

    /* dump input information to stderr */
    if (dumpInput)
        av_dump_format(fmtCtx, 0, fileName, 0);

    if (openAudio && !audioStream)
    {
        lprintf("Could not find video stream in the input, aborting\n");
        ret = 1;
        goto FAIL;
    }
    if (openVideo && !videoStream)
    {
        lprintf("Could not find video stream in the input, aborting\n");
        ret = 1;
        goto FAIL;
    }

    /* When using the new API, you need to use the libavutil/frame.h API, while
    * the classic frame management is available in libavcodec */
    frame = av_frame_alloc();
    if (!frame)
    {
        lprintf("Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto FAIL;
    }

    isOpened = 1;
    theFileName = fileName;
    nextVideoFrameIndex = 0;
    nextAudioFrameIndex = 0;
    theOptions = options;
    return true;

FAIL:
    close();
    return false;
}

bool AudioVideoReader::Impl::read(AudioVideoFrame& header)
{
    if (!isOpened)
        return false;

    AVPacket pkt;
    int ret, gotFrame;
    int pktIndex = -1;

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* read frames from the file */
    int ok = 0;
    while (true)
    {
        int readPacketOK = (av_read_frame(fmtCtx, &pkt) >= 0);
        if (readPacketOK)
        {
            if (pkt.stream_index == audioStreamIndex)
            {
                pktIndex = audioStreamIndex;
                ret = decodeAudioPacket(&pkt, audioDecCtx, frame, sampleRate, sampleFmt, numChannels,
                    &numSamples, &sampleData, &sampleLineSize, &nextAudioFrameIndex, &gotFrame);
                if (ret < 0)
                {
                    lprintf("Error decoding audio packet\n");
                    return false;
                }
                av_free_packet(&pkt);
                // IMPORTANT!!!!!!
                // If gotFrame is false at the beginning of decoding,
                // we have not yet get valid frame, we should continue decoding until we get
                // first valid frame, otherwise the returned frame is invalid.
                if (!gotFrame)
                    continue;
                ok = 1;
                break;
            }
            else if (pkt.stream_index == videoStreamIndex)
            {
                pktIndex = videoStreamIndex;
                ret = decodeVideoPacket(&pkt, videoDecCtx, frame, width, height, pixFmt,
                    swsCtx, bgrData, bgrLineSize, &nextVideoFrameIndex, &gotFrame);
                if (ret < 0)
                {
                    lprintf("Error decoding video packet\n");
                    return false;
                }
                av_free_packet(&pkt);
                if (!gotFrame)
                    continue;
                ok = 1;
                break;
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
                pktIndex = audioStreamIndex;
                ret = decodeAudioPacket(&pkt, audioDecCtx, frame, sampleRate, sampleFmt, numChannels,
                    &numSamples, &sampleData, &sampleLineSize, &nextAudioFrameIndex, &gotFrame);
                if (ret < 0)
                {
                    lprintf("Error decoding audio packet\n");
                    return false;
                }
                ok = gotFrame;
                // IMPORTANT NOTICE!!!!!!
                // If the following two lines of code is
                // if (ok)
                //    break;
                // and if ok == 0 and videoStreamIndex < 0, then
                // there will be a infinite loop.
                // We should pay attention to the condition that only audio is opened.
                if (ok || videoStreamIndex < 0)
                    break;
            }
            if (videoStreamIndex >= 0)
            {
                pktIndex = videoStreamIndex;
                ret = decodeVideoPacket(&pkt, videoDecCtx, frame, width, height, pixFmt,
                    swsCtx, bgrData, bgrLineSize, &nextVideoFrameIndex, &gotFrame);
                if (ret < 0)
                {
                    lprintf("Error decoding video packet\n");
                    return false;
                }
                ok = gotFrame;
                break;
            }
        }
    }

    if (ok)
    {
        long long int streamPts = av_frame_get_best_effort_timestamp(frame);
        if (pktIndex == videoStreamIndex)
        {
            long long int ptsMicroSec =
                (streamPts == AV_NOPTS_VALUE) ? -1 : av_rescale_q(streamPts, videoStream->time_base, avrational(1, AV_TIME_BASE));
            int index = -1;
            if (streamPts != AV_NOPTS_VALUE)
            {
                long long int ptsAbsolute = av_rescale_q(streamPts - (videoStream->start_time == AV_NOPTS_VALUE ? 0 : videoStream->start_time), 
                    videoStream->time_base, avrational(1, AV_TIME_BASE));
                index = double(ptsAbsolute) / 1000000 * frameRate + 0.5;
            }
            header.data = bgrData[0];
            header.width = width;
            header.height = height;
            header.step = bgrLineSize[0];
            header.timeStamp = ptsMicroSec;
            header.frameIndex = index;
            header.mediaType = VIDEO;
            header.pixelType = pixType;
            header.sampleType = SampleTypeUnknown;
            header.numChannels = 0;
            header.channelLayout = 0;
            header.numSamples = 0;
        }
        else if (pktIndex == audioStreamIndex)
        {
            long long int ptsMicroSec =
                (streamPts == AV_NOPTS_VALUE) ? -1 : av_rescale_q(streamPts, audioStream->time_base, avrational(1, AV_TIME_BASE));
            int index = -1;
            if (streamPts != AV_NOPTS_VALUE && numSamples > 0)
            {
                long long int ptsAbsolute = av_rescale_q(streamPts - (audioStream->start_time == AV_NOPTS_VALUE ? 0 : audioStream->start_time),
                    audioStream->time_base, avrational(1, AV_TIME_BASE));
                index = double(ptsAbsolute) / 1000000 * sampleRate / numSamples + 0.5;
            }
            header.data = sampleData;
            header.step = sampleLineSize;
            header.timeStamp = ptsMicroSec;
            header.frameIndex = index;
            header.mediaType = AUDIO;
            header.sampleType = sampleType;
            header.numChannels = numChannels;
            header.channelLayout = channelLayout;
            header.numSamples = numSamples;
            header.width = 0;
            header.height = 0;
            header.pixelType = PixelTypeUnknown;            
        }
        return true;
    }

    return false;
}

bool AudioVideoReader::Impl::readTo(AudioVideoFrame& audioFrame, AudioVideoFrame& videoFrame, int& mediaType)
{
    mediaType = UNKNOWN;
    if (!isOpened)
        return false;

    if (audioStream)
    {
        if (audioFrame.mediaType != AUDIO || audioFrame.sampleType != sampleType || audioFrame.numChannels != numChannels ||
            audioFrame.channelLayout != channelLayout || audioFrame.numSamples != numSamples)
        {
            lprintf("Error in %s, audioFrame not satistied\n", __FUNCTION__);
            return false;
        }
    }

    if (videoStream)
    {
        if (videoFrame.mediaType != VIDEO  || videoFrame.pixelType != pixType || videoFrame.width != width || videoFrame.height != height)
        {
            lprintf("Error in %s, videoFrame not satisfied\n", __FUNCTION__);
            return false;
        }
    }

    AVPacket pkt;
    int ret, gotFrame;
    int pktIndex = -1;

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* read frames from the file */
    int ok = 0;
    while (true)
    {
        int readPacketOK = (av_read_frame(fmtCtx, &pkt) >= 0);
        if (readPacketOK)
        {
            if (pkt.stream_index == audioStreamIndex)
            {
                pktIndex = audioStreamIndex;
                ret = decodeAudioPacket(&pkt, audioDecCtx, frame, sampleRate, sampleFmt, numChannels,
                    numSamples, audioFrame.data, audioFrame.step, &nextAudioFrameIndex, &gotFrame);
                if (ret < 0)
                {
                    lprintf("Error decoding audio packet\n");
                    return false;
                }
                av_free_packet(&pkt);
                // IMPORTANT!!!!!!
                // If gotFrame is false at the beginning of decoding,
                // we have not yet get valid frame, we should continue decoding until we get
                // first valid frame, otherwise the returned frame is invalid.
                if (!gotFrame)
                    continue;
                ok = 1;
                break;
            }
            else if (pkt.stream_index == videoStreamIndex)
            {
                pktIndex = videoStreamIndex;
                unsigned char* currData[] = { videoFrame.data, 0, 0, 0 };
                int currLineSize[] = { videoFrame.step, 0, 0, 0 };
                ret = decodeVideoPacket(&pkt, videoDecCtx, frame, width, height, pixFmt,
                    swsCtx, currData, currLineSize, &nextVideoFrameIndex, &gotFrame);
                if (ret < 0)
                {
                    lprintf("Error decoding video packet\n");
                    return false;
                }
                av_free_packet(&pkt);
                if (!gotFrame)
                    continue;
                ok = 1;
                break;
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
                pktIndex = audioStreamIndex;
                ret = decodeAudioPacket(&pkt, audioDecCtx, frame, sampleRate, sampleFmt, numChannels,
                    numSamples, audioFrame.data, audioFrame.step, &nextAudioFrameIndex, &gotFrame);
                if (ret < 0)
                {
                    lprintf("Error decoding audio packet\n");
                    return false;
                }
                ok = gotFrame;
                // IMPORTANT NOTICE!!!!!!
                // If the following two lines of code is
                // if (ok)
                //    break;
                // and if ok == 0 and videoStreamIndex < 0, then
                // there will be a infinite loop.
                // We should pay attention to the condition that only audio is opened.
                if (ok || videoStreamIndex < 0)
                    break;
            }
            if (videoStreamIndex >= 0)
            {
                pktIndex = videoStreamIndex;
                unsigned char* currData[] = { videoFrame.data, 0, 0, 0 };
                int currLineSize[] = { videoFrame.step, 0, 0, 0 };
                ret = decodeVideoPacket(&pkt, videoDecCtx, frame, width, height, pixFmt,
                    swsCtx, currData, currLineSize, &nextVideoFrameIndex, &gotFrame);
                if (ret < 0)
                {
                    lprintf("Error decoding video packet\n");
                    return false;
                }
                ok = gotFrame;
                break;
            }
        }
    }

    if (ok)
    {
        long long int streamPts = av_frame_get_best_effort_timestamp(frame);
        if (pktIndex == videoStreamIndex)
        {
            long long int ptsMicroSec =
                (streamPts == AV_NOPTS_VALUE) ? -1 : av_rescale_q(streamPts, videoStream->time_base, avrational(1, AV_TIME_BASE));
            videoFrame.timeStamp = ptsMicroSec;
            int index = -1;
            if (streamPts != AV_NOPTS_VALUE)
            {
                long long int ptsAbsolute = av_rescale_q(streamPts - (videoStream->start_time == AV_NOPTS_VALUE ? 0 : videoStream->start_time),
                    videoStream->time_base, avrational(1, AV_TIME_BASE));
                index = double(ptsAbsolute) / 1000000 * frameRate + 0.5;
            }
            videoFrame.frameIndex = index;
            mediaType = VIDEO;
        }
        else if (pktIndex == audioStreamIndex)
        {
            long long int ptsMicroSec =
                (streamPts == AV_NOPTS_VALUE) ? -1 : av_rescale_q(streamPts, audioStream->time_base, avrational(1, AV_TIME_BASE));
            audioFrame.timeStamp = ptsMicroSec;
            int index = -1;
            if (streamPts != AV_NOPTS_VALUE && numSamples > 0)
            {
                long long int ptsAbsolute = av_rescale_q(streamPts - (audioStream->start_time == AV_NOPTS_VALUE ? 0 : audioStream->start_time),
                    audioStream->time_base, avrational(1, AV_TIME_BASE));
                index = double(ptsAbsolute) / 1000000 * sampleRate / numSamples + 0.5;
            }
            audioFrame.frameIndex = index;
            mediaType = AUDIO;
        }
        return true;
    }

    return false;
}


bool AudioVideoReader::Impl::seek(long long int timeStamp, int type)
{
    if (type == AUDIO)
    {
        if (!audioStream)
        {
            lprintf("Error in seeking in audio stream, audio stream not opened\n");
            return false;
        }

        // Seeking a frame directly after opening a file without any reading of a frame
        // MAY RESULT IN A SOUGHT FRAME WITH INACCURATE TIME STAMP 
        // Video stream seeking has been tested, audio not.
        AudioVideoFrame frame;
        while (read(frame))
        {
            if (frame.mediaType == avp::AUDIO)
                break;
        }

        int ret = 0;
        long long int streamTimeStamp = av_rescale_q(timeStamp, avrational(1, AV_TIME_BASE), audioStream->time_base);
        ret = av_seek_frame(fmtCtx, audioStream->index, streamTimeStamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0)
        {
            lprintf("Error, seeking in audio stream failed\n");
            return false;
        }
        avcodec_flush_buffers(audioStream->codec);
        if (videoStream)
            avcodec_flush_buffers(videoStream->codec);
        return true;
    }
    else if (type == VIDEO)
    {
        if (!videoStream)
        {
            lprintf("Error in seeking in video stream, video stream not opened\n");
            return false;
        }

        // Seeking a frame directly after opening a file without any reading of a frame
        // MAY RESULT IN A SOUGHT FRAME WITH INACCURATE TIME STAMP 
        // Video stream seeking has been tested, audio not
        // It would be better to have a read flage in this Impl class indicating
        // that whether a single frame has been read from a specific stream
        AudioVideoFrame frame;
        while (read(frame))
        {
            if (frame.mediaType == avp::VIDEO)
                break;
        }

        int ret = 0;
        long long int prevTimeStamp = timeStamp - 1000000.0 / frameRate;
        long long int streamTimeStamp = av_rescale_q(prevTimeStamp, avrational(1, AV_TIME_BASE), videoStream->time_base);
        if (videoStream->start_time != AV_NOPTS_VALUE && streamTimeStamp < videoStream->start_time)
            streamTimeStamp = videoStream->start_time;
        ret = av_seek_frame(fmtCtx, videoStream->index, streamTimeStamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0)
        {
            lprintf("Error, seeking in video stream failed\n");
            return false;
        }
        avcodec_flush_buffers(videoStream->codec);
        if (audioStream)
            avcodec_flush_buffers(audioStream->codec);

        long long int tsIncUnit = 1000000.0 / frameRate + 0.5;
        long long int halfTsIncUnit = 500000.0 / frameRate + 0.5;
        long long int oneAndHalfTsIncUnit = tsIncUnit + halfTsIncUnit;
        int count = 0;
        bool directReturn = true;
        while (true)
        {
            AudioVideoFrame frame;
            if (!read(frame))
            {
                lprintf("Error, seeking in video stream failed, maybe cannot find target frame when file end met\n");
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
            ret = av_seek_frame(fmtCtx, videoStream->index, streamTimeStamp, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(videoStream->codec);
            if (audioStream)
                avcodec_flush_buffers(audioStream->codec);
            // NOTICE!!!
            // After calling av_seek_frame, the file handle may not directly point to video stream.
            // It is possible that when this seek function returns, the first call of read
            // returns an audio frame.
            for (int i = 0; i < count - 1;)
            {
                AudioVideoFrame frame;
                read(frame);
                if (frame.mediaType == VIDEO)
                    i++;
            }
            return true;
        }
    }

    return false;
}

bool AudioVideoReader::Impl::seekByIndex(int frameIndex, int type)
{
    if (type == AUDIO)
    {
        if (!audioStream)
        {
            lprintf("Error in seeking in audio stream, audio stream not opened\n");
            return false;
        }
        long long int offset = 0;
        if (audioStream->start_time != AV_NOPTS_VALUE)
        {
            offset = av_rescale_q(audioStream->start_time, audioStream->time_base, avrational(1, AV_TIME_BASE));
        }
        long long int timeStamp = frameIndex * 1000000.0 * numSamples / sampleRate + offset + 499;
        return seek(timeStamp, AUDIO);
    }
    else if (type == VIDEO)
    {
        if (!videoStream)
        {
            lprintf("Error in seeking in video stream, video stream not opened\n");
            return false;
        }
        long long int offset = 0;
        if (videoStream->start_time != AV_NOPTS_VALUE)
        {
            offset = av_rescale_q(videoStream->start_time, videoStream->time_base, avrational(1, AV_TIME_BASE));
        }
        long long int timeStamp = frameIndex * 1000000.0  / frameRate + offset + 499;
        return seek(timeStamp, VIDEO);
    }

    return false;
}

void AudioVideoReader::Impl::close()
{
    if (swsCtx)
    {
        sws_freeContext(swsCtx);
        swsCtx = 0;
    }

    if (videoDecCtx)
    {
        avcodec_close(videoDecCtx);
        videoDecCtx = 0;
    }

    if (audioDecCtx)
    {
        avcodec_close(audioDecCtx);
        audioDecCtx = 0;
    }

    if (fmtCtx)
        avformat_close_input(&fmtCtx);

    if (frame)
        av_frame_free(&frame);

    if (bgrData[0])
        av_free(bgrData[0]);

    if (sampleData)
        av_free(sampleData);

    initAll();
}

int AudioVideoReader::Impl::getVideoWidth() const
{
    return width;
}

int AudioVideoReader::Impl::getVideoHeight() const
{
    return height;
}

double AudioVideoReader::Impl::getVideoFrameRate() const
{
    return frameRate;
}

int AudioVideoReader::Impl::getVideoNumFrames() const
{
    return videoNumFrames;
}

int AudioVideoReader::Impl::getAudioSampleType() const
{
    return sampleType;
}

int AudioVideoReader::Impl::getAudioSampleRate() const
{
    return sampleRate;
}

int AudioVideoReader::Impl::getAudioNumChannels() const
{
    return numChannels;
}

int AudioVideoReader::Impl::getAudioChannelLayout() const
{
    return channelLayout;
}

int AudioVideoReader::Impl::getAudioNumFrames() const
{
    return audioNumFrames;
}

int AudioVideoReader::Impl::getAudioNumSamples() const
{
    return numSamples;
}

AudioVideoReader::AudioVideoReader()
{
    ptrImpl.reset(new Impl);
}

bool AudioVideoReader::open(const std::string& fileName, bool openAudio, bool openVideo, int pixelType,
    const std::string& formatName, const std::vector<Option>& options)
{
    initFFMPEG();
    return ptrImpl->open(fileName.c_str(), openAudio, openVideo, pixelType, formatName.c_str(), options);
}

bool AudioVideoReader::read(AudioVideoFrame& frame)
{
    return ptrImpl->read(frame);
}

bool AudioVideoReader::readTo(AudioVideoFrame& audioFrame, AudioVideoFrame& videoFrame, int& mediaType)
{
    return ptrImpl->readTo(audioFrame, videoFrame, mediaType);
}

bool AudioVideoReader::seek(long long int timeStamp, int mediaType)
{
    return ptrImpl->seek(timeStamp, mediaType);
}

bool AudioVideoReader::seekByIndex(int index, int mediaType)
{
    return ptrImpl->seekByIndex(index, mediaType);
}

void AudioVideoReader::close()
{
    ptrImpl->close();
}

int AudioVideoReader::getVideoWidth() const
{
    return ptrImpl->getVideoWidth();
}

int AudioVideoReader::getVideoHeight() const
{
    return ptrImpl->getVideoHeight();
}

double AudioVideoReader::getVideoFrameRate() const
{
    return ptrImpl->getVideoFrameRate();
}

int AudioVideoReader::getVideoNumFrames() const
{
    return ptrImpl->getVideoNumFrames();
}

int AudioVideoReader::getAudioSampleType() const
{
    return ptrImpl->getAudioSampleType();
}

int AudioVideoReader::getAudioSampleRate() const
{
    return ptrImpl->getAudioSampleRate();
}

int AudioVideoReader::getAudioNumChannels() const
{
    return ptrImpl->getAudioNumChannels();
}

int AudioVideoReader::getAudioChannelLayout() const
{
    return ptrImpl->getAudioChannelLayout();
}

int AudioVideoReader::getAudioNumSamples() const
{
    return ptrImpl->getAudioNumSamples();
}

int AudioVideoReader::getAudioNumFrames() const
{
    return ptrImpl->getAudioNumFrames();
}

}