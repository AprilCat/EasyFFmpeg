# EasyFFmpeg 一个简单的音视频读写库

## 动机
熟悉图像处理和计算机视觉的同学们应该都用过 OpenCV，当我们要处理视频读写的时候一般都会采用 OpenCV 自带的 `cv::VideoCapture` 和 `cv::VideoWriter`。这两个类屏蔽了编解码所有细节，非常好用。例如：
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
对于一般的视频处理而言，`cv::VideoCapture` 和 `cv::VideoWriter` 已经够用了。但是，在一些情况下，这两个类就无法满足需求了。比如：
 1. `cv::VideoCapture::read` 函数返回的帧指向的是一块内部内存，如果需要一块深拷贝的帧，需要调用 `cv::Mat::clone()` 或者 `cv::Mat::copyTo()` 函数进行操作
 2. `cv::VideoWriter` 写的视频无法指定码率
 3. `cv::VideoWriter` 在 Windows 操作系统下要输出 H.264 格式编码的视频，需要重编译 opencv_ffmpeg.dll
