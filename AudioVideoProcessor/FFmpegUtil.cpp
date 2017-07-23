#include "FFmpegUtil.h"
#include "AudioVideoProcessor.h"
#include "AudioVideoGlobal.h"

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

#define PRINT_REDUNDANT_LOG 0

static char err_buf[AV_ERROR_MAX_STRING_SIZE];
#define av_err2str_new(errnum) \
    av_make_error_string(err_buf, AV_ERROR_MAX_STRING_SIZE, errnum)

static char time_str_buf[AV_TS_MAX_STRING_SIZE];
#define av_ts2timestr_new(ts, tb) av_ts_make_time_string(time_str_buf, ts, tb)
#define av_ts2str_new(ts) av_ts_make_string(time_str_buf, ts)

using avp::lprintf;

int openCodecContext(int *streamIdx, const char *srcFileName,
    AVFormatContext *fmtCtx, enum AVMediaType type)
{
    int ret, streamIndex;
    AVStream* st;
    AVCodecContext* decCtx = NULL;
    AVCodec* dec = NULL;
    AVDictionary* opts = NULL;

    ret = av_find_best_stream(fmtCtx, type, -1, -1, NULL, 0);
    if (ret < 0) 
    {
        lprintf("Error in %s, ould not find %s stream in input file '%s'\n",
                __FUNCTION__, av_get_media_type_string(type), srcFileName);
        return ret;
    } 
    else 
    {
        streamIndex = ret;
        st = fmtCtx->streams[streamIndex];

        /* find decoder for the stream */
        decCtx = st->codec;
        dec = avcodec_find_decoder(decCtx->codec_id);
        if (!dec) 
        {
            lprintf("Error in %s, failed to find %s codec\n",
                    __FUNCTION__, av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        if ((ret = avcodec_open2(decCtx, dec, &opts)) < 0) 
        {
            lprintf("Error in %s, failed to open %s codec\n",
                    __FUNCTION__, av_get_media_type_string(type));
            return ret;
        }
        *streamIdx = streamIndex;
    }

    return 0;
}

int decodeVideoPacket(AVPacket* pkt, AVCodecContext* videoDecCtx, AVFrame* frame, 
    int width, int height, AVPixelFormat pixFormat, SwsContext* swsCtx,
    uint8_t* videoDstData[4], int videoDstLinesize[4], int *videoFrameCount, int *gotFrame)
{
    int ret = 0;
    int decoded = pkt->size;
    int cached = !pkt->data && !pkt->size;

    *gotFrame = 0;

    /* decode video frame */
    ret = avcodec_decode_video2(videoDecCtx, frame, gotFrame, pkt);
    if (ret < 0) 
    {
        lprintf("Error in %s when decoding video frame (%s)\n", __FUNCTION__, av_err2str_new(ret));
        return ret;
    }

    if (*gotFrame) 
    {
        if (frame->width != width || frame->height != height ||
            frame->format != pixFormat)
        {
            /* To handle this change, one could call av_image_alloc again and
                * decode the following frames into another rawvideo file. */
            lprintf("Error in %s, Width, height and pixel format have to be "
                    "constant, but the width, height or "
                    "pixel format of the input video changed:\n"
                    "old: width = %d, height = %d, format = %s\n"
                    "new: width = %d, height = %d, format = %s\n",
                    __FUNCTION__, width, height, av_get_pix_fmt_name(pixFormat),
                    frame->width, frame->height,
                    av_get_pix_fmt_name((enum AVPixelFormat)(frame->format)));
            return -1;
        }

        (*videoFrameCount)++;
#if PRINT_REDUNDANT_LOG
        lprintf("video_frame%s n:%d coded_n:%d pts:%s\n",
                cached ? "(cached)" : "",
                (*videoFrameCount), frame->coded_picture_number,
                av_ts2timestr_new(frame->pts, &videoDecCtx->time_base));
#endif

        //ztool::Timer t;
        if (swsCtx)
        {
            sws_scale(swsCtx,
                (const uint8_t * const *)frame->data, frame->linesize,
                0, height, videoDstData, videoDstLinesize);
        }
        //t.end();
        //printf("sws time = %f\n", t.elapse());
    }

    return decoded;
}

int decodeAudioPacket(AVPacket* pkt, AVCodecContext* audioDecCtx, AVFrame* frame,
    int sampleRate, enum AVSampleFormat sampleFmt, int numChannels, int* numSamples,
    unsigned char** audioDstData, int* audioDstLineSize,
    int* audioFrameCount, int* gotFrame)
{
    int ret = 0;
    int decoded = pkt->size;
    int cached = !pkt->data && !pkt->size;

    *gotFrame = 0;

    /* decode audio frame */
    ret = avcodec_decode_audio4(audioDecCtx, frame, gotFrame, pkt);
    if (ret < 0) 
    {
        lprintf("Error in %s when decoding audio frame (%s)\n", __FUNCTION__, av_err2str_new(ret));
        return ret;
    }
    if (ret != decoded)
    {
        lprintf("Error in %s, decoded num of bytes not equal to packet size, "
            "such packet containing multiple frames is not supported in this library\n", __FUNCTION__);
        return -1;
    }

    if (*gotFrame) 
    {
        if (frame->sample_rate != sampleRate || frame->format != sampleFmt || frame->channels != numChannels)
        {
            lprintf("Error in %s,  Sample rate, sample format and number of channels have to be "
                    "Constant in the whole media file:\n"
                    "old sample rate = %d, sample format = %s, number of channels = %d\n,"
                    "new sample rate = %d, sample format = %s, number of channels = %d\n",
                    __FUNCTION__, sampleRate, av_get_sample_fmt_name(sampleFmt), numChannels, 
                    frame->sample_rate, av_get_sample_fmt_name((enum AVSampleFormat)frame->format), frame->channels);
            return -1;
        }

        (*audioFrameCount)++;
#if PRINT_REDUNDANT_LOG
        lprintf("audio_frame%s n:%d nb_samples:%d pts:%s\n",
            cached ? "(cached)" : "",
            *audioFrameCount, frame->nb_samples,
            av_ts2timestr_new(frame->pts, &audioDecCtx->time_base));
#endif

        if ((frame->nb_samples != *numSamples) || !(*audioDstData))
        {
            *numSamples = frame->nb_samples;
            av_free(*audioDstData);
            int bufSize = av_samples_get_buffer_size(audioDstLineSize, numChannels, 
                *numSamples, sampleFmt, 0);
            *audioDstData = (unsigned char*)av_malloc(bufSize);
        }

        copyAudioData(frame->data, *audioDstData, *audioDstLineSize, sampleFmt, numChannels, *numSamples);
    }

    return decoded;
}

int decodeAudioPacket(AVPacket* pkt, AVCodecContext* audioDecCtx, AVFrame* frame,
    int sampleRate, enum AVSampleFormat sampleFmt, int numChannels, int numSamples,
    unsigned char* audioDstData, int audioDstLineSize,
    int* audioFrameCount, int* gotFrame)
{
    int ret = 0;
    int decoded = pkt->size;
    int cached = !pkt->data && !pkt->size;

    *gotFrame = 0;

    /* decode audio frame */
    ret = avcodec_decode_audio4(audioDecCtx, frame, gotFrame, pkt);
    if (ret < 0)
    {
        lprintf("Error in %s when decoding audio frame (%s)\n", __FUNCTION__, av_err2str_new(ret));
        return ret;
    }
    if (ret != decoded)
    {
        lprintf("Error in %s, decoded num of bytes not equal to packet size, "
            "such packet containing multiple frames is not supported in this library\n", __FUNCTION__);
        return -1;
    }

    if (*gotFrame)
    {
        if (frame->sample_rate != sampleRate || frame->format != sampleFmt || frame->channels != numChannels)
        {
            lprintf("Error in %s, Sample rate, sample format and number of channels have to be "
                "Constant in the whole media file:\n"
                "old sample rate = %d, sample format = %s, number of channels = %d\n,"
                "new sample rate = %d, sample format = %s, number of channels = %d\n",
                __FUNCTION__, sampleRate, av_get_sample_fmt_name(sampleFmt), numChannels,
                frame->sample_rate, av_get_sample_fmt_name((enum AVSampleFormat)frame->format), frame->channels);
            return -1;
        }

        (*audioFrameCount)++;
#if PRINT_REDUNDANT_LOG
        lprintf("audio_frame%s n:%d nb_samples:%d pts:%s\n",
            cached ? "(cached)" : "",
            *audioFrameCount, frame->nb_samples,
            av_ts2timestr_new(frame->pts, &audioDecCtx->time_base));
#endif
        copyAudioData(frame->data, audioDstData, audioDstLineSize, sampleFmt, numChannels, numSamples);
    }

    return decoded;
}

int decodeAudioPacket(AVPacket* pkt, AVCodecContext* audioDecCtx, AVFrame* frame,
    int sampleRate, AVSampleFormat sampleFmt, int numChannels, int* numSamples,
    SwrContext* swrCtx, AVSampleFormat dstSampleFmt, unsigned char* audioDstData[8], int* audioDstLineSize,
    int* gotFrame)
{
    int ret = 0;
    int decoded = pkt->size;
    int cached = !pkt->data && !pkt->size;

    *gotFrame = 0;

    /* decode audio frame */
    ret = avcodec_decode_audio4(audioDecCtx, frame, gotFrame, pkt);
    if (ret < 0)
    {
        lprintf("Error in %s when decoding audio frame (%s)\n", __FUNCTION__, av_err2str_new(ret));
        return ret;
    }
    if (ret != decoded)
    {
        lprintf("Error in %s, decoded num of bytes not equal to packet size, "
            "such packet containing multiple frames is not supported in this library\n", __FUNCTION__);
        return -1;
    }

    if (*gotFrame)
    {
        if (frame->sample_rate != sampleRate || frame->format != sampleFmt || frame->channels != numChannels)
        {
            lprintf("Error in %s, Sample rate, sample format and number of channels have to be "
                "Constant in the whole media file:\n"
                "old sample rate = %d, sample format = %s, number of channels = %d\n,"
                "new sample rate = %d, sample format = %s, number of channels = %d\n",
                __FUNCTION__, sampleRate, av_get_sample_fmt_name(sampleFmt), numChannels,
                frame->sample_rate, av_get_sample_fmt_name((enum AVSampleFormat)frame->format), frame->channels);
            return -1;
        }

#if PRINT_REDUNDANT_LOG
        lprintf("audio_frame%s n:%d nb_samples:%d pts:%s\n",
            cached ? "(cached)" : "",
            *audioFrameCount, frame->nb_samples,
            av_ts2timestr_new(frame->pts, &audioDecCtx->time_base));
#endif

        if (swrCtx)
        {
            if ((frame->nb_samples != *numSamples) || !(*audioDstData))
            {
                *numSamples = frame->nb_samples;
                av_free(*audioDstData);
                av_samples_alloc(audioDstData, audioDstLineSize, numChannels, *numSamples, dstSampleFmt, 0);
            }            
            ret = swr_convert(swrCtx, audioDstData, *numSamples, (const unsigned char**)frame->data, *numSamples);
            if (ret != *numSamples)
            {
                lprintf("Error in %s, resampler does not produce correct samples\n", __FUNCTION__);
                return -1;
            }
        }
        else
        {
            if (frame->nb_samples != *numSamples)
                *numSamples = frame->nb_samples;
        }
    }

    return decoded;
}

int decodeAudioPacket(AVPacket* pkt, AVCodecContext* audioDecCtx, AVFrame* frame,
    int sampleRate, enum AVSampleFormat sampleFmt, int numChannels, int numSamples,
    SwrContext* swrCtx, AVSampleFormat dstSampleFmt, unsigned char* audioDstData[8],
    int* gotFrame)
{
    int ret = 0;
    int decoded = pkt->size;
    int cached = !pkt->data && !pkt->size;

    *gotFrame = 0;

    /* decode audio frame */
    ret = avcodec_decode_audio4(audioDecCtx, frame, gotFrame, pkt);
    if (ret < 0)
    {
        lprintf("Error in when decoding audio frame (%s)\n", __FUNCTION__, av_err2str_new(ret));
        return ret;
    }
    if (ret != decoded)
    {
        lprintf("Error in %s, decoded num of bytes not equal to packet size, "
            "such packet containing multiple frames is not supported in this library\n", __FUNCTION__);
        return -1;
    }

    if (*gotFrame)
    {
        if (frame->sample_rate != sampleRate || frame->format != sampleFmt || frame->channels != numChannels)
        {
            lprintf("Error in %s, Sample rate, sample format and number of channels have to be "
                "Constant in the whole media file:\n"
                "old sample rate = %d, sample format = %s, number of channels = %d\n,"
                "new sample rate = %d, sample format = %s, number of channels = %d\n",
                __FUNCTION__, sampleRate, av_get_sample_fmt_name(sampleFmt), numChannels,
                frame->sample_rate, av_get_sample_fmt_name((enum AVSampleFormat)frame->format), frame->channels);
            return -1;
        }

#if PRINT_REDUNDANT_LOG
        lprintf("audio_frame%s n:%d nb_samples:%d pts:%s\n",
            cached ? "(cached)" : "",
            *audioFrameCount, frame->nb_samples,
            av_ts2timestr_new(frame->pts, &audioDecCtx->time_base));
#endif
        
        if (swrCtx)
        {
            ret = swr_convert(swrCtx, audioDstData, numSamples, (const unsigned char**)frame->data, numSamples);
            if (ret != numSamples)
            {
                lprintf("Error in %s, resampler does not produce correct samples\n", __FUNCTION__);
                return -1;
            }
        }
    }

    return decoded;
}

void logPacket(const AVFormatContext* fmtCtx, const AVPacket* pkt)
{
    AVRational* time_base = &fmtCtx->streams[pkt->stream_index]->time_base;

    lprintf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str_new(pkt->pts), av_ts2timestr_new(pkt->pts, time_base),
           av_ts2str_new(pkt->dts), av_ts2timestr_new(pkt->dts, time_base),
           av_ts2str_new(pkt->duration), av_ts2timestr_new(pkt->duration, time_base),
           pkt->stream_index);
}

int cvtFrameRate(double frameRate, int* frameRateNum, int* frameRateDen)
{
    if (frameRate <= 0 || !frameRateNum || !frameRateDen)
        return -1;

    int r = frameRate + 0.5;

    if (fabs(r - frameRate) < 0.0001)
    {
        *frameRateNum = r;
        *frameRateDen = 1;
        return 0;
    }

    r = frameRate * 1.001 + 0.5;
    if (fabs(r * 1000 - frameRate * 1001) < 10)
    {
        *frameRateNum = r * 1000;
        *frameRateDen = 1001;
        return 0;
    }

    *frameRateNum = frameRate * 10000 + 0.5;
    *frameRateDen = 10000;
    return 0;
}

AVStream* addVideoStream(AVFormatContext* outFmtCtx, const char* codecName, enum AVCodecID codecID, AVDictionary* dict,
    AVPixelFormat pixFmt, int width, int height, int fpsNum, int fpsDen, int gop, int bps, int openCodec)
{
    AVCodecContext* codecCtx = NULL;
    AVCodec* codec = NULL;
    AVStream* videoStream = NULL;
    AVDictionary* opt = NULL;
    int ret = 0;

    /* find the encoder */
    if (codecName || codecID != AV_CODEC_ID_NONE)
    {
        codec = avcodec_find_encoder_by_name(codecName);
        if (!codec)
            codec = avcodec_find_encoder(codecID);
        if (!codec)
        {
            lprintf("Error in %s, could not find encoder '%s' by name and encoder '%s' by ID\n",
                __FUNCTION__, codecName, avcodec_get_name(codecID));
            return NULL;
        }
    }
    else
    {
        codec = avcodec_find_encoder(outFmtCtx->oformat->video_codec);
        if (!codec)
        {
            lprintf("Error in %s, could not find encoder '%s' by default AVFormatContext::oformat::video_codec\n",
                __FUNCTION__, avcodec_get_name(outFmtCtx->oformat->video_codec));
            return NULL;
        }
    }

    videoStream = avformat_new_stream(outFmtCtx, codec);
    if (!videoStream)
    {
        lprintf("Error in %s, could not allocate stream\n", __FUNCTION__);
        return NULL;
    }
    videoStream->id = outFmtCtx->nb_streams - 1;
    codecCtx = videoStream->codec;

    codecCtx->codec_id = codecID;

    codecCtx->bit_rate = bps > 0 ? bps : width * height * double(fpsNum) / double(fpsDen) * 3 / 2 * 0.15;
    /* Resolution must be a multiple of two. */
    codecCtx->width = width;
    codecCtx->height = height;
    /* timebase: This is the fundamental unit of time (in seconds) in terms
    * of which frame timestamps are represented. For fixed-fps content,
    * timebase should be 1/framerate and timestamp increments should be
    * identical to 1. */
    videoStream->time_base = avrational(fpsDen, fpsNum);
    codecCtx->time_base = videoStream->time_base;

    codecCtx->gop_size = gop > 0 ? gop : (double(fpsNum) / double(fpsDen) + 0.5); 
    codecCtx->pix_fmt = pixFmt;
    //if (codec->pix_fmts)
    //{
    //    codecCtx->pix_fmt = codec->pix_fmts[0];
    //    for (int i = 0; codec->pix_fmts[i] != AV_PIX_FMT_NONE; i++)
    //    {
    //        if (codec->pix_fmts[i] == pixFmt)
    //        {
    //            codecCtx->pix_fmt = pixFmt;
    //            break;
    //        }
    //    }
    //}
    //if (pixFmt != codecCtx->pix_fmt)
    //{
    //    lprintf("requested pixel format %d, acutal pixel format %d\n", pixFmt, codecCtx->pix_fmt);
    //}
    //codecCtx->max_b_frames = 0;

    /* Some formats want stream headers to be separate. */
    if (outFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
        codecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;

    /* open the codec */
    if (openCodec)
    {
        av_dict_copy(&opt, dict, 0);
        ret = avcodec_open2(codecCtx, codec, &opt);
        av_dict_free(&opt);
        if (ret < 0)
        {
            lprintf("Error in %s, could not open video codec: %s\n", __FUNCTION__, av_err2str_new(ret));
            return NULL;
        }
    }

    return videoStream;
}

AVFrame* allocPicture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame* picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
    {
        lprintf("Error in %s, could not allocate frame\n", __FUNCTION__);
        return NULL;
    }

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) 
    {
        lprintf("Error in %s, could not allocate frame data.\n", __FUNCTION__);
        av_frame_free(&picture);
        return NULL;
    }

    return picture;
}

