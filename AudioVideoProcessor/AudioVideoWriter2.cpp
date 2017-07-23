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
struct AudioVideoWriter2::Impl
{
    Impl();
    ~Impl();

    void initAll();
    bool open(const std::string& fileName, const std::string& formatName, bool externTimeStamp,
        bool openAudio, const std::string& audioFormat, int sampleType, int channelLayout, int sampleRate, int audioBPS,
        bool openVideo, const std::string& videoFormat, int pixelType, int width, int height, double frameRate, int videoBPS,
        const std::vector<Option>& options = std::vector<Option>());
    bool write(AudioVideoFrame2& frame);
    void close();

    AVFormatContext* fmtCtx;
    std::unique_ptr<VideoStreamWriter> videoStream;
    std::unique_ptr<AudioStreamWriter> audioStream;
    int useExternTimeStamp;
    long long int firstTimeStamp;
    int firstTimeStampSet;
    int isOpened;
};

AudioVideoWriter2::Impl::Impl()
{
    initAll();
}

AudioVideoWriter2::Impl::~Impl()
{
    close();
}

void AudioVideoWriter2::Impl::initAll()
{
    fmtCtx = 0;
    videoStream.reset(0);
    audioStream.reset(0);
    useExternTimeStamp = 0;
    firstTimeStamp = -1LL;
    firstTimeStampSet = 0;
    isOpened = 0;
}

