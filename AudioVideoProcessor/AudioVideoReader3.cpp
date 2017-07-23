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
#ifdef __cplusplus
}
#endif

static bool contains(const std::vector<int>& arr, int target)
{
    if (arr.empty())
        return false;

    int size = arr.size();
    for (int i = 0; i < size; i++)
    {
        if (arr[i] == target)
            return true;
    }
    return false;
}

namespace avp
{

struct AudioVideoReader3::Impl
{
    Impl();
    ~Impl();
    void init();
    bool open(const std::string& fileName, const std::vector<int>& indexes,
        int sampleType, int pixelType, const std::string& formatName = std::string(),
        const std::vector<Option>& options = std::vector<Option>());
    bool read(AudioVideoFrame2& frame, int& index);
    bool seek(long long int timeStamp, int index);
    void getProperties(int index, InputStreamProperties& prop);
    void close();

    AVFormatContext* fmtCtx;
    std::vector<std::unique_ptr<StreamReader> > streams;
    int isOpened;
};

AudioVideoReader3::Impl::Impl()
{
    init();
}

AudioVideoReader3::Impl::~Impl()
{
    close();
}

void AudioVideoReader3::Impl::init()
{
    fmtCtx = 0;
    streams.clear();
    isOpened = 0;
}

bool AudioVideoReader3::Impl::open(const std::string& fileName, const std::vector<int>& indexes,
    int sampleType, int pixelType, const std::string& formatName, const std::vector<Option>& options)
{
    close();

    AVInputFormat* inputFormat = av_find_input_format(formatName.c_str());
    if (inputFormat)
    {
        lprintf("Input format %s found, used to open file %s\n", formatName.c_str(), fileName.c_str());
    }

    AVDictionary* dict = NULL;
    cvtOptions(options, &dict);

    int ret;

    /* open input file, and allocate format context */
    if (avformat_open_input(&fmtCtx, fileName.c_str(), inputFormat, &dict) < 0)
    {
        lprintf("Could not open source file %s\n", fileName.c_str());
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

    int numStreams = fmtCtx->nb_streams;
    int numIndexes = indexes.size();
    for (int i = 0; i < numIndexes; i++)
    {
        if (indexes[i] < 0 || indexes[i] >= numStreams)
        {
            lprintf("Index out of bound, cannot open corresponding stream\n");
            goto FAIL;
        }
        int mediaType = fmtCtx->streams[indexes[i]]->codec->codec_type;
        if (mediaType != AVMEDIA_TYPE_VIDEO && mediaType != AVMEDIA_TYPE_AUDIO)
        {
            lprintf("Index corresponds to a non-audio or non-video stream, cannot open it\n");
            goto FAIL;
        }
    }

    for (int i = 0; i < numStreams; i++)
    {
        streams.push_back(std::unique_ptr<StreamReader>());
        if (contains(indexes, i))
        {
            int mediaType = fmtCtx->streams[i]->codec->codec_type;
            if (mediaType == AVMEDIA_TYPE_AUDIO)
            {
                AudioStreamReader* stream = new AudioStreamReader;
                if (stream->open(fmtCtx, i, sampleType))
                {
                    streams.back().reset((StreamReader*)stream);
                }
                else
                {
                    lprintf("Could not open audio stream for reading.\n");
                    goto FAIL;
                }
            }
            else if (mediaType == AVMEDIA_TYPE_VIDEO)
            {
                VideoStreamReader* stream = new BuiltinCodecVideoStreamReader;
                if (stream->open(fmtCtx, i, pixelType))
                {
                    streams.back().reset((StreamReader*)stream);
                }
                else
                {
                    lprintf("Could not open video stream for reading.\n");
                    goto FAIL;
                }
            }
        }
    }
    
    /* dump input information to stderr */
    av_dump_format(fmtCtx, 0, fileName.c_str(), 0);
    
    isOpened = 1;
    return true;

FAIL:
    close();
    return false;
}

bool AudioVideoReader3::Impl::read(AudioVideoFrame2& frame, int& index)
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
    while (true)
    {
        int readPacketOK = (av_read_frame(fmtCtx, &pkt) >= 0);
        if (readPacketOK)
        {
            pktIndex = pkt.stream_index;
            index = pktIndex;
            if (streams[pktIndex])
            {
                if (streams[pktIndex]->readFrame(pkt, frame))
                    return true;
            }
            else
                av_free_packet(&pkt);
        }
        else
        {
            av_init_packet(&pkt);
            pkt.data = NULL;
            pkt.size = 0;
            int numStreams = fmtCtx->nb_streams;
            for (int i = 0; i < numStreams; i++)
            {
                index = i;
                if (streams[i])
                {
                    if (streams[i]->readFrame(pkt, frame))
                        return true;
                }
            }
            return false;
        }
    }

    return false;
}

bool AudioVideoReader3::Impl::seek(long long int timeStamp, int index)
{
    if (!isOpened)
        return false;

    int numStreams = fmtCtx->nb_streams;
    if (index < 0 || index >= numStreams)
        return false;

    if (!streams[index])
        return false;

    // Seeking a frame directly after opening a file without any reading of a frame
    // MAY RESULT IN A SOUGHT FRAME WITH INACCURATE TIME STAMP. 
    // Video stream seeking has been tested, audio not
    // It would be better to have a read flage in this Impl class indicating
    // that whether a single frame has been read from a specific stream
    int streamIndex;
    AudioVideoFrame2 frame;
    while (read(frame, streamIndex))
    {
        if (index == streamIndex)
            break;
    }

    int ret = 0;
    AVStream* stream = fmtCtx->streams[index];
    long long int streamTimeStamp;
    if (stream->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        streamTimeStamp = av_rescale_q(timeStamp, avrational(1, AV_TIME_BASE), stream->time_base);
    else
    {
        long long int prevTimeStamp = timeStamp - 1000000.0 / av_q2d(stream->r_frame_rate);
        streamTimeStamp = av_rescale_q(prevTimeStamp, avrational(1, AV_TIME_BASE), stream->time_base);
        if (stream->start_time != AV_NOPTS_VALUE && prevTimeStamp < stream->start_time)
            prevTimeStamp = stream->start_time;
    }
    ret = av_seek_frame(fmtCtx, stream->index, streamTimeStamp, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
    {
        lprintf("Error, seeking in audio stream failed\n");
        return false;
    }
    for (int i = 0; i < numStreams; i++)
    {
        if (streams[i])
            avcodec_flush_buffers(fmtCtx->streams[i]->codec);
    }

    if (stream->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        return true;
    else if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        AVStream* stream = fmtCtx->streams[index];
        double fps = av_q2d(stream->r_frame_rate);
        long long int tsIncUnit = 1000000.0 / fps + 0.5;
        long long int halfTsIncUnit = 500000.0 / fps + 0.5;
        long long int oneAndHalfTsIncUnit = tsIncUnit + halfTsIncUnit;
        int count = 0;
        bool directReturn = true;
        while (true)
        {
            AudioVideoFrame2 frame;
            int streamIndex;
            if (!read(frame, streamIndex))
            {
                lprintf("Error, seeking in video stream failed, maybe cannot find target frame when file end met\n");
                return false;
            }
            if (frame.mediaType == VIDEO && streamIndex == index)
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
            av_seek_frame(fmtCtx, index, streamTimeStamp, AVSEEK_FLAG_BACKWARD);
            for (int i = 0; i < numStreams; i++)
            {
                if (streams[i])
                    avcodec_flush_buffers(fmtCtx->streams[i]->codec);
            }
            // NOTICE!!!
            // After calling av_seek_frame, the file handle may not directly point to video stream.
            // It is possible that when this seek function returns, the first call of read
            // returns an audio frame.
            for (int i = 0; i < count - 1;)
            {
                AudioVideoFrame2 frame;
                int streamIndex;
                read(frame, streamIndex);
                if (frame.mediaType == VIDEO && streamIndex == index)
                    i++;
            }
            return true;
        }
    }
    else
        return true;
}

void AudioVideoReader3::Impl::getProperties(int index, InputStreamProperties& prop)
{
    if (!isOpened)
    {
        prop = InputStreamProperties();
        return;
    }

    int numStreams = fmtCtx->nb_streams;
    if (index < 0 || index >= numStreams)
    {
        prop = InputStreamProperties();
        return;
    }

    if (!streams[index])
    {
        prop = InputStreamProperties();
        return;
    }

    streams[index]->getProperties(prop);
}

void AudioVideoReader3::Impl::close()
{
    int size = streams.size();
    for (int i = 0; i < size; i++)
    {
        if (streams[i])
            streams[i]->close();
    }
    streams.clear();

    if (fmtCtx)
        avformat_close_input(&fmtCtx);

    init();
}

void AudioVideoReader3::getStreamProperties(const std::string& fileName, std::vector<InputStreamProperties>& props,
    const std::string& formatName,  const std::vector<Option>& options)
{
    initFFMPEG();

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
            props.push_back(InputStreamProperties(s->nb_frames, c->sample_fmt, c->sample_rate,
                c->channels, c->channel_layout, c->frame_size));
        }
        else if (c->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            props.push_back(InputStreamProperties(s->nb_frames, c->pix_fmt, c->width, c->height,
                av_q2d(s->r_frame_rate)));
        }
        else
            props.push_back(InputStreamProperties());
    }

END:
    av_dict_free(&dict);
    avformat_close_input(&fmtCtx);
}

AudioVideoReader3::AudioVideoReader3()
{
    ptrImpl.reset(new Impl);
}

bool AudioVideoReader3::open(const std::string& fileName, const std::vector<int>& indexes, int sampleType, int pixelType,
    const std::string& formatName, const std::vector<Option>& options)
{
    return ptrImpl->open(fileName, indexes, sampleType, pixelType, formatName, options);
}

bool AudioVideoReader3::read(AudioVideoFrame2& frame, int& index)
{
    return ptrImpl->read(frame, index);
}

bool AudioVideoReader3::seek(long long int timeStamp, int index)
{
    return ptrImpl->seek(timeStamp, index);
}

void AudioVideoReader3::getProperties(int index, InputStreamProperties& prop)
{
    ptrImpl->getProperties(index, prop);
}

void AudioVideoReader3::close()
{
    ptrImpl->close();
}

}