int writeVideoFrame(AVFormatContext* outFmtCtx, AVStream* stream, const AVFrame* frame)
{
    int ret;
    AVCodecContext *codecCtx = stream->codec;
    int gotPacket = 0;

    if (outFmtCtx->oformat->flags & AVFMT_RAWPICTURE) 
    {
        /* a hack to avoid data copy with some raw video muxers */
        AVPacket pkt;
        av_init_packet(&pkt);

        if (!frame)
            return 1;

        pkt.flags        |= AV_PKT_FLAG_KEY;
        pkt.stream_index  = stream->index;
        pkt.data          = (uint8_t *)frame;
        pkt.size          = sizeof(AVPicture);

        pkt.pts = pkt.dts = frame->pts;
        av_packet_rescale_ts(&pkt, codecCtx->time_base, stream->time_base);

        ret = av_interleaved_write_frame(outFmtCtx, &pkt);
    } 
    else 
    {
        AVPacket pkt = { 0 };
        av_init_packet(&pkt);

        /* encode the image */
        ret = avcodec_encode_video2(codecCtx, &pkt, frame, &gotPacket);
        if (ret < 0) 
        {
            lprintf("Error in % when encoding video frame: %s\n", __FUNCTION__, av_err2str_new(ret));
            return 1;
        }

        if (gotPacket) 
        {
            /* rescale output packet timestamp values from codec to stream timebase */
            av_packet_rescale_ts(&pkt, codecCtx->time_base, stream->time_base);
            pkt.stream_index = stream->index;

            /* Write the compressed frame to the media file. */
            //logPacket(outFmtCtx, &pkt);
            ret = av_interleaved_write_frame(outFmtCtx, &pkt);
        } 
        else
            ret = 0;
    }

    if (ret < 0) 
    {
        lprintf("Error in %s, while writing video frame: %s\n", __FUNCTION__, av_err2str_new(ret));
        return 1;
    }

    return (frame || gotPacket) ? 0 : 1;
}

