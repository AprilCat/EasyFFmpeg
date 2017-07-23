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
#ifdef __cplusplus
}
#endif


namespace avp
{

void getStreamProperties(const std::string& fileName, std::vector<StreamProperties>& props,
    const std::string& formatName, const std::vector<Option>& options)
{
    props.clear();

    AVInputFormat* inputFormat = av_find_input_format(formatName.c_str());
    if (inputFormat)
    {
        lprintf("Input format %s found, used to open file %s\n", formatName.c_str(), fileName.c_str());
    }

    AVDictionary* dict = NULL;
    cvtOptions(options, &dict);

    AVFormatContext* fmtCtx = NULL;
    /* open input file, and allocate format context */
    if (avformat_open_input(&fmtCtx, fileName.c_str(), inputFormat, &dict) < 0)
    {
        lprintf("Could not open source file %s\n", fileName.c_str());
        goto END;
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmtCtx, NULL) < 0)
    {
        lprintf("Could not find stream information\n");
        goto END;
    }

    int numStreams = fmtCtx->nb_streams;
    for (int i = 0; i < numStreams; i++)
    {
        AVStream* s = fmtCtx->streams[i];
        AVCodecContext* c = s->codec;
        if (c->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            props.push_back(StreamProperties(s->nb_frames, c->sample_fmt, c->sample_rate,
                c->channels, c->channel_layout, c->frame_size));
        }
        else if (c->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            props.push_back(StreamProperties(s->nb_frames, c->pix_fmt, c->width, c->height,
                av_q2d(s->r_frame_rate)));
        }
        else
            props.push_back(StreamProperties());
    }

END:
    av_dict_free(&dict);
    avformat_close_input(&fmtCtx);
}

AudioStreamReader::AudioStreamReader()
{
    init();
}

AudioStreamReader::~AudioStreamReader()
{
    close();
}

void AudioStreamReader::init()
{
    fmtCtx = 0;
    stream = 0;
    streamIndex = -1;
    decCtx = 0;
    frame = 0;
    numFrames = 0;
    numSamples = 0;
    origSampleFormat = AV_SAMPLE_FMT_NONE;
    sampleType = SampleTypeUnknown;
    numChannels = 0;
    sampleRate = 0;
    memset(sampleData, 0, sizeof(sampleData));
    sampleLineSize = 0;
    swrCtx = 0;
}

