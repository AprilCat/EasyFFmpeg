#include "AudioVideoProcessor.h"
#include "Timer.h"
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include <thread>

void copy()
{
    cv::Mat src(1000, 2000, CV_8UC4), dst(1000, 2000, CV_8UC4);
    Timer t;
    int count = 100;
    for (int i = 0; i < count; i++)
    {
        src.copyTo(dst);
    }
    t.end();
    printf("time = %f\n", t.elapse() / count);
}

// 1 just test simple audio video reading Using AudioVideoReader
int main1()
{
    avp::AudioVideoFrame avFrame;
    avp::AudioVideoReader avReader;
    std::vector<avp::Option> options;
    bool ok;

    options.push_back(std::make_pair("rtsp_transport", "tcp"));
    ok = avReader.open(/*"F:\\video\\BBC.Frozen.Planet.S01E01.Chi_Eng.HR-HDTV.AC3.1024X576.x264.mkv"*/
        /*"F:\\video\\高鑫投篮一分钟20140603.MOV"*/
        /*"F:\\panovideo\\test\\test5\\YDXJ0076.mp4"*/
        "rtsp://192.168.1.113:554/test.sdp"
        /*"E:\\Projects\\GitRepo\\panoCore\\PanoStitcher\\build\\x64\\Release\\temp1.mp4"*/, 
        false, true, avp::PixelTypeBGR24, "", options);
    long long int lastTS = 0;
    while (avReader.read(avFrame))
    {
        if (avFrame.mediaType == avp::AUDIO)
        {
            printf("audio frame, ts = %lld, diff = %lld, idx = %d, numSamples = %d, numChannels = %d\n",
                avFrame.timeStamp, avFrame.timeStamp - lastTS, avFrame.frameIndex, avFrame.numSamples, avFrame.numChannels);
            lastTS = avFrame.timeStamp;
        }
        if (avFrame.mediaType == avp::VIDEO)
        {
            printf("video frame, ts = %lld, idx = %d\n", avFrame.timeStamp, avFrame.frameIndex);
            cv::Mat image(avFrame.height, avFrame.width, CV_8UC3, avFrame.data, avFrame.step);
            cv::imshow("image", image);
            cv::waitKey(1);
        }
    }
    avReader.close();
    return 0;
}

// 2 simple reading and writing using AudioVideoReader and AudioVideoWriter
int main2()
{
    avp::AudioVideoFrame avFrame;
    avp::AudioVideoReader avReader;
    avp::AudioVideoWriter avWriter;
    std::vector<avp::Option> options;
    bool ok;

    ok = avReader.open("F:\\panovideo\\test\\test1\\YDXJ0136.mp4"
        /*"F:\\panovideo\\test\\test6\\YDXJ0072.mp4"*/
        /*"F:\\video\\VIDEO0008.mp4"*/,
        true, true, avp::PixelTypeBGR24);
    int width = avReader.getVideoWidth();
    int height = avReader.getVideoHeight();
    double fps = avReader.getVideoFrameRate();
    int sampleType = avReader.getAudioSampleType();
    int numChannels = avReader.getAudioNumChannels();
    int channelLayout = avReader.getAudioChannelLayout();
    int sampleRate = avReader.getAudioSampleRate();
    options.clear();
    //options.push_back(std::make_pair("preset", "faster"));
    //options.push_back(std::make_pair("ar", "48000"));
    ok = avWriter.open("test22.mp4", "", false,
        true, "aac", sampleType, channelLayout, sampleRate, 64000,
        true, "h264", avp::PixelTypeBGR24, width, height, fps, 10000000, options);
    //avReader.seek(1000000.0 / avReader.getVideoFrameRate() * 15 + 0.5, avp::VIDEO);
    int writeCount = 0;
    while (avReader.read(avFrame))
    {
        if (avFrame.mediaType == avp::AUDIO)
        {
            printf("audio frame, ts = %lld, numSamples = %d, numChannels = %d\n",
                avFrame.timeStamp, avFrame.numSamples, avFrame.numChannels);
            avWriter.write(avFrame);
            writeCount++;
        }
        else if (avFrame.mediaType == avp::VIDEO)
        {
            avWriter.write(avFrame);
            //printf("video frame, ts = %lld\n", avFrame.timeStampInMilliSec);
            //cv::Mat show(avFrame.height, avFrame.width, CV_8UC3, avFrame.data, avFrame.step);
            //cv::imshow("show", show);
            //cv::waitKey(20);
            writeCount++;
        }
        if (writeCount > 1000)
            break;
    }

    avReader.close();
    avWriter.close();
    return 0;
}

//3 Direct Show audio video reading and writing using AudioVideoReader and AudioVideoWriter
int main3()
{
    avp::AudioVideoFrame avFrame;
    avp::AudioVideoReader avReader;
    std::vector<avp::Option> options;
    bool ok;

    options.push_back(std::make_pair("video_size", "1280x720"));
    //options.push_back(std::make_pair("framerate", "15"));
    ok = avReader.open("video=RICOH THETA S"/*"audio=数字音频接口 (2- XI100DUSB-HDMI Audio:video=XI100DUSB-HDMI Video"*/,
        false, true, avp::PixelTypeBGR24, "dshow", options);
    if (!ok)
    {
        printf("Could not open input\n");
        return 0;
    }
    //int width = avReader.getVideoWidth();
    //int height = avReader.getVideoHeight();
    //int fps = avReader.getVideoFps() + 0.5;
    //int sampleType = avReader.getAudioSampleType();
    //int numChannels = avReader.getAudioNumChannels();
    //int channelLayout = avReader.getAudioChannelLayout();
    //int sampleRate = avReader.getAudioSampleRate();
    //options.clear();
    //options.push_back(std::make_pair("preset", "slow"));
    //options.push_back(std::make_pair("ar", "44100"));
    //ok = avWriter.open("testsmall.flv", "", true,
    //    false, "", sampleType, channelLayout, sampleRate, 128000,
    //    true, "h264", avp::PixelTypeBGR24, width, height, fps, 5000000, options);
    //if (!ok)
    //{
    //    printf("Could not open output\n");
    //    return 0;
    //}
    //int writeVideoCount = 0;
    //while (avReader.read(avFrame))
    //{
    //    if (avFrame.mediaType == avp::AUDIO)
    //    {
    //        avWriter.write(avFrame);
    //        printf("audio frame, ts = %lld, numSamples = %d, numChannels = %d\n",
    //            avFrame.timeStamp, avFrame.numSamples, avFrame.numChannels);
    //    }
    //    else if (avFrame.mediaType == avp::VIDEO)
    //    {
    //        writeVideoCount++;
    //        avWriter.write(avFrame);
    //        printf("video frame, ts = %lld\n", avFrame.timeStamp);
    //        cv::Mat show(avFrame.height, avFrame.width, CV_8UC3, avFrame.data, avFrame.step);
    //        cv::imshow("show", show);
    //        cv::waitKey(1);
    //    }
    //    if (writeVideoCount > fps * 5)
    //        break;
    //}
    return 0;
}