int writeVideoFrame2(AVFormatContext* outFmtCtx, AVStream* stream, AVCodecContext* codecCtx, const AVFrame* frame)
{
    int ret;
    int gotPacket = 0;

    if (outFmtCtx->oformat->flags & AVFMT_RAWPICTURE)
    {
        /* a hack to avoid data copy with some raw video muxers */
        AVPacket pkt;
        av_init_packet(&pkt);

        if (!frame)
            return 1;

        pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.stream_index = stream->index;
        pkt.data = (uint8_t *)frame;
        pkt.size = sizeof(AVPicture);

        pkt.pts = pkt.dts = frame->pts;
        av_packet_rescale_ts(&pkt, codecCtx->time_base, stream->time_base);

        ret = av_interleaved_write_frame(outFmtCtx, &pkt);
    }
    else
    {
        AVPacket pkt = { 0 };
        av_init_packet(&pkt);

        /* encode the image */
        ret = avcodec_encode_video2(codecCtx, &pkt, frame, &gotPacket);
        if (ret < 0)
        {
            lprintf("Error in %s when encoding video frame: %s\n", __FUNCTION__, av_err2str_new(ret));
            return 1;
        }

        if (gotPacket)
        {
            /* rescale output packet timestamp values from codec to stream timebase */
            av_packet_rescale_ts(&pkt, codecCtx->time_base, stream->time_base);
            pkt.stream_index = stream->index;

            /* Write the compressed frame to the media file. */
            //logPacket(outFmtCtx, &pkt);
            ret = av_interleaved_write_frame(outFmtCtx, &pkt);
        }
        else
            ret = 0;
    }

    if (ret < 0)
    {
        lprintf("Error in %s while writing video frame: %s\n", __FUNCTION__, av_err2str_new(ret));
        return 1;
    }

    return (frame || gotPacket) ? 0 : 1;
}

