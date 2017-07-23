#include "AudioVideoProcessor.h"
#include "AudioVideoGlobal.h"
#include "AudioVideoProcessorUtil.h"
#include "FFmpegUtil.h"
//#include "CheckRTSPConnect.h"

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

#include "boost/algorithm/string.hpp"

static char err_buf[AV_ERROR_MAX_STRING_SIZE];
#define av_err2str_new(errnum) \
    av_make_error_string(err_buf, AV_ERROR_MAX_STRING_SIZE, errnum)

static char time_str_buf[AV_TS_MAX_STRING_SIZE];
#define av_ts2timestr_new(ts, tb) av_ts_make_time_string(time_str_buf, ts, tb)
#define av_ts2str_new(ts) av_ts_make_string(time_str_buf, ts)

static bool endsWith(const std::string& str, const std::string& label)
{
    std::string::size_type pos = str.find_last_of(label);
    return (pos == str.size() - label.size());
}

namespace avp
{
struct AudioVideoWriter::Impl
{
    Impl();
    ~Impl();

    void initAll();
    bool open(const char* fileName, const char* formatName, bool externTimeStamp, 
        bool openAudio, const char* audioFormat, int sampleType, int channelLayout, int sampleRate, int audioBPS,
        bool openVideo, const char* videoFormat, int pixelType, int width, int height, double frameRate, int videoBPS,
        const std::vector<Option>& options = std::vector<Option>());
    bool write(AudioVideoFrame& frame);
    void close();

    AVOutputFormat* fmt;
    AVFormatContext* fmtCtx;
    AVStream* videoStream, * audioStream;

    AVFrame* yuvFrame;
    SwsContext* swsCtx;
    int pixelType;
    int frameWidth, frameHeight;
    double videoFps;
    int videoFrameCount;

    AVFrame* audioFrameSrc;
    AVFrame* audioFrameDst;
    int numSamplesSrc;
    int numSamplesDst;
    SwrContext* swrCtx;
    int sampleTypeRequested;
    int sampleRateRequested;
    int numChannelsRequested;
    int channelLayoutRequested;
    int sampleTypeAcquired;
    int sampleRateAcquired;
    int numChannelsAcquired;
    int channelLayoutAcquired;
    long long int inputSampleCount;
    long long int swrSampleCount;
    long long int sampleCount;
    AudioFrameAdaptor adaptor;

    int useExternTimeStamp;
    long long int firstTimeStamp;
    int firstTimeStampSet;
    double videoIncrementUnit, audioIncrementUnit;