// 4 
int main()
{
    avp::AudioVideoFrame2 avFrame;
    avp::AudioVideoReader2 avReader2;
    avp::AudioVideoReader3 avReader3;
    avp::AudioVideoWriter3 avWriter3;
    std::vector<avp::Option> options;
    bool ok;
    
    std::string inFile = "F:\\视频\\VIDEO0008.mp4";
    std::string outFile = "test22.mp4";

    //ok = avReader2.open(inFile, true, avp::SampleTypeUnknown, true, avp::PixelTypeBGR24);
    //int width = avReader2.getVideoWidth();
    //int height = avReader2.getVideoHeight();
    //double fps = avReader2.getVideoFrameRate();
    //int sampleType = avReader2.getAudioSampleType();
    //int numChannels = avReader2.getAudioNumChannels();
    //int channelLayout = avReader2.getAudioChannelLayout();
    //int sampleRate = avReader2.getAudioSampleRate();
    //std::vector<avp::OutputStreamProperties> props(3);
    //props[0] = avp::OutputStreamProperties("h264", avp::PixelTypeBGR24, width, height, fps, 4000000);
    //props[1] = avp::OutputStreamProperties("h264", avp::PixelTypeBGR24, width / 2, height / 2, fps, 1000000);
    //props[2] = avp::OutputStreamProperties("aac", sampleType, channelLayout, sampleRate, 128000);
    //options.clear();
    //options.push_back(std::make_pair("preset", "faster"));
    //ok = avWriter3.open(outFile, "", true, props, options);
    ////avReader.seek(1000000.0 / avReader.getVideoFps() * 5 + 0.5, avp::VIDEO);
    //while (avReader2.read(avFrame))
    //{
    //    if (avFrame.mediaType == avp::AUDIO)
    //    {
    //        printf("audio frame, ts = %lld, numSamples = %d, numChannels = %d\n",
    //            avFrame.timeStamp, avFrame.numSamples, avFrame.numChannels);
    //        avWriter3.write(avFrame, 2);
    //    }
    //    else if (avFrame.mediaType == avp::VIDEO)
    //    {
    //        cv::Mat src(avFrame.height, avFrame.width, CV_8UC3, avFrame.data[0], avFrame.steps[0]);
    //        cv::Mat dst;
    //        cv::resize(src, dst, cv::Size(width / 2, height / 2));
    //        avWriter3.write(avFrame, 0);
    //        //avFrame = avp::videoFrame(dst.data, dst.step, avFrame.pixelType, dst.cols, dst.rows, avFrame.timeStamp);
    //        unsigned char* data[8] = { dst.data, 0 };
    //        int steps[8] = { dst.step[0], 0 };
    //        avFrame = avp::AudioVideoFrame2(data, steps, avFrame.pixelType, dst.cols, dst.rows, avFrame.timeStamp);
    //        avWriter3.write(avFrame, 1);
    //        //printf("video frame, ts = %lld\n", avFrame.timeStampInMilliSec);
    //        //cv::Mat show(avFrame.height, avFrame.width, CV_8UC3, avFrame.data, avFrame.step);
    //        //cv::imshow("show", show);
    //        //cv::waitKey(20);
    //    }
    //}

    avReader2.close();
    avWriter3.close();

    std::vector<avp::InputStreamProperties> inProps;
    avp::AudioVideoReader3::getStreamProperties(outFile, inProps);
    std::vector<int> indexes;
    indexes.push_back(0);
    indexes.push_back(1);
    indexes.push_back(2);
    avReader3.open(outFile, indexes, avp::SampleTypeUnknown, avp::PixelTypeBGR24);
    int index;
    while (avReader3.read(avFrame, index))
    {
        if (avFrame.mediaType == avp::AUDIO)
        {
            printf("audio frame, ts = %lld, numSamples = %d, numChannels = %d\n",
                avFrame.timeStamp, avFrame.numSamples, avFrame.numChannels);
        }
        else if (avFrame.mediaType == avp::VIDEO)
        {
            char buf[64];
            sprintf(buf, "image[%d]", index);
            cv::Mat show(avFrame.height, avFrame.width, CV_8UC3, avFrame.data[0], avFrame.steps[0]);
            cv::imshow(buf, show);
            cv::waitKey(20);
        }
    }
    return 0;
}

void noLog(void*, int, const char*, va_list)
{

}

