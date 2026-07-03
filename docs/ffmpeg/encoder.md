# H264 FFmpeg Encoder 代码思路

## 1. encoder 输入和输出

encoder 位于 processor 后、muxer 前：

```text
MediaFrame(YUV420P)
  ↓
MediaPacket(H264)
```

相关代码：

```text
encoder/encoder_manager.c
encoder/encoder_manager.h
encoder/h264_ffmpeg_encoder.c
core/media_frame.h
core/media_packet.h
core/media_packet.c
```

## 2. Manager + Ops

插件：

```text
g_h264_ffmpeg_encoder_ops
name = "h264_ffmpeg"
```

AppPipeline 调用：

```text
EncoderManager_Init
EncoderManager_Encode
EncoderManager_Flush
EncoderManager_Deinit
```

manager 再转调 H264 FFmpeg encoder 的 ops。

## 3. 核心 FFmpeg 对象

### AVCodec

编码器定义。当前通过：

```text
avcodec_find_encoder(AV_CODEC_ID_H264)
```

查找 H264 encoder。

### AVCodecContext

编码器实例配置和状态。

当前设置：

```text
width
height
time_base = {1, fps}
framerate = {fps, 1}
bit_rate
gop_size = fps * 2
pix_fmt = AV_PIX_FMT_YUV420P
max_b_frames = 0
AV_CODEC_FLAG_GLOBAL_HEADER
```

如果底层是 libx264，还设置：

```text
preset = ultrafast
tune = zerolatency
x264-params = repeat-headers=1
```

### AVFrame

未压缩输入帧。

当前 `MediaFrame(YUV420P)` 会包装成 `AVFrame`：

```text
av_frame.format = AV_PIX_FMT_YUV420P
av_frame.width = src_frame->width
av_frame.height = src_frame->height
av_frame.pts = src_frame->pts
av_frame.data[0..2] = src_frame->data[0..2]
av_frame.linesize[0..2] = src_frame->linesize[0..2]
```

注意：这里不拷贝 frame 数据，只是包装输入指针同步送入 encoder。

### AVPacket

压缩输出包。

FFmpeg 输出 `AVPacket` 后，项目会转换为 `MediaPacket`。

## 4. send / receive 流程

当前 encode 流程：

```text
MediaPacket_Unref(out_packet)
构造 AVFrame
avcodec_send_frame(codec_ctx, &av_frame)
h264_ffmpeg_receive_packets()
h264_ffmpeg_pop_packet(out_packet)
```

`h264_ffmpeg_receive_packets()` 内部循环调用：

```text
avcodec_receive_packet()
```

收到 packet 后先放入 pending queue。

## 5. 为什么不是每送一帧就输出一包

编码器内部可能缓存 frame。

原因包括：

- 码率控制
- GOP
- B-frame
- 内部 lookahead

当前代码设置 `max_b_frames = 0`，尽量降低延迟，但仍然保留 pending packet 队列处理多个输出 packet。

`AVERROR(EAGAIN)` 不是致命错误，表示当前需要先 receive packet 或继续送 frame。

## 6. MediaPacket deep copy 怎么做

函数：

```text
MediaPacket_CopyFromAVPacket()
```

做的事情：

```text
MediaPacket_Alloc(dst, src->size)
memcpy(dst->data, src->data, src->size)
dst->pts = src->pts
dst->dts = src->dts
dst->time_base = codec_ctx->time_base
dst->codec = CODEC_H264
```

这样 `MediaPacket.data` 不依赖 FFmpeg `AVPacket.data` 生命周期。

释放：

```text
MediaPacket_Unref()
```

## 7. pts / dts / time_base

输入 frame：

```text
av_frame.pts = src_frame->pts
```

输出 packet：

```text
MediaPacket.pts = AVPacket.pts
MediaPacket.dts = AVPacket.dts
MediaPacket.time_base = codec_ctx->time_base
```

当前 MP4 muxer 为了稳定文件 fps，最终仍使用 muxer 内部 `frame_index` 生成 MP4 时间戳。

RTSP muxer 也使用内部 `frame_index` 生成连续 packet 时间戳。

## 8. keyframe / SPS / PPS / extradata

### keyframe

当前根据 FFmpeg packet flags：

```text
AV_PKT_FLAG_KEY
```

设置：

```text
MediaPacket.keyframe
```

### SPS / PPS

H264 解码需要 SPS/PPS。

缺少 SPS/PPS 时，ffplay 可能报：

```text
non-existing PPS 0 referenced
decode_slice_header error
no frame!
```

### extradata

encoder 打开后从 `AVCodecContext.extradata` 复制一份稳定 SPS/PPS 到 encoder 私有上下文。

每个输出 `MediaPacket` 设置：

```text
packet.extradata = encoder_private_extradata
packet.extradata_size = encoder_private_extradata_size
```

注意：

- `MediaPacket.data` 是 deep copy
- `MediaPacket.extradata` 是 encoder 内部稳定只读引用
- muxer 使用时必须 copy 到自己的 `codecpar->extradata`

## 9. flush 是什么

录制结束或输入结束后，encoder 内部可能还有未输出 packet。

`EncoderManager_Flush()` 调用：

```text
avcodec_send_frame(codec_ctx, NULL)
avcodec_receive_packet()
```

直到没有更多 packet。

如果不 flush，MP4/RTSP 可能丢最后几帧。

## 10. zerolatency / repeat-headers 对 RTSP 的作用

RTSP 是实时流，要求低延迟和客户端能随时解码。

当前设置：

```text
preset = ultrafast
tune = zerolatency
repeat-headers = 1
max_b_frames = 0
```

作用：

- 减少编码延迟
- 避免 B 帧增加重排序复杂度
- 关键帧附近重复 SPS/PPS，帮助 RTSP client 解码
- 配合 RTSP muxer 在写 header 前复制 extradata 到 SDP

## 11. 当前未完善点

- 没有硬件编码器
- 没有音频编码
- 没有动态码率调整
- 没有完整 encoder 性能统计
