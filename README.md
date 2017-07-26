# EasyFFmpeg 一个简单的音视频读写库

## 动机
熟悉图像处理和计算机视觉的同学们应该都用过 OpenCV，当我们要处理视频读写的时候一般都会采用 OpenCV 自带的 `cv::VideoCapture` 和 `cv::VideoWriter`。这两个类本质上是对音视频编解码库 FFmpeg 的封装，并且屏蔽了编解码操作的细节，使用很简单。例如：
```c++
cv::VideoCapture reader;
reader.open("in.mp4");
CV_Assert(reader.isOpened());
cv::Mat frame;
reader.read(frame);

cv::VideoWriter writer;
writer.open("out.avi", CV_FOURCC('M', 'J', 'P', 'G'), 25, frame.size(), frame.type() == CV_8UC3);
CV_Assert(writer.isOpened());
writer.write(frame);

while (reader.read(frame))
{
    cv::imshow("frame", frame);
    writer.write(frame);
    int key = cv::waitKey(20);
    if (key == 'q')
        break;
}
```
对于一般的视频处理而言，`cv::VideoCapture` 和 `cv::VideoWriter` 已经够用了。但是，如果我们要对视频文件做更精细或者深入的处理，这两个类就无法满足需求了。比如：
 1. `cv::VideoCapture::read` 函数返回的帧指向的是一块内部内存，如果需要一块深拷贝的帧，需要调用 `cv::Mat::clone()` 或者 `cv::Mat::copyTo()` 函数进行操作。
 2. `cv::VideoCapture` 读出的帧只能是 BGR 格式，我们无法获得内部 FFmpeg AVCodecContext 解码后直接得到的 YUV420P 格式的帧。
 3. `cv::VideoWriter` 写的视频无法指定编码器的详细参数，无法指定码率。
 4. `cv::VideoWriter` 只能写入灰度帧或者 BGR 格式的帧，无法写入 YUV420P 格式的帧。
 5. `cv::VideoWriter` 在 Windows 操作系统下要输出 H.264 格式编码的视频，需要重编译 opencv_ffmpeg.dll。
 6. `cv::VideoCapture` 和 `cv::VideoWriter` 这两个类都无法处理音频。

OpenCV 的视频读写不够强大，无法处理音频，所以最终还是得直接调用类似 FFmpeg 这样的库。最初的目标就是写两个类 `AudioVideoReader` 和 `AudioVideoWriter` 对 FFmpeg 进行封装，解决上述问题。

## 第一版 avp::AudioVideoReader avp::AudioVideoWriter avp::AudioVideoFrame
第一个版本的设计解决了上述的第 1, 3 , 5 和 6 这 4 个问题。