// 5 test seek function of AudioVideoReader
int main5()
{
    avp::setFFmpegLogCallback(noLog);
    bool ok = false;
    avp::AudioVideoReader r;
    avp::AudioVideoFrame f;

    for (int i = 0; i < 150; i++)
    {
        ok = r.open("F:\\panovideo\\test\\test1\\YDXJ0136.mp4", true, true, avp::PixelTypeBGR24);
        long long int ts = i * 1000000.0 / 48;
        ok = r.seek(ts, avp::VIDEO);
        while (true)
        {
            ok = r.read(f);
            if (f.mediaType == avp::VIDEO)
                break;
        }
        printf("seek video frame ts = %lld, actual ts = %lld, diff = %lld\n", ts, f.timeStamp, ts - f.timeStamp);
        r.close();
    }

    ok = r.open("F:\\panovideo\\test\\test1\\YDXJ0136.mp4", true, true, avp::PixelTypeBGR24);
    int videoCount = 0;
    while (videoCount < 200)
    {
        ok = r.read(f);
        if (!ok)
            break;
        if (f.mediaType == avp::VIDEO)
        {
            videoCount++;
            printf("ts = %lld\n", f.timeStamp);
        }
    }
    //for (int i = 0; i < 10; i++)
    //{
    //    ok = r.read(f);
    //    printf("%s, pts = %lld\n", f.mediaType == avp::VIDEO ? "video" : "audio", f.timeStamp);
    //}
    //ok = r.read(f);
    //while (r.read(f))
    //{
    //    if (f.mediaType == avp::VIDEO)
    //        break;
    //}
    //printf("first video frame ts = %lld\n", f.timeStamp);

    //long long int ts = 146 * 1000000.0 / 48;
    //ok = r.seek(ts, avp::VIDEO);
    //while (true)
    //{
    //    ok = r.read(f);
    //    if (f.mediaType == avp::VIDEO)
    //        break;
    //}
    //printf("seek video frame ts = %lld, actual ts = %lld\n", ts, f.timeStamp);    

    //for (int i = 14; i < 100; i++)
    //{
    //    long long int ts = i * 1000000.0 / 48;
    //    ok = r.seek(ts, avp::VIDEO);
    //    while (true)
    //    {
    //        ok = r.read(f);
    //        if (f.mediaType == avp::VIDEO)
    //            break;
    //    }
    //    printf("seek video frame ts = %lld, actual ts = %lld\n", ts, f.timeStamp);
    //}
    //cv::Mat show(f.height, f.width, CV_8UC3, f.data, f.step);
    //cv::imshow("image", show);
    //cv::waitKey(0);
    r.close();
    return 0;
}

static void compareVideoFrame(const avp::SharedAudioVideoFrame& lhs, const avp::SharedAudioVideoFrame& rhs)
{
    if (lhs.mediaType != avp::VIDEO || rhs.mediaType != avp::VIDEO)
        return;
    if (lhs.width != rhs.width || lhs.height != rhs.height)
        return;

    int width = lhs.width, height = lhs.height;
    int lNumChannels = lhs.pixelType == avp::PixelTypeBGR24 ? 3 : 4;
    int rNumChannels = rhs.pixelType == avp::PixelTypeBGR24 ? 3 : 4;
    for (int i = 0; i < height; i++)
    {
        const unsigned char* lptr = lhs.data + lhs.step * i;
        const unsigned char* rptr = rhs.data + rhs.step * i;
        for (int j = 0; j < width; j++)
        {
            if (lptr[0] != rptr[0] ||
                lptr[1] != rptr[1] ||
                lptr[2] != rptr[2])
            {
                printf("diff at x = %d, y = %d, lhs(%3d, %3d, %3d), rhs(%3d, %3d, %3d)\n",
                    j, i, lptr[0], lptr[1], lptr[2], rptr[0], rptr[1], rptr[2]);
            }
            lptr += lNumChannels;
            rptr += rNumChannels;
        }
    }
}

// 6 another program to check AudioVideoReader::seek
int main6()
{
    avp::setFFmpegLogCallback(noLog);
    bool ok = false;
    avp::AudioVideoReader r;
    avp::AudioVideoFrame f;

    std::string fileName = 
        "F:\\panovideo\\test\\test1\\YDXJ0136.mp4";

    avp::SharedAudioVideoFrame sf1, sf2;

    ok = r.open(fileName, true, true, avp::PixelTypeBGR24);
    while (true)
    {
        r.read(f);
        if (f.mediaType == avp::VIDEO)
                break;
    }
    sf1 = f;
    r.close();

    ok = r.open(fileName, true, true, avp::PixelTypeBGR24);
    r.seekByIndex(0, avp::VIDEO);
    while (true)
    {
        r.read(f);
        if (f.mediaType == avp::VIDEO)
            break;
    }
    sf2 = f;
    r.close();

    compareVideoFrame(sf1, sf2);

    //return 0;

    for (int i = 0; i < 150; i++)
    {
        ok = r.open(fileName, true, true, avp::PixelTypeBGR24);
        ok = r.seekByIndex(i, avp::VIDEO);
        while (true)
        {
            ok = r.read(f);
            if (f.mediaType == avp::VIDEO)
                break;
        }
        printf("seek video frame idx = %d, actual idx = %d, diff = %d\n", i, f.frameIndex, i - f.frameIndex);
        r.close();
    }

    ok = r.open(fileName, true, true, avp::PixelTypeBGR24);
    int videoCount = 0;
    while (videoCount < 200)
    {
        ok = r.read(f);
        if (!ok)
            break;
        if (f.mediaType == avp::VIDEO)
        {
            videoCount++;
            printf("ts = %lld, idx = %d\n", f.timeStamp, f.frameIndex);
        }
    }

    for (int i = 0; i < 150; i++)
    {
        ok = r.open(fileName, true, true, avp::PixelTypeBGR24);
        ok = r.seekByIndex(i, avp::AUDIO);
        while (true)
        {
            ok = r.read(f);
            if (f.mediaType == avp::AUDIO)
                break;
        }
        printf("seek audio frame idx = %d, actual idx = %d, diff = %d\n", i, f.frameIndex, i - f.frameIndex);
        r.close();
    }

    ok = r.open(fileName, true, true, avp::PixelTypeBGR24);
    int audioCount = 0;
    while (audioCount < 200)
    {
        ok = r.read(f);
        if (!ok)
            break;
        if (f.mediaType == avp::AUDIO)
        {
            audioCount++;
            printf("ts = %lld, idx = %d\n", f.timeStamp, f.frameIndex);
        }
    }

    return 0;
}