AVStream* addAudioStream(AVFormatContext* outFmtCtx, const char* codecName, enum AVCodecID codecID, AVDictionary* dict,
    enum AVSampleFormat sampleFormat, int sampleRate, int channelLayout, int bps)
{
    AVCodecContext* codecCtx;
    AVCodec* codec;
    AVStream* audioStream;
    AVDictionary* opt = NULL;
    int ret;

    /* find the encoder */
    if (codecName || codecID != AV_CODEC_ID_NONE)
    {
        codec = avcodec_find_encoder_by_name(codecName);
        if (!codec)
            codec = avcodec_find_encoder(codecID);
        if (!codec)
        {
            lprintf("Error in %s, could not find encoder '%s' by name and encoder '%s' by ID\n",
                __FUNCTION__, codecName, avcodec_get_name(codecID));
            return NULL;
        }
    }
    else
    {
        codec = avcodec_find_encoder(outFmtCtx->oformat->video_codec);
        if (!codec)
        {
            lprintf("Error in %s, could not find encoder '%s' by default AVFormatContext::oformat::video_codec\n",
                __FUNCTION__, avcodec_get_name(outFmtCtx->oformat->video_codec));
            return NULL;
        }
    }

    audioStream = avformat_new_stream(outFmtCtx, codec);
    if (!audioStream)
    {
        lprintf("Error in %s, could not allocate stream\n", __FUNCTION__);
        return NULL;
    }
    audioStream->id = outFmtCtx->nb_streams - 1;
    codecCtx = audioStream->codec;

