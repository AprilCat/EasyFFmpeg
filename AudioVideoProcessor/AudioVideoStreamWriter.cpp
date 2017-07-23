#include "AudioVideoStream.h"
#include "AudioVideoGlobal.h"
#include "AudioVideoProcessor.h"
#include "FFmpegUtil.h"
#include "boost/algorithm/string.hpp"


namespace avp
{

AudioStreamWriter::AudioStreamWriter()
{
    init();
}

AudioStreamWriter::~AudioStreamWriter()
{
    close();
}

void AudioStreamWriter::init()
{
    fmtCtx = 0;
    stream = 0;
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
    sampleCount = 0;
    inputSampleCount = 0;
    swrSampleCount = 0;
    adaptor.clear();

    useExternTimeStamp = 0;
    firstTimeStamp = 0;
    audioIncrementUnit = 0;
}

bool AudioStreamWriter::open(AVFormatContext* outFmtCtx, const std::string& format, int useExternTS, long long int* ptrFirstTS,
    int sampleType, int channelLayout, int sampleRate, int bps, const std::vector<Option>& options)
{
    close();

    AVCodecID codecID = AV_CODEC_ID_NONE;
    if (format == "aac")
        codecID = AV_CODEC_ID_AAC;
    else if (format == "mp3")
        codecID = AV_CODEC_ID_MP3;

    fmtCtx = outFmtCtx;
    sampleTypeRequested = sampleType;
    sampleRateRequested = sampleRate;
    channelLayoutRequested = channelLayout;    
    numChannelsRequested = av_get_channel_layout_nb_channels(channelLayout);
    bool isFLV = boost::iends_with(outFmtCtx->filename, "flv") || boost::iequals(outFmtCtx->oformat->name, "flv");
    // NOTICE!!!
    // Setting profile main will result in failure of opening aac encoder
    // So AVdictionary is disabled now
    //AVDictionary* dict = NULL;
    //cvtOptions(options, &dict);
    stream = addAudioStream(fmtCtx, NULL, codecID, NULL,
        (enum AVSampleFormat)sampleType, isFLV ? 44100 : sampleRate, channelLayout, bps);
    //av_dict_free(&dict);
    if (!stream)
    {
        lprintf("Error in %s, ould not add audio stream.\n", __FUNCTION__);
        goto FAIL;
    }
    AVCodecContext* codecCtx = stream->codec;
    sampleTypeAcquired = codecCtx->sample_fmt;
    sampleRateAcquired = codecCtx->sample_rate;
    numChannelsAcquired = codecCtx->channels;
    channelLayoutAcquired = codecCtx->channel_layout;

    /* create resampler context */
    swrCtx = swr_alloc();
    if (!swrCtx)
    {
        lprintf("Error in %s, ould not allocate resampler context\n", __FUNCTION__);
        goto FAIL;
    }

    /* set options */
    av_opt_set_int(swrCtx, "in_channel_layout", channelLayoutRequested, 0);
    av_opt_set_int(swrCtx, "in_sample_rate", sampleRateRequested, 0);
    av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", (enum AVSampleFormat)sampleTypeRequested, 0);

    av_opt_set_int(swrCtx, "out_channel_layout", channelLayoutAcquired, 0);
    av_opt_set_int(swrCtx, "out_sample_rate", sampleRateAcquired, 0);
    av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", (enum AVSampleFormat)sampleTypeAcquired, 0);

    int ret = 0;
    /* initialize the resampling context */
    if ((ret = swr_init(swrCtx)) < 0)
    {
        lprintf("Error in %s, failed to initialize the resampling context\n", __FUNCTION__);
        goto FAIL;
    }

    numSamplesDst = codecCtx->frame_size;
    audioFrameDst = allocAudioFrame((enum AVSampleFormat)sampleTypeAcquired,
        sampleRateAcquired, channelLayoutAcquired, numSamplesDst);
    if (!audioFrameDst)
    {
        lprintf("Error in %s, could not allocate audio frame\n", __FUNCTION__);
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
        lprintf("Error in %s, could not allocate audio frame\n", __FUNCTION__);
        goto FAIL;
    }

    audioIncrementUnit = 1.0 / sampleRateAcquired * 1000000;
    useExternTimeStamp = useExternTS;
    firstTimeStamp = ptrFirstTS;

    return true;

FAIL:
    close();
    return false;
}

bool AudioStreamWriter::writeFrame(const AudioVideoFrame2& frame)
{
    if (!frame.data[0] || frame.mediaType != AUDIO ||
        frame.numChannels != numChannelsRequested ||
        frame.channelLayout != channelLayoutRequested ||
        frame.sampleType != sampleTypeRequested)
    {
        lprintf("Error in %s, audio frame unmatched, frame data %p, media type %d, require %d, ", 
            "num channels %d, required %d, channle layout %d, require %d, "
            "sample type %d, require %d,  failed to write\n", __FUNCTION__, frame.data[0],
            frame.mediaType, AUDIO, frame.numChannels, numChannelsRequested,
            frame.channelLayout, channelLayoutRequested, frame.sampleType, sampleRateRequested);
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
            lprintf("Error in %s, could not allocate audio frame\n", __FUNCTION__);
            return false;
        }
        numSamplesSrc = currNumSamplesSrc + 32;
    }
    int actualNumSamplesSrc = swr_convert(swrCtx, audioFrameSrc->data, currNumSamplesSrc,
        (const unsigned char**)frame.data, frame.numSamples);
    if (actualNumSamplesSrc < 0)
    {
        lprintf("Error in %s, could not convert audio samples\n", __FUNCTION__);
        return false;
    }
    adaptor.setSrcInfo(audioFrameSrc->data, sampleRateAcquired, sampleTypeAcquired, numChannelsAcquired, actualNumSamplesSrc,
        frame.timeStamp + (double(swrSampleCount) / sampleRateAcquired - double(inputSampleCount) / sampleRateRequested) * 1000000 + 0.5);
    inputSampleCount += frame.numSamples;
    swrSampleCount += actualNumSamplesSrc;