// 7 test AudioVideoReader::readTo
int main7()
{
    avp::setFFmpegLogCallback(noLog);
    bool ok = false;
    avp::AudioVideoReader r;

    std::string fileName = "F:\\video\\BBC.Frozen.Planet.S01E01.Chi_Eng.HR-HDTV.AC3.1024X576.x264.mkv"
        /*"F:\\panovideo\\test\\test1\\YDXJ0136.mp4"*/;
    int pixelType = avp::PixelTypeBGR24;
    ok = r.open(fileName, true, true, pixelType);
    if (!ok)
    {
        printf("open failed\n");
        return 0;
    }

    int width = r.getVideoWidth();
    int height = r.getVideoHeight();

    int numSamples = r.getAudioNumSamples();
    int sampleType = r.getAudioSampleType();
    int numChannels = r.getAudioNumChannels();
    int channelLayout = r.getAudioChannelLayout();
    if (!numSamples)
    {
        avp::AudioVideoFrame f;
        while (true)
        {
            ok = r.read(f);
            if (f.mediaType == avp::AUDIO)
                break;
        }
        numSamples = r.getAudioNumSamples();
    }

    avp::SharedAudioVideoFrame saf = avp::sharedAudioFrame(sampleType, numChannels, channelLayout, numSamples);
    avp::SharedAudioVideoFrame svf = avp::sharedVideoFrame(pixelType, width, height);
    avp::AudioVideoFrame af = saf, vf = svf;
    int mediaType;
    for (int i = 0; i < 1000; i++)
    {
        ok = r.readTo(af, vf, mediaType);
        if (!ok)
            break;
        if (mediaType == avp::VIDEO)
            printf("video frame, ts = %lld, idx = %d\n", vf.timeStamp, vf.frameIndex);
        else if (mediaType == avp::AUDIO)
            printf("audio frame, ts = %lld, idx = %d\n", af.timeStamp, af.frameIndex);
    }

    return 0;
}

// 8 test AudioVideoReader and AudioVideoWriter
int main8()
{
    avp::AudioVideoFrame avFrame;
    avp::AudioVideoReader avReader;
    avp::AudioVideoWriter avWriter;
    std::vector<avp::Option> options;
    bool ok;
    Timer t;

    ok = avReader.open("F:\\panovideo\\stabilized_1.avi",
        false, true, avp::PixelTypeBGR24);
    int width = avReader.getVideoWidth();
    int height = avReader.getVideoHeight();
    double fps = avReader.getVideoFrameRate();
    ok = avWriter.open("test.mp4", "", false,
        false, "", 0, 0, 0, 0,
        true, "h264", avp::PixelTypeBGR24, width, height, fps, 8000000, options);
    while (avReader.read(avFrame))
    {
        if (avFrame.mediaType == avp::AUDIO)
        {
            //printf("audio frame, ts = %lld, numSamples = %d, numChannels = %d\n",
            //    avFrame.timeStamp, avFrame.numSamples, avFrame.numChannels);
            avWriter.write(avFrame);
        }
        else if (avFrame.mediaType == avp::VIDEO)
        {
            t.start();
            avWriter.write(avFrame);
            t.end();
            printf("time = %f\n", t.elapse());
        }
    }

    avReader.close();
    avWriter.close();
    return 0;
}

const int channelHeight = 180;

void drawAudioFrame(const float* data, int size, cv::Mat& image)
{
    int height = channelHeight;
    int width = size;
    image.create(height, width, CV_8UC1);
    image.setTo(0);
    cv::Scalar color(255, 255, 255);
    for (int i = 0; i < width - 1; i++)
        cv::line(image, cv::Point(i, height * (1 - data[i]) * 0.5), cv::Point(i + 1, height * (1 - data[i + 1]) * 0.5), color);
}

void drawAudioFrame(const double* data, int size, cv::Mat& image)
{
    int height = channelHeight;
    int width = size;
    image.create(height, width, CV_8UC1);
    image.setTo(0);
    cv::Scalar color(255, 255, 255);
    for (int i = 0; i < width - 1; i++)
        cv::line(image, cv::Point(i, height * (1 - data[i]) * 0.5), cv::Point(i + 1, height * (1 - data[i + 1]) * 0.5), color);
}

void drawAudioFrame(const cv::Mat& audio, cv::Mat& image)
{
    if (!audio.data || ((audio.depth() != CV_32F) && (audio.depth() != CV_64F)))
        return;

    int height = channelHeight * audio.rows;
    int width = audio.cols;
    image.create(height, width, CV_8UC1);
    image.setTo(0);
    cv::Scalar color(255, 255, 255);
    if (audio.depth() == CV_32F)
    {
        for (int i = 0; i < audio.rows; i++)
        {
            cv::Mat curr = image.rowRange(i * channelHeight, (i + 1) * channelHeight);
            drawAudioFrame(audio.ptr<float>(i), width, curr);
        }
    }
    else
    {
        for (int i = 0; i < audio.rows; i++)
        {
            cv::Mat curr = image.rowRange(i * channelHeight, (i + 1) * channelHeight);
            drawAudioFrame(audio.ptr<double>(i), width, curr);
        }
    }
}