    int isOpened;
};

AudioVideoWriter::Impl::Impl()
{
    initAll();
}

AudioVideoWriter::Impl::~Impl()
{
    close();
}

void AudioVideoWriter::Impl::initAll()
{
    fmt = 0;
    fmtCtx = 0;
    videoStream = 0;
    audioStream = 0;

    yuvFrame = 0;
    swsCtx = 0;
    pixelType = PixelTypeUnknown;
    frameWidth = 0;
    frameHeight = 0;
    videoFps = 0;
    videoFrameCount = 0;

    audioFrameSrc = 0;
    audioFrameDst = 0;
    numSamplesSrc = 0;
    numSamplesDst = 0;
    swrCtx = 0;
    sampleTypeRequested = SampleTypeUnknown;
    sampleRateRequested = 0;
    numChannelsRequested = 0;
    channelLayoutRequested = 0;
    sampleTypeAcquired = SampleTypeUnknown;
    sampleRateAcquired = 0;
    numChannelsAcquired = 0;
    channelLayoutAcquired = 0;
    inputSampleCount = 0;
    swrSampleCount = 0;
    sampleCount = 0;
    adaptor.clear();

    useExternTimeStamp = 0;
    firstTimeStamp = -1;
    firstTimeStampSet = 0;
    videoIncrementUnit = 0;
    audioIncrementUnit = 0;

    isOpened = 0;
}

bool AudioVideoWriter::Impl::open(const char* fileName, const char* formatName, bool externTimeStamp,
    bool openAudio, const char* audioFormat, int audioSampleType, int audioChannelLayout, int audioSampleRate, int audioBPS,
    bool openVideo, const char* videoFormat, int videoPixelType, int videoWidth, int videoHeight, double videoFrameRate, int videoBPS,
    const std::vector<Option>& options)
{
    close();

    if (!openAudio && !openVideo)
        return false;

    if (openAudio && (audioSampleRate <= 0 || audioBPS <= 0))
        return false;

    if (openAudio && 
        (audioSampleType < SampleType8U && audioSampleType > SampleType64FP))
        return false;

    if (openVideo && (videoWidth < 0 || videoWidth & 1 || videoHeight < 0 || videoHeight & 1 || videoFrameRate <= 0 || videoBPS <= 0))
        return false;

    if (openVideo && (videoPixelType != PixelTypeBGR24 && videoPixelType != PixelTypeBGR32))
        return false;

    //if (boost::istarts_with(fileName, "rtsp") || boost::iequals(formatName, "rtsp"))
    //{
    //    if (!checkRTSPConnect(fileName))
    //    {
    //        lprintf("Error, cannot connect to url %s\n", fileName);
    //        return false;
    //    }
    //}

    int ret;

    const char* theFormatName = NULL;
    if (formatName && strlen(formatName))
        theFormatName = formatName;

    /* allocate the output media context */
    avformat_alloc_output_context2(&fmtCtx, NULL, theFormatName, fileName);
    if (!fmtCtx)
    {
        lprintf("Error, could not alloc output format context\n");
        goto FAIL;
    }

    useExternTimeStamp = externTimeStamp;

    fmt = fmtCtx->oformat;

    AVDictionary* dict = NULL;
    cvtOptions(options, &dict);

    if (openAudio)
    {
        AVCodecID audioCodecID = AV_CODEC_ID_NONE;
        std::string theAudioFormat = audioFormat;
        if (theAudioFormat == "mp3")
            audioCodecID = AV_CODEC_ID_MP3;
        else if (theAudioFormat == "aac")
            audioCodecID = AV_CODEC_ID_AAC;

        sampleTypeRequested = audioSampleType;
        sampleRateRequested = audioSampleRate;
        channelLayoutRequested = audioChannelLayout;
        numChannelsRequested = av_get_channel_layout_nb_channels(audioChannelLayout);
        bool isFLV = boost::iends_with(fileName, "flv") || boost::iequals(formatName, "flv");
        // NOTICE!!!
        // Disable AVDictionary here
        audioStream = addAudioStream(fmtCtx, NULL, audioCodecID, NULL, 
            (enum AVSampleFormat)audioSampleType, isFLV ? 44100 : audioSampleRate, audioChannelLayout, audioBPS);
        if (!audioStream)
        {
            lprintf("Could not add audio stream.\n");
            goto FAIL;
        }
        AVCodecContext* codecCtx = audioStream->codec;
        sampleTypeAcquired = codecCtx->sample_fmt;
        sampleRateAcquired = codecCtx->sample_rate;
        numChannelsAcquired = codecCtx->channels;
        channelLayoutAcquired = codecCtx->channel_layout;

        /* create resampler context */
        swrCtx = swr_alloc();
        if (!swrCtx)
        {
            lprintf("Could not allocate resampler context\n");
            goto FAIL;
        }

        /* set options */
        av_opt_set_int(swrCtx, "in_channel_layout", channelLayoutRequested, 0);
        av_opt_set_int(swrCtx, "in_sample_rate", sampleRateRequested, 0);
        av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", (enum AVSampleFormat)sampleTypeRequested, 0);

        av_opt_set_int(swrCtx, "out_channel_layout", channelLayoutAcquired, 0);
        av_opt_set_int(swrCtx, "out_sample_rate", sampleRateAcquired, 0);
        av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", (enum AVSampleFormat)sampleTypeAcquired, 0);

        /* initialize the resampling context */
        if ((ret = swr_init(swrCtx)) < 0)
        {
            lprintf("Failed to initialize the resampling context\n");
            goto FAIL;
        }

        numSamplesDst = codecCtx->frame_size;
        audioFrameDst = allocAudioFrame((enum AVSampleFormat)sampleTypeAcquired, 
            sampleRateAcquired, channelLayoutAcquired, numSamplesDst);
        if (!audioFrameDst)
        {
            lprintf("Could not allocate audio frame\n");
            goto FAIL;
        }

        adaptor.setDstInfo(audioFrameDst->data, sampleRateAcquired, 
            sampleTypeAcquired, numChannelsAcquired, numSamplesDst);

        // At this time we are not sure the input audio frame size, 
        // we just make audioFrameSrc frame size sufficiently large. 
        // See the comment in function AudioVideoWriter::Impl::write()
        numSamplesSrc = 2 * numSamplesDst;
        audioFrameSrc = allocAudioFrame((enum AVSampleFormat)sampleTypeAcquired,
            sampleRateAcquired, channelLayoutAcquired, numSamplesSrc);
        if (!audioFrameSrc)
        {
            lprintf("Could not allocate audio frame\n");
            goto FAIL;
        }

        // videoIncrementUnit should not add 0.5, it it a double value.
        audioIncrementUnit = 1.0 / sampleRateAcquired * 1000000;
    }

    if (openVideo)
    {
        AVCodecID videoCodecID = AV_CODEC_ID_NONE;
        std::string theVideoFormat = audioFormat;
        if (theVideoFormat == "h264")
            videoCodecID = AV_CODEC_ID_H264;

        int fpsNum, fpsDen;
        cvtFrameRate(videoFrameRate, &fpsNum, &fpsDen);
        if (fpsDen != 1 && fpsDen != 1001)
        {
            lprintf("Input fps converted to fractional %d / %d, invalid, "
                "denominator should be 1 or 1001, add video stream failed.\n", fpsNum, fpsDen);
            goto FAIL;
        }
        videoStream = addVideoStream(fmtCtx, NULL, videoCodecID, dict, AV_PIX_FMT_YUV420P, 
            videoWidth, videoHeight, fpsNum, fpsDen, videoFrameRate + 0.5, videoBPS, 1);
        if (!videoStream)
        {
            lprintf("Could not add video stream.\n");
            goto FAIL;
        }

        yuvFrame = allocPicture(AV_PIX_FMT_YUV420P, videoWidth, videoHeight);
        if (!yuvFrame)
        {
            lprintf("Could not allocate frame\n");
            goto FAIL;
        }

        if (videoPixelType == PixelTypeBGR24)
        {
            swsCtx = sws_getContext(videoWidth, videoHeight, AV_PIX_FMT_BGR24,
                videoWidth, videoHeight, AV_PIX_FMT_YUV420P,
                SWS_BICUBIC, NULL, NULL, NULL);
        }
        else if (videoPixelType == PixelTypeBGR32)
        {
            swsCtx = sws_getContext(videoWidth, videoHeight, AV_PIX_FMT_BGR0,
                videoWidth, videoHeight, AV_PIX_FMT_YUV420P,
                SWS_BICUBIC, NULL, NULL, NULL);
        }
        if (!swsCtx)
        {
            fprintf(stderr,
                "Could not initialize the conversion context\n");
            goto FAIL;
        }

        pixelType = videoPixelType;
        frameWidth = videoWidth;
        frameHeight = videoHeight;
        videoFps = videoFrameRate;
        // videoIncrementUnit should not add 0.5, it it a double value.
        videoIncrementUnit = 1.0 / videoFps * 1000000;
    }

    //av_dict_free(&dict);

    av_dump_format(fmtCtx, 0, fileName, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&fmtCtx->pb, fileName, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            lprintf("Could not open '%s': %s\n", fileName,
                av_err2str_new(ret));
            goto FAIL;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(fmtCtx, &dict);
    if (ret < 0)
    {
        lprintf("Error occurred when opening output file: %s\n",
            av_err2str_new(ret));
        goto FAIL;
    }

    av_dict_free(&dict);

    isOpened = 1;
    return true;
FAIL:
    av_dict_free(&dict);
    close();
    return false;
}

bool AudioVideoWriter::Impl::write(AudioVideoFrame& frame)
{
    if (!isOpened)
        return false;

    if (!frame.data)
    {
        lprintf("Frame data NULL, cannot write\n");
        return false;
    }

    if (frame.mediaType != AUDIO && frame.mediaType != VIDEO)
    {
        lprintf("Frame neither audio nor video, cannot write\n");
        return false;
    }

    if (frame.mediaType == AUDIO && !audioStream)
    {
        lprintf("Audio stream not opened, skip writing this audio frame\n");
        return true;
    }

    if (frame.mediaType == VIDEO && !videoStream)
    {
        lprintf("Video stream not opened, skip writing this video frame\n");
        return true;
    }

    if (useExternTimeStamp)
    {
        if (!firstTimeStampSet)
        {
            firstTimeStamp = frame.timeStamp;
            lprintf("Use extern time stamp, first pts %s %lld\n",
                frame.mediaType == AUDIO ? "audio" : "video", frame.timeStamp);
            firstTimeStampSet = 1;
        }

        if (frame.timeStamp < firstTimeStamp)
        {
            lprintf("Current frame time stamp %lld smaller than first time stamp %lld, skip writing this frame\n",
                frame.timeStamp, firstTimeStamp);
            return true;
        }
    }    

    if (frame.mediaType == AUDIO && audioStream)
    {
        if (frame.numChannels != numChannelsRequested || 
            frame.channelLayout != channelLayoutRequested ||
            frame.sampleType != sampleTypeRequested)
        {
            lprintf("Audio frame unmatched, failed to write\n");
            return false;
        }

        // NOTICE!!!!!
        // Each time this function av_rescale_rnd is called, the returned value may change,
        // so it is not wise to allocate audioFrameSrc according to the returned value.
        // It would be better to allocate a audioFrameSrc with sufficient large memory.
        // When the return of av_rescale_rnd is larger then audioFrameSrc frame size, reallocate audioFrameSrc.
        // Before reallocation, DO REMEMBER TO CALL av_frame_free to free current audioFrameSrc.
        int currNumSamplesSrc = av_rescale_rnd(swr_get_delay(swrCtx, sampleRateRequested) + frame.numSamples,
            sampleRateAcquired, sampleRateRequested, AV_ROUND_UP);
        if (currNumSamplesSrc > numSamplesSrc)
        {
            av_frame_free(&audioFrameSrc);
            audioFrameSrc = allocAudioFrame((enum AVSampleFormat)sampleTypeAcquired,
                sampleRateAcquired, channelLayoutAcquired, currNumSamplesSrc + 32);
            if (!audioFrameSrc)
            {
                lprintf("Could not allocate audio frame\n");
                return false;
            }
            numSamplesSrc = currNumSamplesSrc + 32;
        }
        unsigned char* origData[8];
        setDataPtr(frame.data, frame.step, frame.numChannels, frame.sampleType, origData);
        int actualNumSamplesSrc = swr_convert(swrCtx, audioFrameSrc->data, currNumSamplesSrc,
            (const unsigned char**)origData, frame.numSamples);
        if (actualNumSamplesSrc < 0)
        {
            lprintf("Could not convert audio samples\n");
            return false;
        }
        // IMPORTANT NOTICE!!!!!!
        // It is not right to set timeStamp argument of setSrcInfo to frame.timeStamp.
        // Notice that the first sample in audioFrameSrc is actually not the first sample in frame,
        // since the SwrContext may buffer some samples.
        // If sampleRateRequested and sampleRateAcquired are the same, it does not matter.
        // But it matters if the two are different.
        adaptor.setSrcInfo(audioFrameSrc->data, sampleRateAcquired, sampleTypeAcquired, numChannelsAcquired, actualNumSamplesSrc, 
            frame.timeStamp + (double(swrSampleCount) / sampleRateAcquired - double(inputSampleCount) / sampleRateRequested) * 1000000 + 0.5);
        inputSampleCount += frame.numSamples;
        swrSampleCount += actualNumSamplesSrc;

        int ret = 0;
        while (adaptor.getFullDstFrame())
        {
            if (useExternTimeStamp)
                audioFrameDst->pts = double(adaptor.getLastFullDstFrameTimeStamp() - firstTimeStamp) / audioIncrementUnit + 0.5;
            else
                audioFrameDst->pts = sampleCount;
            //lprintf("audio frame ts = %lld\n", audioFrameDst->pts);
            ret = writeAudioFrame(fmtCtx, audioStream, audioFrameDst);
            if (ret != 0)
            {
                lprintf("Could not write audio frame, sampleCount = %d\n", sampleCount);
                sampleCount += numSamplesDst;
                return false;
            }
            sampleCount += numSamplesDst;
        }
        return true;
    }
    
    if (frame.mediaType == VIDEO && videoStream)
    {
        if (frame.width != frameWidth || frame.height != frameHeight || frame.pixelType != pixelType)
        {
            lprintf("Video frame unmatched, failed to write\n");
            return false;
        }

        int ret = 0;
        sws_scale(swsCtx,
            (const uint8_t * const *)&frame.data, &frame.step,
            0, frame.height, yuvFrame->data, yuvFrame->linesize);
        if (useExternTimeStamp)
        {
            long long int newPts = (frame.timeStamp - firstTimeStamp) / videoIncrementUnit + 0.5;
            // The following condition may happen to real time video input.
            // Skipping writing the frame will resolve potention warning raised by video encoder.
            // But this may not be the best solution, since some video frames will not write to
            // the destination file. 
            if (yuvFrame->pts > 0 && yuvFrame->pts == newPts)
            {
                lprintf("Incoming video frame pts equals last frame pts (%lld), "
                    "which may cause video encoder warning, skip writing this frame\n", newPts);
                return true;
            }
            yuvFrame->pts = newPts;
        }            
        else
            yuvFrame->pts = videoFrameCount;
        //lprintf("video frame pts = %lld\n", yuvFrame->pts);
        ret = writeVideoFrame(fmtCtx, videoStream, yuvFrame);
        if (ret != 0)
        {
            lprintf("Could not write video frame, frameCount = %d\n", videoFrameCount);
            videoFrameCount++;
            return false;
        }
        videoFrameCount++;
        return true;
    }

    return false;
}

void AudioVideoWriter::Impl::close()
{
    if (isOpened && audioStream)
    {
        int ret = 0;
        while (ret == 0)
        {
            ret = writeAudioFrame(fmtCtx, audioStream, NULL);
        }
    }

    if (isOpened && videoStream)
    {
        int ret = 0;
        while (ret == 0)
        {
            ret = writeVideoFrame(fmtCtx, videoStream, NULL);
        }
    }

    if (isOpened && fmtCtx)
        av_write_trailer(fmtCtx);

    if (yuvFrame)
    {
        av_frame_free(&yuvFrame);
        yuvFrame = 0;
    }

    if (swsCtx)
    {
        sws_freeContext(swsCtx);
        swsCtx = 0;
    }

    if (videoStream && videoStream->codec)
    {
        avcodec_close(videoStream->codec);
        videoStream = 0;
    }

    if (audioFrameSrc)
    {
        av_frame_free(&audioFrameSrc);
        audioFrameSrc = 0;
    }

    if (audioFrameDst)
    {
        av_frame_free(&audioFrameDst);
        audioFrameDst = 0;
    }

    if (swrCtx)
    {
        swr_free(&swrCtx);
        swrCtx = 0;
    }

    if (audioStream && audioStream->codec)
    {
        avcodec_close(audioStream->codec);
        audioStream = 0;
    }

    if (fmtCtx && !(fmt->flags & AVFMT_NOFILE))
    {
        avio_closep(&fmtCtx->pb);
    }

    if (fmtCtx)
    {
        avformat_free_context(fmtCtx);
        fmtCtx = 0;
    }

    initAll();
}

AudioVideoWriter::AudioVideoWriter()
{
    ptrImpl.reset(new Impl);
}

bool AudioVideoWriter::open(const std::string& fileName, const std::string& formatName, bool useExternTimeStamp,
    bool openAudio, const std::string& audioFormat, int sampleType, int numChannels, int sampleRate, int audioBPS,
    bool openVideo, const std::string& videoFormat, int pixelType, int width, int height, double frameRate, int videoBPS,
    const std::vector<Option>& options)
{
    initFFMPEG();
    return ptrImpl->open(fileName.c_str(), formatName.c_str(), useExternTimeStamp,
        openAudio, audioFormat.c_str(), sampleType, numChannels, sampleRate, audioBPS,
        openVideo, videoFormat.c_str(), pixelType, width, height, frameRate, videoBPS,
        options);
}

bool AudioVideoWriter::write(AudioVideoFrame& frame)
{
    return ptrImpl->write(frame);
}

void AudioVideoWriter::close()
{
    ptrImpl->close();
}

}