    int ret = 0;
    while (adaptor.getFullDstFrame())
    {
        if (useExternTimeStamp)
            audioFrameDst->pts = double(adaptor.getLastFullDstFrameTimeStamp() - *firstTimeStamp) / audioIncrementUnit + 0.5;
        else
            audioFrameDst->pts = sampleCount;
        //lprintf("audio frame ts = %lld\n", audioFrameDst->pts);
        ret = writeAudioFrame(fmtCtx, stream, audioFrameDst);
        if (ret != 0)
        {
            lprintf("Error in %s, could not write audio frame, sampleCount = %d\n", __FUNCTION__, sampleCount);
            sampleCount += numSamplesDst;
            return false;
        }
        sampleCount += numSamplesDst;
    }
    return true;
}

void AudioStreamWriter::close()
{
    // NOTICE!!!
    // Should detect whether AVIOContext is not NULL
    if (fmtCtx && fmtCtx->pb && stream)
    {
        int ret = 0;
        while (ret == 0)
        {
            ret = writeAudioFrame(fmtCtx, stream, NULL);
        }
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

    if (stream && stream->codec)
    {
        avcodec_close(stream->codec);
        stream = 0;
    }

    init();
}

BuiltinCodecVideoStreamWriter::BuiltinCodecVideoStreamWriter()
{
    init();
}

BuiltinCodecVideoStreamWriter::~BuiltinCodecVideoStreamWriter()
{
    close();
}

void BuiltinCodecVideoStreamWriter::init()
{
    fmtCtx = 0;
    stream = 0;
    yuvFrame = 0;
    swsCtx = 0;

    framePixelTypeRequested = PixelTypeUnknown;
    framePixelFormatAcquired = AV_PIX_FMT_NONE;
    frameWidth = 0;
    frameHeight = 0;
    frameCount = 0;
    frameRate = 0;

    useExternTimeStamp = 0;
    firstTimeStamp = 0;
    videoIncrementUnit = 0;
}

bool BuiltinCodecVideoStreamWriter::open(AVFormatContext* outFmtCtx, const std::string& format, 
    int useExternTS, long long int* ptrFirstTS, int pixelType, int width, int height, 
    double fps, int bps, const std::vector<Option>& options)
{
    close();

    if (!isInterfacePixelType(pixelType))
    {
        lprintf("Error in %s, unsupported pixel type %d\n", __FUNCTION__, pixelType);
        goto FAIL;
    }

    AVCodecID codecID = AV_CODEC_ID_NONE;
    if (format == "h264")
        codecID = AV_CODEC_ID_H264;
    
    AVDictionary* dict = NULL;
    cvtOptions(options, &dict);
    int fpsNum, fpsDen;
    cvtFrameRate(fps, &fpsNum, &fpsDen);
    if (fpsDen != 1 && fpsDen != 1001)
    {
        lprintf("Error in %s, input fps converted to fractional %d / %d, invalid, "
            "denominator should be 1 or 1001, add video stream failed.\n", __FUNCTION__, fpsNum, fpsDen);
        goto FAIL;
    }
    stream = addVideoStream(outFmtCtx, NULL, codecID, dict, 
        AV_PIX_FMT_YUV420P, width, height, fpsNum, fpsDen, fps + 0.5, bps, 1);
    av_dict_free(&dict);
    if (!stream)
    {
        lprintf("Error in %s, could not add video stream.\n", __FUNCTION__);
        goto FAIL;
    }

    if (pixelType != AV_PIX_FMT_YUV420P)
    {
        yuvFrame = allocPicture(AV_PIX_FMT_YUV420P, width, height);
        if (!yuvFrame)
        {
            lprintf("Error in %s, could not allocate frame\n", __FUNCTION__);
            goto FAIL;
        }

        swsCtx = sws_getContext(width, height, (AVPixelFormat)pixelType,
            width, height, AV_PIX_FMT_YUV420P,
            SWS_BICUBIC, NULL, NULL, NULL);
        if (!swsCtx)
        {
            lprintf("Error in %s, could not initialize the conversion context\n", __FUNCTION__);
            goto FAIL;
        }
    }
    else
    {
        yuvFrame = av_frame_alloc();
        if (!yuvFrame)
        {
            lprintf("Error in %s, could not allocate frame\n", __FUNCTION__);
            goto FAIL;
        }
    }

    fmtCtx = outFmtCtx;
    framePixelTypeRequested = pixelType;
    framePixelFormatAcquired = AV_PIX_FMT_YUV420P;
    frameWidth = width;
    frameHeight = height;
    frameRate = fps;

    videoIncrementUnit = 1.0 / fps * 1000000;
    useExternTimeStamp = useExternTS;
    firstTimeStamp = ptrFirstTS;
    return true;

FAIL:
    av_dict_free(&dict);
    close();
    return false;
}

bool BuiltinCodecVideoStreamWriter::writeFrame(const AudioVideoFrame2& frame)
{
    if (!frame.data[0] || frame.mediaType != VIDEO ||
        frame.width != frameWidth || frame.height != frameHeight ||
        frame.pixelType != framePixelTypeRequested)
    {
        lprintf("Error in %s, video frame unmatched, frame data %p, media type %d, require %d, "
            "width %d, require %d, height %d, require %d, pixel type %d, require %d, failed to write\n",
            __FUNCTION__, frame.data[0], frame.mediaType, VIDEO, 
            frame.width, frameWidth, frame.height, frameHeight,
            frame.pixelType, framePixelTypeRequested);
        return false;
    }

    int ret = 0;
    if (swsCtx)
    {
        sws_scale(swsCtx,
            (const uint8_t * const *)frame.data, frame.steps,
            0, frame.height, yuvFrame->data, yuvFrame->linesize);
    }
    else
    {
        yuvFrame->width = frameWidth;
        yuvFrame->height = frameHeight;
        yuvFrame->format = framePixelFormatAcquired;
        memcpy(yuvFrame->data, frame.data, 4 * sizeof(unsigned char*));
        memcpy(yuvFrame->linesize, frame.steps, 4 * sizeof(int));
    }
    
    if (useExternTimeStamp)
    {
        long long int newPts = (frame.timeStamp - *firstTimeStamp) / videoIncrementUnit + 0.5;
        // The following condition may happen to real time video input.
        // Skipping writing the frame will resolve potention warning raised by video encoder.
        // But this may not be the best solution, since some video frames will not write to
        // the destination file. 
        if (yuvFrame->pts > 0 && yuvFrame->pts >= newPts)
        {
            lprintf("Warning in %s, incoming video frame pts %lld not larger than last frame pts %lld, "
                "which may cause video encoder warning, skip writing this frame, "
                "more info, first pts = %lld, frame pts = %lld, inc unit %f\n", 
                __FUNCTION__, newPts, yuvFrame->pts, *firstTimeStamp, frame.timeStamp, videoIncrementUnit);
            return true;
        }
        yuvFrame->pts = newPts;
    }
    else
        yuvFrame->pts = frameCount;
    //lprintf("video frame pts = %lld\n", yuvFrame->pts);
    ret = writeVideoFrame(fmtCtx, stream, yuvFrame);
    if (ret != 0)
    {
        lprintf("Error in %s, could not write video frame, frameCount = %d\n", __FUNCTION__, frameCount);
        frameCount++;
        return false;
    }
    frameCount++;
    return true;
}

void BuiltinCodecVideoStreamWriter::close()
{
    if (fmtCtx && fmtCtx->pb && stream)
    {
        int ret = 0;
        while (ret == 0)
        {
            ret = writeVideoFrame(fmtCtx, stream, NULL);
        }
    }

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

    if (stream && stream->codec)
    {
        avcodec_close(stream->codec);
        stream = 0;
    }

    init();
}

}