// 9 test AudioVideoReader3's read and readTo
int main9()
{
    std::string fileName = /*"F:\\panovideo\\test\\test1\\YDXJ0136.mp4"*/
        "rtmp://192.168.1.113/live/rtmptest";
        /*"F:\\QQRecord\\452103256\\FileRecv\\VID_20160825_123702.mp4"*/
        /*"F:\\video\\BBC.Frozen.Planet.S01E01.Chi_Eng.HR-HDTV.AC3.1024X576.x264.mkv"*/
        /*"E:\\Projects\\IntelMediaSDKSamples\\_bin\\content\\test_stream.264"*/
        /*"out.h264"*/
        /*"test22.flv"*/;
    
    avp::AudioVideoReader2 avReader;
    bool ok;

    Timer t;
    int vCount = 0;
    // simple read
    avp::AudioVideoFrame2 avFrame;
    ok = avReader.open(fileName, false, avp::SampleTypeUnknown, true, avp::PixelTypeUnknown);
    long long int lastTimeStamp = 0;
    t.start();
    while (avReader.read(avFrame))
    {
        if (avFrame.mediaType == avp::AUDIO)
        {
            printf("audio, pts = %lld, idx = %d\n", avFrame.timeStamp, avFrame.frameIndex);
        }
        else if (avFrame.mediaType == avp::VIDEO)
        {
            printf("video, pts = %lld, pts diff = %lld, idx = %d\n", 
                avFrame.timeStamp, avFrame.timeStamp - lastTimeStamp, avFrame.frameIndex);
            lastTimeStamp = avFrame.timeStamp;
            cv::Mat image(avFrame.height, avFrame.width, CV_8UC1, avFrame.data[0], avFrame.steps[0]);
            cv::imshow("image", image);
            cv::waitKey(1);
            vCount++;
        }
        //if (vCount == 500)
        //    break;
    }
    t.end();
    printf("t = %f\n", t.elapse());
    avReader.close();
    return 0;

    // read and show
    /*avp::AudioVideoFrame2 avFrame;
    ok = avReader.open(fileName, true, avp::SampleType64FP, true, avp::PixelTypeYUV420P);
    cv::Mat audioImage;
    while (avReader.read(avFrame))
    {
        if (avFrame.mediaType == avp::AUDIO)
        {
            printf("audio, pts = %lld, idx = %d\n", avFrame.timeStamp, avFrame.frameIndex);
            if (avFrame.sampleType == avp::SampleType32FP)
            {
                cv::Mat a(avFrame.numChannels, avFrame.numSamples, CV_32FC1, avFrame.data[0], avFrame.steps[0]);
                drawAudioFrame(a, audioImage);
                cv::imshow("audio", audioImage);
            }
            else if (avFrame.sampleType == avp::SampleType64FP)
            {
                cv::Mat a(avFrame.numChannels, avFrame.numSamples, CV_64FC1, avFrame.data[0], avFrame.steps[0]);
                drawAudioFrame(a, audioImage);
                cv::imshow("audio", audioImage);
            }
        }
        else if (avFrame.mediaType == avp::VIDEO)
        {
            printf("video, pts = %lld, idx = %d\n", avFrame.timeStamp, avFrame.frameIndex);
            if (avFrame.pixelType == avp::PixelTypeBGR24)
            {
                cv::Mat image(avFrame.height, avFrame.width, CV_8UC3, avFrame.data[0], avFrame.steps[0]);
                cv::imshow("rgb", image);
            }
            else if (avFrame.pixelType == avp::PixelTypeBGR32)
            {
                cv::Mat image(avFrame.height, avFrame.width, CV_8UC4, avFrame.data[0], avFrame.steps[0]);
                cv::imshow("rgb0", image);
            }
            else if (avFrame.pixelType == avp::PixelTypeYUV420P)
            {
                cv::Mat y(avFrame.height, avFrame.width, CV_8UC1, avFrame.data[0], avFrame.steps[0]);
                cv::Mat u(avFrame.height / 2, avFrame.width / 2, CV_8UC1, avFrame.data[1], avFrame.steps[1]);
                cv::Mat v(avFrame.height / 2, avFrame.width / 2, CV_8UC1, avFrame.data[2], avFrame.steps[2]);
                cv::imshow("y", y);
                cv::imshow("u", u);
                cv::imshow("v", v);
            }
            else if (avFrame.pixelType == avp::PixelTypeNV12)
            {
                cv::Mat y(avFrame.height, avFrame.width, CV_8UC1, avFrame.data[0], avFrame.steps[0]);
                cv::Mat uv(avFrame.height / 2, avFrame.width, CV_8UC1, avFrame.data[1], avFrame.steps[1]);
                cv::imshow("y", y);
                cv::imshow("uv", uv);
            }
        }
        int key = cv::waitKey(1);
        if (key == 'q')
            break;
    }
    avReader.close();

    return 0;*/

    // read to externally allocated memory
    bool openAudio = false;
    //fileName = "rtmp://192.168.1.107/live/rtmptest";
    //fileName = "E:\\Projects\\IntelMediaSDKSamples\\_bin\\content\\test_stream.264";
    //fileName = "out.h264";
    fileName = "rtmp://192.168.1.113/live/rtmptest";
    ok = avReader.open(fileName, false, avp::SampleType32FP, true, avp::PixelTypeYUV420P);
    int width = avReader.getVideoWidth();
    int height = avReader.getVideoHeight();
    int pixelType = avReader.getVideoPixelType();
    int numSamples = avReader.getAudioNumSamples();
    int sampleType = avReader.getAudioSampleType();
    int numChannels = avReader.getAudioNumChannels();
    int channelLayout = avReader.getAudioChannelLayout();
    printf("num audio frames = %d, num video frames = %d\n", 
        avReader.getAudioNumFrames(), avReader.getVideoNumFrames());
    if (!numSamples && openAudio)
    {
        avp::AudioVideoFrame2 f;
        while (true)
        {
            ok = avReader.read(f);
            printf("trying... %s, pts = %lld, idx = %d\n", f.mediaType == avp::VIDEO ? "video" : "audio", f.timeStamp, f.frameIndex);
            if (f.mediaType == avp::AUDIO)
                break;
        }
        numSamples = avReader.getAudioNumSamples();
    }

    avp::AudioVideoFrame2 aFrame(sampleType, numChannels, channelLayout, numSamples);
    avp::AudioVideoFrame2 vFrame(pixelType, width, height);

    cv::Mat audioImage;
    int mediaType;
    int videoFrameCount = 0;
    while (avReader.readTo(aFrame, vFrame, mediaType))
    {
        if (mediaType == avp::AUDIO)
        {
            printf("audio, pts = %lld, idx = %d, num samples = %d\n", aFrame.timeStamp, aFrame.frameIndex, aFrame.numSamples);
            if (aFrame.sampleType == avp::SampleType32FP)
            {
                cv::Mat a(aFrame.numChannels, aFrame.numSamples, CV_32FC1, aFrame.data[0], aFrame.steps[0]);
                drawAudioFrame(a, audioImage);
                cv::imshow("audio", audioImage);
            }
            else if (aFrame.sampleType == avp::SampleType64FP)
            {
                cv::Mat a(aFrame.numChannels, aFrame.numSamples, CV_64FC1, aFrame.data[0], aFrame.steps[0]);
                drawAudioFrame(a, audioImage);
                cv::imshow("audio", audioImage);
            }
        }
        else if (mediaType == avp::VIDEO)
        {
            videoFrameCount++;
            printf("video, pts = %lld, idx = %d, count = %d\n", vFrame.timeStamp, vFrame.frameIndex, videoFrameCount);
            if (vFrame.pixelType == avp::PixelTypeBGR24)
            {
                cv::Mat image(vFrame.height, vFrame.width, CV_8UC3, vFrame.data[0], vFrame.steps[0]);
                cv::imshow("rgb", image);
            }
            else if (vFrame.pixelType == avp::PixelTypeBGR32)
            {
                cv::Mat image(vFrame.height, vFrame.width, CV_8UC4, vFrame.data[0], vFrame.steps[0]);
                cv::imshow("rgb0", image);
            }
            else if (vFrame.pixelType == avp::PixelTypeYUV420P)
            {
                cv::Mat y(vFrame.height, vFrame.width, CV_8UC1, vFrame.data[0], vFrame.steps[0]);
                cv::Mat u(vFrame.height / 2, vFrame.width / 2, CV_8UC1, vFrame.data[1], vFrame.steps[1]);
                cv::Mat v(vFrame.height / 2, vFrame.width / 2, CV_8UC1, vFrame.data[2], vFrame.steps[2]);
                cv::imshow("y", y);
                cv::imshow("u", u);
                cv::imshow("v", v);
            }
            else if (vFrame.pixelType == avp::PixelTypeNV12)
            {
                cv::Mat y(vFrame.height, vFrame.width, CV_8UC1, vFrame.data[0], vFrame.steps[0]);
                cv::Mat uv(vFrame.height / 2, vFrame.width, CV_8UC1, vFrame.data[1], vFrame.steps[1]);
                cv::imshow("y", y);
                cv::imshow("uv", uv);
            }
        }
        else
        {
            printf("other\n");
        }
        //int key = cv::waitKey(0);
        //if (key == 'q')
        //    break;
    }
    avReader.close();

    return 0;
}