    codecCtx->codec_id = codecID;

    codecCtx->sample_fmt = sampleFormat;
    if (codec->sample_fmts)
    {
        codecCtx->sample_fmt = codec->sample_fmts[0];
        for (int i = 0; codec->sample_fmts[i] > 0; i++)
        {
            AVSampleFormat fmt = codec->sample_fmts[i];
            if (codec->sample_fmts[i] == sampleFormat)
            {
                codecCtx->sample_fmt = sampleFormat;
                break;
            }
        }
    }
    if (codecCtx->sample_fmt != sampleFormat)
    {
        lprintf("Info in %s, requested sample format %d, acutal sample format %d\n", 
            __FUNCTION__, sampleFormat, codecCtx->sample_fmt);
    }

    codecCtx->sample_rate = sampleRate;
    if (codec->supported_samplerates) 
    {
        codecCtx->sample_rate = codec->supported_samplerates[0];
        for (int i = 0; codec->supported_samplerates[i]; i++) 
        {
            if (codec->supported_samplerates[i] == sampleRate)
            {
                codecCtx->sample_rate = sampleRate;
                break;
            }
        }
    }
    if (codecCtx->sample_rate != sampleRate)
    {
        lprintf("Info in %s, requested sample rate %d, actual sample rate %d\n", 
            __FUNCTION__, sampleRate, codecCtx->sample_rate);
    }