bool AudioVideoWriter2::Impl::open(const std::string& fileName, const std::string& formatName, bool externTimeStamp,
    bool openAudio, const std::string& audioFormat, int audioSampleType, int audioChannelLayout, int audioSampleRate, int audioBPS,
    bool openVideo, const std::string& videoFormat, int videoPixelType, int videoWidth, int videoHeight, double videoFrameRate, int videoBPS,
    const std::vector<Option>& options)
{
    close();

    if (!openAudio && !openVideo)
    {
        lprintf("Error in %s, at least one of audio and video should be opened\n", __FUNCTION__);
        return false;
    }

    if (openAudio && (audioSampleRate <= 0 || audioBPS <= 0))
    {
        lprintf("Error in %s, audio sample rate and audio bit rate should be larger than zero\n", __FUNCTION__);
        return false;
    }

    if (openAudio &&
        (audioSampleType < SampleType8U && audioSampleType > SampleType64FP))
    {
        lprintf("Error in %s, sample format invalid\n", __FUNCTION__);
        return false;
    }

    if (openVideo && (videoFrameRate <= 0 || videoBPS <= 0))
    {
        lprintf("Error in %s, video frame rate and video bit rate should be larger than zero\n", __FUNCTION__);
        return false;
    }

    if (openVideo && (videoWidth < 0 || videoWidth & 1 || videoHeight < 0 || videoHeight & 1))
    {
        lprintf("Error in %s, video width and height should be positive even numbers, "
            "but now %d %d\n", __FUNCTION__, videoWidth, videoHeight);
        return false;
    }

    if (openVideo && (!isInterfacePixelType(videoPixelType)))
    {
        lprintf("Error in %s, invalid pixel type %d\n", __FUNCTION__, videoPixelType);
        return false;
    }

    //if (boost::istarts_with(fileName, "rtsp") || boost::iequals(formatName, "rtsp"))
    //{
    //    if (!checkRTSPConnect(fileName))
    //    {
    //        lprintf("Error in %s, cannot connect to url %s\n", __FUNCTION__, fileName.c_str());
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
        lprintf("Error in %s, could not alloc output format context\n", __FUNCTION__);
        goto FAIL;
    }

    if (openAudio)
    {
        audioStream.reset(new AudioStreamWriter);
        if (!audioStream->open(fmtCtx, audioFormat, externTimeStamp, &firstTimeStamp,
            audioSampleType, audioChannelLayout, audioSampleRate, audioBPS, options))
        {
            lprintf("Error in %s, could not open audio stream\n", __FUNCTION__);
            goto FAIL;
        }
    }

    if (openVideo)
    {
        //if (videoFormat == "h264_qsv" || videoFormat == "mpeg2_qsv")
        //    videoStream.reset(new QsvVideoStreamWriter);
        //else if (videoFormat == "nvenc_h264" || videoFormat == "nvenc_hevc")
        //    videoStream.reset(new NvVideoStreamWriter);
        //else
            videoStream.reset(new BuiltinCodecVideoStreamWriter);
        if (!videoStream->open(fmtCtx, videoFormat, externTimeStamp, &firstTimeStamp,
            videoPixelType, videoWidth, videoHeight, videoFrameRate, videoBPS, options))
        {
            lprintf("Error in %s, could not open video stream\n", __FUNCTION__);
            goto FAIL;
        }
    }

    av_dump_format(fmtCtx, 0, fileName.c_str(), 1);

    /* open the output file, if needed */
    if (!(fmtCtx->oformat->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&fmtCtx->pb, fileName.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            lprintf("Error in %s, could not open '%s': %s\n", __FUNCTION__, fileName,
                av_err2str_new(ret));
            goto FAIL;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(fmtCtx, NULL);
    if (ret < 0)
    {
        lprintf("Error occurred in %s when opening output file: %s\n",
            __FUNCTION__, av_err2str_new(ret));
        goto FAIL;
    }

    useExternTimeStamp = externTimeStamp;
    isOpened = 1;
    return true;
FAIL:
    close();
    return false;
}

bool AudioVideoWriter2::Impl::write(AudioVideoFrame2& frame)
{
    if (!isOpened)
        return false;

    if (!frame.data[0])
    {
        lprintf("Error in %s, frame data NULL, cannot write\n", __FUNCTION__);
        return false;
    }

    if (frame.mediaType != AUDIO && frame.mediaType != VIDEO)
    {
        lprintf("Error in %s, frame neither audio nor video, cannot write\n", __FUNCTION__);
        return false;
    }

    if (frame.mediaType == AUDIO && !audioStream)
    {
        lprintf("Warning in %s, audio stream not opened, skip writing this audio frame\n", __FUNCTION__);
        return true;
    }

    if (frame.mediaType == VIDEO && !videoStream)
    {
        lprintf("Warning in %s, video stream not opened, skip writing this video frame\n", __FUNCTION__);
        return true;
    }

    if (useExternTimeStamp)
    {
        if (!firstTimeStampSet)
        {
            firstTimeStamp = frame.timeStamp;
            lprintf("Info in %s, use extern time stamp, first pts %s %lld\n",
                __FUNCTION__, frame.mediaType == AUDIO ? "audio" : "video", frame.timeStamp);
            firstTimeStampSet = 1;
        }

        if (frame.timeStamp < firstTimeStamp)
        {
            lprintf("Info in %s, current frame time stamp %lld smaller than first time stamp %lld, skip writing this frame\n", 
                __FUNCTION__, frame.timeStamp, firstTimeStamp);
            return true;
        }
    }

    if (frame.mediaType == AUDIO && audioStream)
    {
        return audioStream->writeFrame(frame);
    }

    if (frame.mediaType == VIDEO && videoStream)
    {
        return videoStream->writeFrame(frame);
    }

    return false;
}

void AudioVideoWriter2::Impl::close()
{
    // IMPORTANT NOTICE!!!!!!
    // Closing audioStream and resetting it to zero should not include the condition isOpened == true.
    // Just need to check whether audioStream is empty.
    // Remember that audioStream->close() should be called before fmtCtx is freed.
    // If the following if condition is (isOpened && audioStream), 
    // the if condition code is skipped, and in the final initAll(),
    // audioStream.reset(0) is called, inside audioStream->close() is called,
    // where a freed fmtCtx is accessed to write the buffered frames to the stream, 
    // causing memory access violation.
    if (audioStream)
    {
        audioStream->close();
        audioStream.reset(0);
    }

    // Should not be if (isOpened && videoStream)
    if (videoStream)
    {
        videoStream->close();
        videoStream.reset(0);
    }

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

AudioVideoWriter2::AudioVideoWriter2()
{
    ptrImpl.reset(new Impl);
}

bool AudioVideoWriter2::open(const std::string& fileName, const std::string& formatName, bool useExternTimeStamp,
    bool openAudio, const std::string& audioFormat, int sampleType, int numChannels, int sampleRate, int audioBPS,
    bool openVideo, const std::string& videoFormat, int pixelType, int width, int height, double frameRate, int videoBPS,
    const std::vector<Option>& options)
{
    initFFMPEG();
    return ptrImpl->open(fileName, formatName, useExternTimeStamp,
        openAudio, audioFormat, sampleType, numChannels, sampleRate, audioBPS,
        openVideo, videoFormat, pixelType, width, height, frameRate, videoBPS,
        options);
}

bool AudioVideoWriter2::write(AudioVideoFrame2& frame)
{
    return ptrImpl->write(frame);
}

void AudioVideoWriter2::close()
{
    ptrImpl->close();
}

}