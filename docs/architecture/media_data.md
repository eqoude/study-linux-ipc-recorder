# MediaFrame / MediaPacket 数据结构说明

## 1. 为什么不直接全项目使用 FFmpeg 类型

项目没有让所有模块都依赖 `AVFrame / AVPacket`。

原因：

- capture 只需要 V4L2，不应该依赖 FFmpeg
- converter 只做像素转换，不应该关心 muxer
- viewer 只显示 YUV420P，不应该依赖 H264 packet
- encoder / muxer 可以替换

因此定义了：

```text
MediaFrame   raw frame
MediaPacket  encoded packet
```

## 2. MediaFrame

定义：

```text
core/media_frame.h
```

用途：

```text
capture -> converter -> processor/viewer -> encoder
```

字段：

```text
width / height   图像尺寸
pixfmt           PIX_FMT_YUYV422 或 PIX_FMT_YUV420P
data[3]          plane 指针
linesize[3]      每行字节数
size             有效数据大小
pts              当前 frame 序号
```

YUYV422：

```text
data[0] = packed YUYV buffer
linesize[0] = width * 2
```

YUV420P：

```text
data[0] = Y
data[1] = U
data[2] = V
linesize[0] = width
linesize[1] = width / 2
linesize[2] = width / 2
size = width * height * 3 / 2
```

## 3. MediaPacket

定义：

```text
core/media_packet.h
core/media_packet.c
```

用途：

```text
encoder -> muxer
```

字段：

```text
data             H264 数据
size             数据大小
owns_data        是否释放 data
keyframe         是否关键帧
extradata        SPS/PPS 只读引用
extradata_size   SPS/PPS 大小
pts / dts        编码器输出时间戳
time_base        packet 时间基
codec            CODEC_H264
```

`MediaPacket.data` 是 deep copy，`MediaPacket_Unref()` 负责释放。

`MediaPacket.extradata` 当前指向 encoder 私有上下文保存的稳定 SPS/PPS 拷贝，muxer 使用时必须复制到自己的 FFmpeg codecpar。

## 4. timestamp / pts / time_base

当前视频时间基约定：

```text
1 / fps
```

capture 生成：

```text
MediaFrame.pts = frame_index++
```

encoder 继承：

```text
AVFrame.pts = MediaFrame.pts
MediaPacket.pts = AVPacket.pts
```

MP4 / RTSP muxer 当前为了输出稳定，使用 muxer 内部 `frame_index` 生成连续 pts/dts。

## 5. frame_index vs pts

`frame_index` 是简单连续帧序号。

`pts` 是媒体时间戳。

当前 MP4/RTSP 输出：

```text
pts = muxer frame_index rescale 到 stream time_base
duration = 1 frame
```

这样可以避免历史上出现的：

- start 不为 0
- fps 不稳定
- duration 异常

## 6. ownership 总结

MediaFrame：

- V4L2 frame 指向 mmap buffer，不拥有
- `release_frame` 归还 V4L2 buffer
- `FrameQueue_Push()` deep copy frame 数据
- `FrameQueue_UnrefFrame()` 释放队列副本

MediaPacket：

- encoder 输出时 deep copy `AVPacket.data`
- `PacketQueue_Push()` deep copy packet data
- muxer 写完后调用 `MediaPacket_Unref()`
- muxer 不能保存 `MediaPacket.data` 裸指针

## 7. 数据流总图

```text
V4L2 mmap buffer
  ↓ wrap
MediaFrame(YUYV422)
  ↓ convert
MediaFrame(YUV420P)
  ↓ optional process / preview
MediaFrame(YUV420P)
  ↓ encode
MediaPacket(H264)
  ↓ mux
MP4 / RTSP
```