bool AudioStreamReader::open(AVFormatContext* outFmtCtx, int index, int splType)
{
    close();

    fmtCtx = outFmtCtx;
    stream = fmtCtx->streams[index];
    streamIndex = index;

    decCtx = stream->codec;
    AVCodec* dec = avcodec_find_decoder(decCtx->codec_id);
    if (!dec)
    {
        lprintf("Error in %s, failed to find %s codec\n",
            __FUNCTION__, av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto FAIL;
    }

    int ret;
    if ((ret = avcodec_open2(decCtx, dec, 0)) < 0)
    {
        lprintf("Error in %s, ailed to open %s codec\n",
            __FUNCTION__, av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        goto FAIL;
    }

    numFrames = stream->nb_frames;
    numSamples = decCtx->frame_size;
    sampleRate = decCtx->sample_rate;
    origSampleFormat = decCtx->sample_fmt;
    numChannels = decCtx->channels;
    channelLayout = decCtx->channel_layout;
    sampleType = (isInterfaceSampleType(splType) && (splType != origSampleFormat)) ? splType : origSampleFormat;
    if (channelLayout == 0)
        channelLayout = av_get_default_channel_layout(numChannels);
    if (sampleType != origSampleFormat)
    {
        swrCtx = swr_alloc();
        if (!swrCtx)
        {
            lprintf("Error in %s, could not allocate resampler context\n", __FUNCTION__);
            goto FAIL;
        }

        av_opt_set_int(swrCtx, "in_channel_layout", channelLayout, 0);
        av_opt_set_int(swrCtx, "in_sample_rate", sampleRate, 0);
        av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", origSampleFormat, 0);

        av_opt_set_int(swrCtx, "out_channel_layout", channelLayout, 0);
        av_opt_set_int(swrCtx, "out_sample_rate", sampleRate, 0);
        av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", (AVSampleFormat)sampleType, 0);

        if ((ret = swr_init(swrCtx)) < 0)
        {
            lprintf("Error in %s, failed to initialize the resampling context\n", __FUNCTION__);
            goto FAIL;
        }

        if (numSamples)
        {
            //int bufSize = av_samples_get_buffer_size(&sampleLineSize, numChannels,
            //    numSamples, (AVSampleFormat)sampleType, 0);
            //sampleData = (unsigned char*)av_malloc(bufSize);
            av_samples_alloc(sampleData, &sampleLineSize, numChannels, numSamples, (AVSampleFormat)sampleType, 0);
        }
    }

    frame = av_frame_alloc();
    if (!frame)
    {
        lprintf("Error in %s, could not allocate frame\n", __FUNCTION__);
        ret = AVERROR(ENOMEM);
        goto FAIL;
    }

    return true;

FAIL:
    close();
    return false;
}

bool AudioStreamReader::readFrame(AVPacket& packet, AudioVideoFrame2& header)
{
    int gotFrame;
    int ret = decodeAudioPacket(&packet, decCtx, frame, sampleRate, origSampleFormat, numChannels,
        &numSamples, swrCtx, (AVSampleFormat)sampleType, sampleData, &sampleLineSize, &gotFrame);
    av_free_packet(&packet);

    if (ret < 0)
    {
        lprintf("Error in %s, decoding audio packet failed\n", __FUNCTION__);
        return false;
    }

    if (gotFrame)
    {
        long long int streamPts = av_frame_get_best_effort_timestamp(frame);
        long long int ptsMicroSec =
            (streamPts == AV_NOPTS_VALUE) ? -1 : av_rescale_q(streamPts, stream->time_base, avrational(1, AV_TIME_BASE));
        int index = -1;
        if (streamPts != AV_NOPTS_VALUE && numSamples > 0)
        {
            long long int ptsAbsolute = av_rescale_q(streamPts - (stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time),
                stream->time_base, avrational(1, AV_TIME_BASE));
            index = double(ptsAbsolute) / 1000000 * sampleRate / numSamples + 0.5;
        }
        if (swrCtx)
        {
            header = AudioVideoFrame2(sampleData, sampleLineSize, 
                sampleType, numChannels, channelLayout, numSamples, ptsMicroSec, index);
        }
        else
        {
            header = AudioVideoFrame2(frame->data, frame->linesize[0], 
                sampleType, numChannels, channelLayout, numSamples, ptsMicroSec, index);
        }
        return true;
    }

    return false;
}

bool AudioStreamReader::readTo(AVPacket& packet, AudioVideoFrame2& buffer)
{
    if (!buffer.data || buffer.mediaType != AUDIO || buffer.sampleType != sampleType ||
        buffer.numChannels != numChannels || buffer.channelLayout != channelLayout ||
        buffer.numSamples != numSamples)
    {
        lprintf("Error in %s, buffer not satisfied\n", __FUNCTION__);
        return false;
    }

    int gotFrame;
    int ret = decodeAudioPacket(&packet, decCtx, frame, sampleRate, origSampleFormat, numChannels,
        numSamples, swrCtx, (AVSampleFormat)sampleType, buffer.data, &gotFrame);
    av_free_packet(&packet);

    if (ret < 0)
    {
        lprintf("Error in %s, decoding audio packet failed\n", __FUNCTION__);
        return false;
    }

    if (gotFrame)
    {
        long long int streamPts = av_frame_get_best_effort_timestamp(frame);
        long long int ptsMicroSec =
            (streamPts == AV_NOPTS_VALUE) ? -1 : av_rescale_q(streamPts, stream->time_base, avrational(1, AV_TIME_BASE));
        int index = -1;
        if (streamPts != AV_NOPTS_VALUE && numSamples > 0)
        {
            long long int ptsAbsolute = av_rescale_q(streamPts - (stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time),
                stream->time_base, avrational(1, AV_TIME_BASE));
            index = double(ptsAbsolute) / 1000000 * sampleRate / numSamples + 0.5;
        }
        if (!swrCtx)
            av_samples_copy(buffer.data, frame->data, 0, 0, numSamples, numChannels, (AVSampleFormat)sampleType);
        buffer.timeStamp = ptsMicroSec;
        buffer.frameIndex = index;
        return true;
    }

    return false;
}

void AudioStreamReader::flushBuffer()
{
    if (decCtx)
        avcodec_flush_buffers(decCtx);
}

void AudioStreamReader::getProperties(InputStreamProperties& prop)
{
    prop = InputStreamProperties(numFrames, sampleType, sampleRate, numChannels, channelLayout, numSamples);
}

void AudioStreamReader::close()
{
    if (swrCtx)
        swr_free(&swrCtx);

    if (frame)
        av_frame_free(&frame);

    if (sampleData)
        av_free(sampleData[0]);

    if (decCtx)
        avcodec_close(decCtx);

    init();
}

BuiltinCodecVideoStreamReader::BuiltinCodecVideoStreamReader()
{
    init();
}

BuiltinCodecVideoStreamReader::~BuiltinCodecVideoStreamReader()
{
    close();
}

void BuiltinCodecVideoStreamReader::init()
{
    fmtCtx = 0;
    stream = 0;
    streamIndex = -1;
    decCtx = 0;
    frame = 0;
    width = 0;
    height = 0;
    origPixelFormat = AV_PIX_FMT_NONE;
    pixelType = PixelTypeUnknown;
    frameRate = 0;
    numFrames = 0;
    memset(pixelData, 0, sizeof(pixelData));
    memset(pixelLinesize, 0, sizeof(pixelLinesize));
    swsCtx = 0;
}

bool BuiltinCodecVideoStreamReader::open(AVFormatContext* outFmtCtx, int index, int pixType)
{
    close();

    fmtCtx = outFmtCtx;
    stream = fmtCtx->streams[index];
    streamIndex = index;

    decCtx = stream->codec;
    AVCodec* dec = avcodec_find_decoder(decCtx->codec_id);
    if (!dec)
    {
        lprintf("Error in %s, failed to find %s codec\n", __FUNCTION__, 
            av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        goto FAIL;
    }

    int ret;
    if ((ret = avcodec_open2(decCtx, dec, 0)) < 0)
    {
        lprintf("Error in %s, failed to open %s codec\n", __FUNCTION__,
            av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        goto FAIL;
    }

    /* allocate image where the decoded image will be put */
    width = decCtx->width;
    height = decCtx->height;
    origPixelFormat = decCtx->pix_fmt;
    frameRate = av_q2d(stream->r_frame_rate);
    numFrames = stream->nb_frames;
    pixelType = (isInterfacePixelType(pixType) && (pixType != origPixelFormat)) ? pixType : origPixelFormat;
    if (pixelType != origPixelFormat)
    {
        ret = av_image_alloc(pixelData, pixelLinesize,
            width, height, (AVPixelFormat)pixelType, 16);
        if (ret < 0)
        {
            lprintf("Error in %s, could not allocate raw video buffer\n", __FUNCTION__);
            goto FAIL;
        }

        swsCtx = sws_getContext(width, height, origPixelFormat,
            width, height, (AVPixelFormat)pixelType,
            SWS_BICUBIC, NULL, NULL, NULL);
        if (!swsCtx)
        {
            lprintf("Error in %s, could not allocate scale context\n", __FUNCTION__);
            goto FAIL;
        }
    }

    frame = av_frame_alloc();
    if (!frame)
    {
        lprintf("Error in %s, could not allocate frame\n", __FUNCTION__);
        ret = AVERROR(ENOMEM);
        goto FAIL;
    }

    return true;

FAIL:
    close();
    return false;
}

bool BuiltinCodecVideoStreamReader::readFrame(AVPacket& packet, AudioVideoFrame2& header)
{
    int index, gotFrame;
    int ret = decodeVideoPacket(&packet, decCtx, frame, width, height, origPixelFormat,
        swsCtx, pixelData, pixelLinesize, &index, &gotFrame);
    av_free_packet(&packet);

    if (ret < 0)
    {
        lprintf("Error in %s, decoding video packet failed\n", __FUNCTION__);
        return false;
    }
    
    if (gotFrame)
    {
        long long int streamPts = av_frame_get_best_effort_timestamp(frame);
        long long int ptsMicroSec =
            (streamPts == AV_NOPTS_VALUE) ? -1 : av_rescale_q(streamPts, stream->time_base, avrational(1, AV_TIME_BASE));
        int index = -1;
        if (streamPts != AV_NOPTS_VALUE)
        {
            long long int ptsAbsolute = av_rescale_q(streamPts - (stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time),
                stream->time_base, avrational(1, AV_TIME_BASE));
            index = double(ptsAbsolute) / 1000000 * frameRate + 0.5;
        }
        if (swsCtx)
        {
            header = AudioVideoFrame2(pixelData, pixelLinesize, 
                pixelType, width, height, ptsMicroSec, index);
        }
        else
        {
            header = AudioVideoFrame2(frame->data, frame->linesize,
                pixelType, width, height, ptsMicroSec, index);
        }
        return true;
    }

    return false;
}

bool BuiltinCodecVideoStreamReader::readTo(AVPacket& packet, AudioVideoFrame2& buffer)
{
    if (!buffer.data[0] || buffer.mediaType != VIDEO || buffer.pixelType != pixelType ||
        buffer.width != width || buffer.height != height)
    {
        lprintf("Error in %s, buffer not satisfied\n", __FUNCTION__);
        return false;
    }

    int index, gotFrame;
    int ret = decodeVideoPacket(&packet, decCtx, frame, width, height, origPixelFormat,
        swsCtx, buffer.data, buffer.steps, &index, &gotFrame);
    av_free_packet(&packet);

    if (ret < 0)
    {
        lprintf("Error in %s, decoding video packet failed\n", __FUNCTION__);
        return false;
    }

    if (gotFrame)
    {
        long long int streamPts = av_frame_get_best_effort_timestamp(frame);
        long long int ptsMicroSec =
            (streamPts == AV_NOPTS_VALUE) ? -1 : av_rescale_q(streamPts, stream->time_base, avrational(1, AV_TIME_BASE));
        int index = -1;
        if (streamPts != AV_NOPTS_VALUE)
        {
            long long int ptsAbsolute = av_rescale_q(streamPts - (stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time),
                stream->time_base, avrational(1, AV_TIME_BASE));
            index = double(ptsAbsolute) / 1000000 * frameRate + 0.5;
        }
        if (!swsCtx)
            av_image_copy(buffer.data, buffer.steps, (const unsigned char**)frame->data, frame->linesize, (AVPixelFormat)pixelType, width, height);
        buffer.timeStamp = ptsMicroSec;
        buffer.frameIndex = index;
        return true;
    }

    return false;
}

void BuiltinCodecVideoStreamReader::flushBuffer()
{
    if (decCtx)
        avcodec_flush_buffers(decCtx);
}

void BuiltinCodecVideoStreamReader::getProperties(InputStreamProperties& prop)
{
    prop = InputStreamProperties(numFrames, pixelType, width, height, frameRate);
}

void BuiltinCodecVideoStreamReader::close()
{
    if (swsCtx)
    {
        sws_freeContext(swsCtx);
        swsCtx = 0;
    }

    if (decCtx)
    {
        avcodec_close(decCtx);
        decCtx = 0;
    }

    if (frame)
        av_frame_free(&frame);

    if (pixelData[0])
        av_free(pixelData[0]);

    init();
}


}