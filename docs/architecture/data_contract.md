# MediaFrame / MediaPacket 数据契约

## 1. 为什么需要数据契约

IPC Recorder 中每个模块都通过统一数据结构交换数据：

- 原始图像：`MediaFrame`
- 编码码流：`MediaPacket`

如果不规定 ownership，就会出现：

- V4L2 mmap buffer 被归还后下游继续读
- FFmpeg `AVPacket.data` 被释放后 muxer 继续写
- 跨线程队列保存了悬空指针
- muxer 保存 packet 指针异步使用

所以必须明确：谁创建、谁释放、谁能持有、是否拷贝。

## 2. MediaFrame 是什么

定义：

```text
core/media_frame.h
```

字段：

```c
int width;
int height;
PixelFormat pixfmt;
uint8_t *data[3];
int linesize[3];
int size;
int64_t pts;
```

含义：

- `width / height`：图像尺寸
- `pixfmt`：当前像素格式，例如 `PIX_FMT_YUYV422`、`PIX_FMT_YUV420P`
- `data[0..2]`：图像数据指针
- `linesize[0..2]`：每个 plane 每行字节数
- `size`：有效数据大小
- `pts`：当前 frame 时间戳，当前以 frame index 表示

## 3. MediaPacket 是什么

定义：

```text
core/media_packet.h
core/media_packet.c
```

字段：

```c
uint8_t *data;
int size;
int owns_data;
int keyframe;
uint8_t *extradata;
int extradata_size;
int64_t pts;
int64_t dts;
AVRational time_base;
CodecType codec;
```

含义：

- `data`：H264 packet 数据
- `size`：packet 大小
- `owns_data`：是否由 `MediaPacket_Unref()` 释放 `data`
- `keyframe`：是否关键帧
- `extradata / extradata_size`：H264 SPS/PPS 等 codec config
- `pts / dts`：编码 packet 时间戳
- `time_base`：packet 时间基
- `codec`：当前编码类型，例如 `CODEC_H264`

## 4. MediaFrame 生命周期

### 4.1 capture 创建 / 获取

V4L2 插件中：

```text
capture/v4l2_capture.c
```

`v4l2_capture_get_frame()` 调用 `VIDIOC_DQBUF` 从驱动取出一个 buffer，然后填充：

```text
frame->width
frame->height
frame->pixfmt = PIX_FMT_YUYV422
frame->data[0] = mmap buffer address
frame->linesize[0] = width * 2
frame->size = bytesused
frame->pts = frame_index++
```

此时 `MediaFrame.data[0]` 指向 V4L2 mmap buffer。

### 4.2 capture frame 是否能长期持有

不能长期持有。

原因：

V4L2 buffer 需要尽快通过 `VIDIOC_QBUF` 归还给驱动继续采集。

当前 `capture_thread_main()` 的做法：

```text
CaptureManager_GetFrame
FrameQueue_Push(raw_queue, &frame)
CaptureManager_ReleaseFrame
```

`FrameQueue_Push()` 会 deep copy frame 数据，所以 release 后下游仍然安全。

### 4.3 converter 读取

`process_thread_main()` 从 `raw_queue` pop 出 frame：

```text
FrameQueue_Pop(raw_queue, &raw_frame)
ConverterManager_Convert(&raw_frame, &yuv420_frame)
FrameQueue_UnrefFrame(&raw_frame)
```

converter 输入：

```text
MediaFrame(YUYV422)
```

converter 输出：

```text
MediaFrame(YUV420P)
```

### 4.4 processor 处理

如果启用 `--processor osd`：

```text
FrameProcessorManager_Process(&yuv420_frame, &processed_frame)
```

输入 / 输出都是：

```text
MediaFrame(YUV420P)
```

processor 不改变编码格式，也不生成 H264。

### 4.5 viewer 显示

如果启用 `--preview`：

```text
ViewerManager_Display(&viewer, output_frame)
```

SDL viewer 会把 YUV420P frame 拷贝到内部显示 buffer，再由 SDL 线程更新 texture。

viewer 不拥有 AppPipeline 中的 `MediaFrame`，不能保存外部 frame 指针长期使用。

### 4.6 release_frame 归还 V4L2 buffer

V4L2 `release_frame` 的核心是：

```text
VIDIOC_QBUF
```

它表示把 `DQBUF` 取出的 buffer 归还给驱动。

如果不归还：

- 驱动可用 buffer 越来越少
- 最终采集停止或阻塞

## 5. MediaPacket 生命周期

### 5.1 encoder 生成

`encoder/h264_ffmpeg_encoder.c` 中：