// 10 test AudioVideoReader3 and AudioVideoWriter3
int main10()
{
    std::string fileName = "F:\\panovideo\\test\\test1\\YDXJ0094.mp4"
        /*"F:\\panovideo\\test\\test1\\out.mp4"*/
        /*"F:\\video\\oneplusone.mp4"*/;
    avp::AudioVideoFrame2 avFrame;
    avp::AudioVideoReader2 avReader;
    avp::AudioVideoWriter2 avWriter;
    std::vector<avp::Option> opts;
    bool ok;

    ok = avReader.open(fileName, true, avp::SampleTypeUnknown, true, avp::PixelTypeNV12);
    if (!ok)
    {
        printf("cannot open file for read\n");
        return 0;
    }
    int width = avReader.getVideoWidth();
    int height = avReader.getVideoHeight();
    int pixelType = avReader.getVideoPixelType();
    double frameRate = avReader.getVideoFrameRate();
    int sampleType = avReader.getAudioSampleType();
    int numChannels = avReader.getAudioNumChannels();
    int channelLayout = avReader.getAudioChannelLayout();
    int sampleRate = avReader.getAudioSampleRate();

    opts.push_back(std::make_pair("profile", "main"));
    opts.push_back(std::make_pair("preset", "medium"));

    ok = avWriter.open("testlibx264.mp4", "", true,
        true, "aac", sampleType, channelLayout, sampleRate, 64000,
        true, "h264", pixelType, width, height, frameRate, 8000000, opts);
    if (!ok)
    {
        printf("cannot open file for write\n");
        return 0;
    }
    int writeCount = 0;
    while (avReader.read(avFrame))
    {
        printf("%s, ts = %lld, idx = %d\n", 
            avFrame.mediaType == avp::VIDEO ? "video" : "audio", avFrame.timeStamp, avFrame.frameIndex);
        avWriter.write(avFrame);
        writeCount++;
        //if (writeCount > 500)
        //    break;
    }
    avReader.close();
    avWriter.close();

    return 0;
}