    codecCtx->channel_layout = channelLayout;
    if (codec->channel_layouts) 
    {
        codecCtx->channel_layout = codec->channel_layouts[0];
        int maxNumChannels = av_get_channel_layout_nb_channels(codecCtx->channel_layout);
        for (int i = 0; codec->channel_layouts[i]; i++) 
        {
            int layout = codec->channel_layouts[i];
            int numChannels = av_get_channel_layout_nb_channels(layout);
            if (codec->channel_layouts[i] == channelLayout)
            {
                codecCtx->channel_layout = channelLayout;
                break;
            }
            if (maxNumChannels < numChannels)
            {
                maxNumChannels = numChannels;
                codecCtx->channel_layout = layout;
            }
        }
    }
    if (codecCtx->channel_layout != channelLayout)
    {
        char buf1[64], buf2[64];
        av_get_channel_layout_string(buf1, 64, 0, channelLayout);
        av_get_channel_layout_string(buf2, 64, 0, codecCtx->channel_layout);
        lprintf("Info in %s, requested channel layout %s(%d), actual channel layout %s(%d)\n", 
            __FUNCTION__, buf1, channelLayout, buf2, codecCtx->channel_layout);
    }

    codecCtx->channels = av_get_channel_layout_nb_channels(codecCtx->channel_layout);
    codecCtx->bit_rate = bps;
    audioStream->time_base = avrational(1, sampleRate);
    codecCtx->time_base = audioStream->time_base;

    /* Some formats want stream headers to be separate. */
    if (outFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
        codecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;

    /* open the codec */
    av_dict_copy(&opt, dict, 0);
    ret = avcodec_open2(codecCtx, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0)
    {
        lprintf("Error in %s, could not open audio codec: %s\n", __FUNCTION__, av_err2str_new(ret));
        return NULL;
    }

    return audioStream;
}

AVFrame* allocAudioFrame(enum AVSampleFormat sampleFormat, int sampleRate, int channeLayout, int numSamples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame) 
    {
        lprintf("Error in %s, allocating an audio frame\n", __FUNCTION__);
        return NULL;
    }

    frame->format = sampleFormat;
    frame->sample_rate = sampleRate;
    frame->nb_samples = numSamples;
    frame->channel_layout = channeLayout;

    if (numSamples) 
    {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) 
        {
            lprintf("Error in %s, allocating an audio buffer\n", __FUNCTION__);
            av_frame_free(&frame);
            return NULL;
        }
    }

    return frame;
}