```text
MediaFrame(YUV420P)
  -> AVFrame
  -> avcodec_send_frame
  -> avcodec_receive_packet
  -> MediaPacket_CopyFromAVPacket
```

`MediaPacket_CopyFromAVPacket()` 会 deep copy `AVPacket.data` 到 `MediaPacket.data`。

### 5.2 packet 数据 ownership

当前规则：

```text
MediaPacket.data 由 MediaPacket 拥有
MediaPacket_Unref 负责释放 data
```

实现：

```text
MediaPacket_Alloc
MediaPacket_CopyFromAVPacket
MediaPacket_Unref
```

### 5.3 packet 跨线程

`encode_thread_main()` 输出 packet 后：

```text
PacketQueue_Push(packet_queue, &packet)
MediaPacket_Unref(&packet)
```

`PacketQueue_Push()` 会再次 deep copy `packet->data`，所以 mux thread 不依赖 encode thread 的局部变量。

### 5.4 muxer 读取

`mux_thread_main()`：

```text
PacketQueue_Pop(packet_queue, &packet)
MuxerManager_WritePacket(mp4, &packet)
MuxerManager_WritePacket(rtsp, &packet)
MediaPacket_Unref(&packet)
```

muxer 读取 packet 后必须同步写出或同步拷贝，不能保存 `packet->data` 裸指针以后再用。

## 6. 谁拥有 data 指针

### MediaFrame

不同来源不同：

```text
V4L2 get_frame:
  data[0] 指向 mmap buffer
  ownership 属于 V4L2 driver / capture plugin
  通过 release_frame 归还

FrameQueue_Push 后:
  data[0] 指向 heap clone
  ownership 属于 FrameQueue / pop 后消费者
  通过 FrameQueue_UnrefFrame 释放

converter / processor 输出:
  data 指向插件内部 buffer
  ownership 属于对应插件
  下游不能长期保存，跨线程必须 queue deep copy
```

### MediaPacket

```text
encoder 输出:
  data 是 AVPacket.data 的 deep copy
  ownership 属于 MediaPacket
  MediaPacket_Unref 释放

PacketQueue 输出:
  data 是 packet data 的 deep copy
  ownership 属于 pop 后消费者
  MediaPacket_Unref 释放
```

## 7. extradata ownership

当前 `MediaPacket.extradata` 表示 H264 SPS/PPS。

当前实现：

- encoder 打开后从 `AVCodecContext.extradata` 复制一份到 encoder 私有上下文
- 每个 `MediaPacket.extradata` 指向 encoder 私有上下文中的稳定拷贝
- muxer 使用时必须立刻复制到自己的 `AVStream.codecpar->extradata`
- `MediaPacket_Unref()` 不释放 `extradata`

注意：

这意味着 `extradata` 的生命周期依赖 encoder，不能在 encoder deinit 后再使用 packet。

## 8. 哪些模块不能保存指针

不允许：

- converter 保存 capture 的 `MediaFrame.data`
- processor 保存输入 frame 指针
- viewer 保存外部 frame 指针长期异步使用
- encoder 保存输入 `MediaFrame.data`
- muxer 保存 `MediaPacket.data` 后异步写

允许：

- viewer 同步 copy frame 到内部 buffer
- muxer 同步 copy packet 到 FFmpeg `AVPacket`
- FrameQueue / PacketQueue deep copy 数据跨线程

## 9. 跨线程注意事项

跨线程必须经过 queue：

```text
capture_thread -> raw_queue -> process_thread
process_thread -> encode_queue -> encode_thread
encode_thread -> packet_queue -> mux_thread
```

不要把 V4L2 mmap buffer 指针直接跨线程长期传递。

不要把 FFmpeg `AVPacket.data` 指针直接跨线程传递。

## 10. V4L2 mmap buffer 和 MediaFrame 的关系

V4L2 mmap buffer 是驱动申请并映射到用户态的采集 buffer。

`MediaFrame` 只是包装它：

```text
frame->data[0] = ctx->buffers[index].start
frame->size = buffer.bytesused
frame->pts = ctx->frame_index++
```

真正拥有 buffer 的是 capture plugin 和 V4L2 driver，不是 `MediaFrame`。

## 11. H264 AVPacket 和 MediaPacket 的关系

FFmpeg encoder 输出：

```text
AVPacket
```

项目对外输出：

```text
MediaPacket
```

转换函数：

```text
MediaPacket_CopyFromAVPacket()
```

它会：

- 分配 `MediaPacket.data`
- copy `AVPacket.data`
- 保存 `pts / dts`
- 保存 `time_base`
- 保存 `codec`

这样 muxer 不依赖 encoder 内部 `AVPacket` 生命周期。