static void compareVideoFrame(const avp::AudioVideoFrame2& lhs, const avp::AudioVideoFrame2& rhs)
{
    if (lhs.mediaType != avp::VIDEO || rhs.mediaType != avp::VIDEO)
    {
        printf("%s, skip\n", __FUNCTION__);
        return;
    }
    if ((lhs.pixelType != avp::PixelTypeBGR24 && lhs.pixelType != avp::PixelTypeBGR32) ||
        (rhs.pixelType != avp::PixelTypeBGR24 && rhs.pixelType != avp::PixelTypeBGR32))
    {
        printf("%s, skip\n", __FUNCTION__);
        return;
    }
    if (lhs.width != rhs.width || lhs.height != rhs.height)
    {
        printf("%s, skip\n", __FUNCTION__);
        return;
    }

    int width = lhs.width, height = lhs.height;
    int lNumChannels = lhs.pixelType == avp::PixelTypeBGR24 ? 3 : 4;
    int rNumChannels = rhs.pixelType == avp::PixelTypeBGR24 ? 3 : 4;
    for (int i = 0; i < height; i++)
    {
        const unsigned char* lptr = lhs.data[0] + lhs.steps[0] * i;
        const unsigned char* rptr = rhs.data[0] + rhs.steps[0] * i;
        for (int j = 0; j < width; j++)
        {
            if (lptr[0] != rptr[0] ||
                lptr[1] != rptr[1] ||
                lptr[2] != rptr[2])
            {
                printf("diff at x = %d, y = %d, lhs(%3d, %3d, %3d), rhs(%3d, %3d, %3d)\n",
                    j, i, lptr[0], lptr[1], lptr[2], rptr[0], rptr[1], rptr[2]);
            }
            lptr += lNumChannels;
            rptr += rNumChannels;
        }
    }
}

// 11 test AudioVideoReader3's seek and seekByIndex
int main11()
{
    avp::setFFmpegLogCallback(noLog);
    bool ok = false;
    avp::AudioVideoReader2 r;
    avp::AudioVideoFrame2 f;

    std::string fileName = 
        "F:\\panovideo\\test\\test1\\YDXJ0136.mp4";

    avp::AudioVideoFrame2 sf1, sf2;

    ok = r.open(fileName, true, avp::SampleTypeUnknown, true, avp::PixelTypeBGR24);
    while (true)
    {
        r.read(f);
        if (f.mediaType == avp::VIDEO)
            break;
    }
    sf1 = f.clone();
    r.close();

    ok = r.open(fileName, true, avp::SampleTypeUnknown, true, avp::PixelTypeBGR24);
    r.seekByIndex(0, avp::VIDEO);
    while (true)
    {
        r.read(f);
        if (f.mediaType == avp::VIDEO)
            break;
    }
    sf2 = f.clone();
    r.close();

    compareVideoFrame(sf1, sf2);

    //return 0;

    for (int i = 0; i < 150; i++)
    {
        ok = r.open(fileName, true, avp::SampleTypeUnknown, true, avp::PixelTypeBGR24);
        ok = r.seekByIndex(i, avp::VIDEO);
        while (true)
        {
            ok = r.read(f);
            if (f.mediaType == avp::VIDEO)
                break;
        }
        printf("seek video frame idx = %d, actual idx = %d, diff = %d\n", i, f.frameIndex, i - f.frameIndex);
        r.close();
    }

    ok = r.open(fileName, true, avp::SampleTypeUnknown, true, avp::PixelTypeBGR24);
    int videoCount = 0;
    while (videoCount < 200)
    {
        ok = r.read(f);
        if (!ok)
            break;
        if (f.mediaType == avp::VIDEO)
        {
            videoCount++;
            printf("ts = %lld, idx = %d\n", f.timeStamp, f.frameIndex);
        }
    }

    for (int i = 0; i < 150; i++)
    {
        ok = r.open(fileName, true, avp::SampleTypeUnknown, true, avp::PixelTypeBGR24);
        ok = r.seekByIndex(i, avp::AUDIO);
        while (true)
        {
            ok = r.read(f);
            if (f.mediaType == avp::AUDIO)
                break;
        }
        printf("seek audio frame idx = %d, actual idx = %d, diff = %d\n", i, f.frameIndex, i - f.frameIndex);
        r.close();
    }

    ok = r.open(fileName, true, avp::SampleTypeUnknown, true, avp::PixelTypeBGR24);
    int audioCount = 0;
    while (audioCount < 200)
    {
        ok = r.read(f);
        if (!ok)
            break;
        if (f.mediaType == avp::AUDIO)
        {
            audioCount++;
            printf("ts = %lld, idx = %d\n", f.timeStamp, f.frameIndex);
        }
    }

    return 0;
}

// 12 test Direct Show reading using AudioVideoReader3
int main12()
{
    std::vector<avp::Device> ds, ads, vds;
    avp::listDirectShowDevices(ds);
    avp::keepAudioDirectShowDevices(ds, ads);
    avp::keepVideoDirectShowDevices(ds, vds);
    if (ads.empty() || vds.empty())
    {
        printf("no direct show video device found\n");
        return 0;
    }

    std::vector<avp::VideoDeviceOption> vopts;
    avp::listVideoDirectShowDeviceOptions(vds[0], vopts);

    std::string name = "video=" + vds[0].shortName + ":audio=" + ads[0].shortName;
    std::vector<avp::Option> opts;
    opts.push_back(std::make_pair("video_size", "1280x720"));
    opts.push_back(std::make_pair("video_device_number", vds[0].numString));
    opts.push_back(std::make_pair("audio_device_number", vds[0].numString));
    avp::AudioVideoReader2 avReader;
    avReader.open(name, true, avp::SampleType32FP, true, avp::PixelTypeYUV420P, "dshow", opts);
    avp::AudioVideoFrame2 avFrame;

    cv::Mat audioImage;
    while (avReader.read(avFrame))
    {
        if (avFrame.mediaType == avp::AUDIO)
        {
            printf("audio, pts = %lld, idx = %d\n", avFrame.timeStamp, avFrame.frameIndex);
            if (avFrame.sampleType == avp::SampleType32FP)
            {
                cv::Mat a(avFrame.numChannels, avFrame.numSamples, CV_32FC1, avFrame.data[0], avFrame.steps[0]);
                drawAudioFrame(a, audioImage);
                cv::imshow("audio", audioImage);
            }
            else if (avFrame.sampleType == avp::SampleType64FP)
            {
                cv::Mat a(avFrame.numChannels, avFrame.numSamples, CV_64FC1, avFrame.data[0], avFrame.steps[0]);
                drawAudioFrame(a, audioImage);
                cv::imshow("audio", audioImage);
            }
        }
        else if (avFrame.mediaType == avp::VIDEO)
        {
            printf("video, pts = %lld, idx = %d\n", avFrame.timeStamp, avFrame.frameIndex);
            if (avFrame.pixelType == avp::PixelTypeBGR24)
            {
                cv::Mat image(avFrame.height, avFrame.width, CV_8UC3, avFrame.data[0], avFrame.steps[0]);
                cv::imshow("rgb", image);
            }
            else if (avFrame.pixelType == avp::PixelTypeBGR32)
            {
                cv::Mat image(avFrame.height, avFrame.width, CV_8UC4, avFrame.data[0], avFrame.steps[0]);
                cv::imshow("rgb0", image);
            }
            else if (avFrame.pixelType == avp::PixelTypeYUV420P)
            {
                cv::Mat y(avFrame.height, avFrame.width, CV_8UC1, avFrame.data[0], avFrame.steps[0]);
                cv::Mat u(avFrame.height / 2, avFrame.width / 2, CV_8UC1, avFrame.data[1], avFrame.steps[1]);
                cv::Mat v(avFrame.height / 2, avFrame.width / 2, CV_8UC1, avFrame.data[2], avFrame.steps[2]);
                cv::imshow("y", y);
                cv::imshow("u", u);
                cv::imshow("v", v);
            }
            else if (avFrame.pixelType == avp::PixelTypeNV12)
            {
                cv::Mat y(avFrame.height, avFrame.width, CV_8UC1, avFrame.data[0], avFrame.steps[0]);
                cv::Mat uv(avFrame.height / 2, avFrame.width, CV_8UC1, avFrame.data[1], avFrame.steps[1]);
                cv::imshow("y", y);
                cv::imshow("uv", uv);
            }
        }
        int key = cv::waitKey(1);
        if (key == 'q')
            break;
    }
    avReader.close();

    return 0;
}