int writeAudioFrame(AVFormatContext* outFmtCtx, AVStream* stream, const AVFrame* frame)
{
    int ret;
    AVCodecContext *codecCtx = stream->codec;
    int gotPacket = 0;

    AVPacket pkt = { 0 };
    ret = avcodec_encode_audio2(codecCtx, &pkt, frame, &gotPacket);
    if (ret < 0) 
    {
        lprintf("Error in %s when encoding audio frame: %s\n", __FUNCTION__, av_err2str_new(ret));
        return 1;
    }

    if (gotPacket)
    {
        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(&pkt, codecCtx->time_base, stream->time_base);
        pkt.stream_index = stream->index;

        /* Write the compressed frame to the media file. */
        //logPacket(outFmtCtx, &pkt);
        ret = av_interleaved_write_frame(outFmtCtx, &pkt);
    }
    else
        ret = 0;

    if (ret < 0)
    {
        lprintf("Error in %s while writing video frame: %s\n", __FUNCTION__, av_err2str_new(ret));
        return 1;
    }

    return (frame || gotPacket) ? 0 : 1;
}

static char* videoEncodeSpeedString[] = { "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow" };

const char* getVideoEncodeSpeedString(int videoEncodeSpeed)
{
    if (videoEncodeSpeed < 0 || videoEncodeSpeed > 8)
        videoEncodeSpeed = 4;
    return videoEncodeSpeedString[videoEncodeSpeed];
}

void cvtOptions(const std::vector<avp::Option>& src, AVDictionary** dst)
{
    int size = src.size();
    for (int i = 0; i < size; i++)
        av_dict_set(dst, src[i].first.c_str(), src[i].second.c_str(), 0);
}

int getSampleTypeNumBytes(int sampleType)
{
    int ret = 0;
    switch (sampleType)
    {
    case avp::SampleType8U:
    case avp::SampleType8UP:
        ret = 1;
        break;
    case avp::SampleType16S:
    case avp::SampleType16SP:
        ret = 2;
        break;
    case avp::SampleType32S:
    case avp::SampleType32SP:
    case avp::SampleType32F:
    case avp::SampleType32FP:
        ret = 4;
        break;
    case avp::SampleType64F:
    case avp::SampleType64FP:
        ret = 8;
        break;
    default:
        break;
    }
    return ret;
}

template<typename ElemType>
void cvtPlanarToPackedInternal(const unsigned char* const* srcData,
    int numChannels, int numSamples, unsigned char* dstData)
{
    std::vector<const ElemType*> ptrVec(numChannels);
    for (int i = 0; i < numChannels; i++)
        ptrVec[i] = (const ElemType*)srcData[i];
    const ElemType** ptrSrc = &ptrVec[0];
    ElemType* ptrDst = (ElemType*)dstData;

    for (int i = 0; i < numSamples; i++)
    {
        for (int j = 0; j < numChannels; j++)
        {
            *(ptrDst++) = *(ptrSrc[j]++);
        }
    }
}

void cvtPlanarToPacked(const unsigned char* const* srcData, int sampleFormat,
    int numChannels, int numSamples, unsigned char* dstData)
{
    switch (sampleFormat)
    {
    case AV_SAMPLE_FMT_U8P:
        cvtPlanarToPackedInternal<unsigned char>(srcData, numChannels, numSamples, dstData);
        break;
    case AV_SAMPLE_FMT_S16P:
        cvtPlanarToPackedInternal<short>(srcData, numChannels, numSamples, dstData);
        break;
    case AV_SAMPLE_FMT_S32P:
        cvtPlanarToPackedInternal<int>(srcData, numChannels, numSamples, dstData);
        break;
    case AV_SAMPLE_FMT_FLTP:
        cvtPlanarToPackedInternal<float>(srcData, numChannels, numSamples, dstData);
        break;
    case AV_SAMPLE_FMT_DBLP:
        cvtPlanarToPackedInternal<double>(srcData, numChannels, numSamples, dstData);
        break;
    default:
        break;
    }
}

void copyAudioData(const unsigned char* const * srcData, unsigned char* dstData, int dstStep,
    int sampleFormat, int numChannels, int numSamples)
{
    int elemNumBytes = av_get_bytes_per_sample((enum AVSampleFormat)sampleFormat);
    if (av_sample_fmt_is_planar((enum AVSampleFormat)sampleFormat))
    {
        for (int i = 0; i < numChannels; i++)
            memcpy(dstData + dstStep * i, srcData[i], elemNumBytes * numSamples);
    }
    else
    {
        memcpy(dstData, srcData[0], elemNumBytes * numSamples * numChannels);
    }
}

void copyAudioData(const unsigned char* srcData, int srcStep, int srcPos,
    unsigned char* dstData, int dstStep, int dstPos,
    int sampleFormat, int numChannels, int numSamples)
{
    int elemNumBytes = av_get_bytes_per_sample((enum AVSampleFormat)sampleFormat);
    if (av_sample_fmt_is_planar((enum AVSampleFormat)sampleFormat))
    {
        for (int i = 0; i < numChannels; i++)
            memcpy(dstData + dstStep * i + dstPos * elemNumBytes, 
                   srcData + srcStep * i + srcPos * elemNumBytes, 
                   elemNumBytes * numSamples);
    }
    else
    {
        memcpy(dstData + dstPos * elemNumBytes * numChannels, 
               srcData + srcPos * elemNumBytes * numChannels, 
               elemNumBytes * numSamples * numChannels);
    }
}

