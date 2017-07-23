#include "AudioVideoProcessor.h"
#include "AudioVideoGlobal.h"
#include "AudioVideoStream.h"
//#include "CheckRTSPConnect.h"
#include "boost/algorithm/string.hpp"

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

static char err_buf[AV_ERROR_MAX_STRING_SIZE];
#define av_err2str_new(errnum) \
    av_make_error_string(err_buf, AV_ERROR_MAX_STRING_SIZE, errnum)

static char time_str_buf[AV_TS_MAX_STRING_SIZE];
#define av_ts2timestr_new(ts, tb) av_ts_make_time_string(time_str_buf, ts, tb)
#define av_ts2str_new(ts) av_ts_make_string(time_str_buf, ts)

namespace avp
{

struct AudioVideoWriter3::Impl
{
    Impl();
    ~Impl();

    void initAll();
    bool open(const std::string& fileName, const std::string& formatName, bool useExternTimeStamp,
        const std::vector<OutputStreamProperties>& props, const std::vector<Option>& options = std::vector<Option>());
    bool write(const AudioVideoFrame2& frame, int index);
    void close();

    AVFormatContext* fmtCtx;
    std::vector<std::unique_ptr<StreamWriter> > streams;
    int useExternTimeStamp;
    long long int firstTimeStamp;
	int firstTimeStampSet;
    int isOpened;
};

AudioVideoWriter3::Impl::Impl()
{
    initAll();
}

AudioVideoWriter3::Impl::~Impl()
{
    close();
}

void AudioVideoWriter3::Impl::initAll()
{
    fmtCtx = 0;
    streams.clear();
    useExternTimeStamp = 0;
    firstTimeStamp = -1LL;
	firstTimeStampSet = 0;
    isOpened = 0;
}

bool AudioVideoWriter3::Impl::open(const std::string& fileName, const std::string& formatName, bool externTimeStamp,
    const std::vector<OutputStreamProperties>& props, const std::vector<Option>& options)
{
    close();

    int numStreams = props.size();
    for (int i = 0; i < numStreams; i++)
    {
        const OutputStreamProperties& prop = props[i];
        if (prop.mediaType == AUDIO)
        {
            if (prop.sampleType < SampleType8U && prop.sampleType > SampleType64FP)
                return false;
        }
        else if (prop.mediaType == VIDEO)
        {
            if (prop.width < 0 || prop.width & 1 || prop.height < 0 || prop.height & 1 ||
                (prop.pixelType != PixelTypeBGR24 && prop.pixelType != PixelTypeBGR32))
                return false;
        }
    }

    //if (boost::istarts_with(fileName, "rtsp") || boost::iequals(formatName, "rtsp"))
    //{
    //    if (!checkRTSPConnect(fileName))
    //    {
    //        lprintf("Error, cannot connect to url %s\n", fileName.c_str());
    //        return false;
    //    }
    //}

    int ret;

    const char* theFormatName = NULL;
    if (formatName.size())
        theFormatName = formatName.c_str();

    /* allocate the output media context */
    avformat_alloc_output_context2(&fmtCtx, NULL, theFormatName, fileName.c_str());
    if (!fmtCtx)
    {
        lprintf("Error, could not alloc output format context\n");
        goto FAIL;
    }

    for (int i = 0; i < numStreams; i++)
    {
        const OutputStreamProperties& prop = props[i];
        if (prop.mediaType == AUDIO)
        {
            AudioStreamWriter* audioStream = new AudioStreamWriter;
            if (!audioStream->open(fmtCtx, prop.format, externTimeStamp, &firstTimeStamp,
                prop.sampleType, prop.channelLayout, prop.sampleRate, prop.bitRate, options))
            {
                lprintf("Error open audio stream.\n");
                goto FAIL;
            }
            streams.push_back(std::unique_ptr<StreamWriter>());
            streams.back().reset((StreamWriter*)audioStream);
        }
        else if (prop.mediaType == VIDEO)
        {
            VideoStreamWriter* videoStream = 0;
            //if (prop.format == "h264_qsv" || prop.format == "mpeg2_qsv")
            //    videoStream = new QsvVideoStreamWriter;
            //else
                videoStream = new BuiltinCodecVideoStreamWriter;
            if (!videoStream->open(fmtCtx, prop.format, externTimeStamp, &firstTimeStamp,
                prop.pixelType, prop.width, prop.height, prop.frameRate, prop.bitRate, options))
            {
                lprintf("Error open video stream.\n");
                goto FAIL;
            }
            streams.push_back(std::unique_ptr<StreamWriter>());
            streams.back().reset((StreamWriter*)videoStream);
        }
    }

    av_dump_format(fmtCtx, 0, fileName.c_str(), 1);

    /* open the output file, if needed */
    if (!(fmtCtx->oformat->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&fmtCtx->pb, fileName.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            lprintf("Could not open '%s': %s\n", fileName,
                av_err2str_new(ret));
            goto FAIL;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(fmtCtx, NULL);
    if (ret < 0)
    {
        lprintf("Error occurred when opening output file: %s\n",
            av_err2str_new(ret));
        goto FAIL;
    }

    useExternTimeStamp = externTimeStamp;
    isOpened = 1;
    return true;
FAIL:
    close();
    return false;
}

bool AudioVideoWriter3::Impl::write(const AudioVideoFrame2& frame, int index)
{
    if (!isOpened)
        return false;

    if (frame.mediaType != AUDIO && frame.mediaType != VIDEO)
        return false;

    if (index < 0 || index >= streams.size())
        return false;

    if (useExternTimeStamp)
    {
        //if (frame.timeStamp < 0)
        //{
        //    lprintf("Current frame time stamp negative, skip writing this frame\n");
        //    return true;
        //}

        if (!firstTimeStampSet)
        {
            firstTimeStamp = frame.timeStamp;
            lprintf("Use extern time stamp, first pts %s %lld\n",
                frame.mediaType == AUDIO ? "audio" : "video", frame.timeStamp);
			firstTimeStampSet = 1;
        }

        if (frame.timeStamp < firstTimeStamp)
        {
            lprintf("Current frame time stamp smaller than first time stampe, skip writing this frame\n");
            return true;
        }
    }

    return streams[index]->writeFrame(frame);
}

void AudioVideoWriter3::Impl::close()
{
    int numStreams = streams.size();
    for (int i = 0; i < numStreams; i++)
        streams[i]->close();
    streams.clear();

    if (isOpened && fmtCtx)
        av_write_trailer(fmtCtx);

    if (fmtCtx && !(fmtCtx->oformat->flags & AVFMT_NOFILE))
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

AudioVideoWriter3::AudioVideoWriter3()
{
    ptrImpl.reset(new Impl);
}

bool AudioVideoWriter3::open(const std::string& fileName, const std::string& formatName, bool useExternTimeStamp,
    const std::vector<OutputStreamProperties>& props, const std::vector<Option>& options)
{
    initFFMPEG();
    return ptrImpl->open(fileName, formatName, useExternTimeStamp, props, options);
}

bool AudioVideoWriter3::write(const AudioVideoFrame2& frame, int index)
{
    return ptrImpl->write(frame, index);
}

void AudioVideoWriter3::close()
{
    ptrImpl->close();
}

}