// 13 write yuv to file 
int main13()
{
    bool ok = false;
    avp::AudioVideoReader2 r;
    avp::AudioVideoFrame2 f;

    std::string fileName = 
        "F:\\panovideo\\test\\test1\\YDXJ0136.mp4";

    //FILE* file = fopen("1280x960.yuv", "wb");
    ok = r.open(fileName, false, avp::SampleTypeUnknown, true, avp::PixelTypeYUV420P);
    int count = 0;
    while (r.read(f))
    {
        printf("index = %d\n", f.frameIndex);
        //if (f.mediaType == avp::VIDEO)
        //{
        //    for (int i = 0; i < f.height; i++)
        //        fwrite(f.data[0] + i * f.steps[0], 1, f.width, file);
        //    for (int i = 0; i < f.height / 2; i++)
        //        fwrite(f.data[1] + i * f.steps[1], 1, f.width / 2, file);
        //    for (int i = 0; i < f.height / 2; i++)
        //        fwrite(f.data[2] + i * f.steps[2], 1, f.width / 2, file);
        //}
    }
    //fclose(file);
    return 0;
}

// 14 show frame index
int main14()
{
    std::string fileName = "F:\\panovideo\\test\\SP7\\1-7.MP4"
        /*"F:\\panovideo\\test\\test1\\YDXJ0136.mp4"*/
        /*"F:\\video\\oneplusone.mp4"*/;
    avp::AudioVideoFrame2 avFrame;
    avp::AudioVideoReader2 avReader;
    bool ok;

    ok = avReader.open(fileName, false, avp::SampleTypeUnknown, true, avp::PixelTypeNV12);
    if (!ok)
    {
        printf("cannot open file for read\n");
        return 0;
    }
    int videoNumFrames = avReader.getVideoNumFrames();
    int width = avReader.getVideoWidth();
    int height = avReader.getVideoHeight();
    int pixelType = avReader.getVideoPixelType();
    double frameRate = avReader.getVideoFrameRate();
    int sampleType = avReader.getAudioSampleType();
    int numChannels = avReader.getAudioNumChannels();
    int channelLayout = avReader.getAudioChannelLayout();
    int sampleRate = avReader.getAudioSampleRate();
    printf("video num frames %d\n", videoNumFrames);

    int lastIndex = -1;
    int diffAbnormalCount = 0;
    while (avReader.read(avFrame))
    {
        printf("%s, ts = %lld, idx = %d\n", 
            avFrame.mediaType == avp::VIDEO ? "video" : "audio", avFrame.timeStamp, avFrame.frameIndex);
        if (avFrame.frameIndex - lastIndex != 1)
        {
            printf("...............................................................\n");
            diffAbnormalCount++;
        }            
        lastIndex = avFrame.frameIndex;
    }
    avReader.close();
    printf("diff abnormal count %d\n", diffAbnormalCount);

    return 0;
}

// 15
int main15()
{
    std::string fileName = "F:\\panovideo\\ricoh m15\\R0010128.MOV"
        /*"F:\\panovideo\\test\\SP7\\1-7.MP4"*/
        /*"F:\\panovideo\\test\\test1\\YDXJ0136.mp4"*/
        /*"F:\\video\\oneplusone.mp4"*/;
    avp::AudioVideoFrame2 avFrame;
    avp::AudioVideoReader2 avReader;
    bool ok;

    ok = avReader.open(fileName, false, avp::SampleTypeUnknown, true, avp::PixelTypeBGR24);
    if (!ok)
    {
        printf("cannot open file for read\n");
        return 0;
    }

    int saveCount = 0;
    while (avReader.read(avFrame))
    {
        cv::Mat image(avFrame.height, avFrame.width, CV_8UC3, avFrame.data[0], avFrame.steps[0]);
        cv::imshow("image", image);
        int key = cv::waitKey(0);
        if (key == 's')
        {
            char buf[256];
            sprintf(buf, "image%d.bmp", saveCount++);
            cv::imwrite(buf, image);
        }
    }    

    return 0;
}