void copyAudioData(const unsigned char* srcData, int srcStep, int srcPos,
    unsigned char** dstData, int dstStep, int dstPos,
    int sampleFormat, int numChannels, int numSamples)
{
    int elemNumBytes = av_get_bytes_per_sample((enum AVSampleFormat)sampleFormat);
    if (av_sample_fmt_is_planar((enum AVSampleFormat)sampleFormat))
    {
        for (int i = 0; i < numChannels; i++)
            memcpy(dstData[i] + dstPos * elemNumBytes,
                   srcData + srcStep * i + srcPos * elemNumBytes,
                   elemNumBytes * numSamples);
    }
    else
    {
        memcpy(dstData[0] + dstPos * elemNumBytes * numChannels,
            srcData + srcPos * elemNumBytes * numChannels,
            elemNumBytes * numSamples * numChannels);
    }
}

void copyAudioData(const unsigned char** srcData, int srcPos, unsigned char** dstData, int dstPos,
    int sampleFormat, int numChannels, int numSamples)
{
    int elemNumBytes = av_get_bytes_per_sample((enum AVSampleFormat)sampleFormat);
    if (av_sample_fmt_is_planar((enum AVSampleFormat)sampleFormat))
    {
        for (int i = 0; i < numChannels; i++)
            memcpy(dstData[i] + dstPos * elemNumBytes,
                   srcData[i] + srcPos * elemNumBytes,
                   elemNumBytes * numSamples);
    }
    else
    {
        memcpy(dstData[0] + dstPos * elemNumBytes * numChannels,
            srcData[0] + srcPos * elemNumBytes * numChannels,
            elemNumBytes * numSamples * numChannels);
    }
}

void setDataPtr(unsigned char* src, int srcStep, int numChannels, int sampleType, unsigned char* dst[8])
{
    if (sampleType <= avp::SampleType64F)
    {
        dst[0] = src;
        for (int i = 1; i < 8; i++)
            dst[i] = 0;
    }
    else
    {
        for (int i = 0; i < 8; i++)
        {
            if (i < numChannels)
                dst[i] = src + srcStep * i;
            else
                dst[i] = 0;
        }
    }
}

#ifndef AV_WB32
#   define AV_WB32(p, darg) do {                \
        unsigned d = (darg);                    \
        ((uint8_t*)(p))[3] = (d);               \
        ((uint8_t*)(p))[2] = (d) >> 8;          \
        ((uint8_t*)(p))[1] = (d) >> 16;         \
        ((uint8_t*)(p))[0] = (d) >> 24;         \
} while (0)
#endif

#ifndef AV_RB16
#   define AV_RB16(x)                  \
    ((((const uint8_t*)(x))[0] << 8) | \
      ((const uint8_t*)(x))[1])
#endif

static int alloc_and_copy(uint8_t **poutbuf, int *poutbuf_size, int *poutbuf_capacity,
    const uint8_t *sps_pps, uint32_t sps_pps_size, const uint8_t *in, uint32_t in_size)
{
    uint32_t offset = *poutbuf_size;
    uint8_t nal_header_size = offset ? 3 : 4;
    uint8_t *pout = NULL;
    int err;

    *poutbuf_size += sps_pps_size + in_size + nal_header_size;
    if (*poutbuf_size + AV_INPUT_BUFFER_PADDING_SIZE > *poutbuf_capacity) {
        *poutbuf_capacity = (*poutbuf_size) + AV_INPUT_BUFFER_PADDING_SIZE;
        if ((pout = (uint8_t*)realloc(*poutbuf, *poutbuf_capacity)) == 0) {
            free(*poutbuf);
            *poutbuf = NULL;
            *poutbuf_capacity = 0;
            *poutbuf_size = 0;
            err = AVERROR(ENOMEM);
            return err;
        }
        else {
            *poutbuf = pout;
        }
    }
    if (sps_pps)
        memcpy(*poutbuf + offset, sps_pps, sps_pps_size);
    memcpy(*poutbuf + sps_pps_size + nal_header_size + offset, in, in_size);
    if (!offset) {
        AV_WB32(*poutbuf + sps_pps_size, 1);
    }
    else {
        (*poutbuf + offset + sps_pps_size)[0] =
            (*poutbuf + offset + sps_pps_size)[1] = 0;
        (*poutbuf + offset + sps_pps_size)[2] = 1;
    }

    return 0;
}
