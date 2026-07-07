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

H264 解码必须依赖 SPS/PPS。

SPS 描述视频序列级参数，例如 profile、level、分辨率等。PPS 描述图像参数集，例如熵编码模式、slice 相关参数等。解码器只有拿到 SPS/PPS 后，才能正确解析后续 H264 slice。

缺少 SPS/PPS 时，ffplay 可能报：

```text
non-existing PPS 0 referenced
decode_slice_header error
no frame!
```

启动解码必须满足：

```text
SPS + PPS + IDR
```

不能只发送：

```text
SPS + PPS + P-frame
```

原因是 P 帧依赖之前的参考帧。新接入的 RTSP 客户端没有历史参考帧，无法直接从 P 帧开始解码。

### SPS/PPS 的三种常见处理方式

#### 方式一：全局头 extradata

编码器打开后，SPS/PPS 保存在 `AVCodecContext.extradata` 中。MP4 muxer 会把它写入 `AVStream.codecpar->extradata`，播放器从 MP4 文件头读取 SPS/PPS。

MP4 录像推荐使用全局头：

```text
AV_CODEC_FLAG_GLOBAL_HEADER
↓
AVCodecContext.extradata
↓
AVStream.codecpar->extradata
↓
MP4 moov / avcC
```

MP4 场景不依赖每个 IDR 前重复 SPS/PPS。全局头写入文件头后，播放器可以在解析 MP4 metadata 时拿到 codec 参数。

#### 方式二：关键帧附近重复 SPS/PPS

当前工程使用：

```text
x264-params = repeat-headers=1
```

作用是让编码器在 IDR 关键帧附近重复输出 SPS/PPS。RTSP 客户端中途加入时，等待下一个 `SPS + PPS + IDR` 后即可开始解码。

当前工程同时设置：

```text
preset = ultrafast
tune = zerolatency
repeat-headers = 1
max_b_frames = 0
```

作用：

- `preset = ultrafast`：降低编码计算量。
- `tune = zerolatency`：降低实时编码延迟。
- `max_b_frames = 0`：关闭 B 帧，减少重排序。
- `repeat-headers = 1`：关键帧附近重复 SPS/PPS，提高 VLC / ffplay 等客户端中途接入成功率。

重复 SPS/PPS 会带来少量码流冗余，但数据量很小，当前初版 RTSP 推流可以接受。

#### 方式三：RTSP SDP / 客户端连接时发送 SPS/PPS

后续可增强为：

```text
encoder extradata
↓
解析 SPS/PPS
↓
写入 RTSP SDP 的 sprop-parameter-sets
↓
客户端等待 IDR
↓
开始解码
```

也可以在客户端新连接时，先单独发送 SPS/PPS，再等待并发送下一个 IDR。

关键约束仍然是：

```text
SPS + PPS + IDR
```

只给新客户端发送 `SPS + PPS + P-frame` 不能启动解码。

### 当前工程结论

encoder 打开后从 `AVCodecContext.extradata` 复制一份稳定 SPS/PPS 到 encoder 私有上下文。

每个输出 `MediaPacket` 设置：

```text
packet.extradata = encoder_private_extradata
packet.extradata_size = encoder_private_extradata_size
```

注意：

- `AVCodecContext.extradata` 用于 MP4 muxer 写入 `codecpar->extradata`。
- `AVCodecContext.extradata` 也为后续 RTSP SDP 管理 SPS/PPS 预留。
- `repeat-headers=1` 用于初版 RTSP 推流，让 IDR 附近重复携带 SPS/PPS。
- `MediaPacket.data` 是 deep copy。
- `MediaPacket.extradata` 是 encoder 内部稳定只读引用。
- muxer 使用 extradata 时必须 copy 到自己的 `codecpar->extradata`。

当前实现优先目标：

```text
先保证 MP4 可播放
再保证 RTSP 初版容易解码
后续再优化为 SDP 管理 SPS/PPS
```

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

- `preset = ultrafast` 降低编码耗时，适合实时预览和推流。
- `tune = zerolatency` 减少编码器内部缓存。
- `max_b_frames = 0` 关闭 B 帧，避免未来参考帧带来的重排序延迟。
- `repeat-headers = 1` 在 IDR 关键帧附近重复 SPS/PPS，帮助 RTSP client 中途接入后解码。

当前 RTSP 初版方案依赖两条路径：

```text
extradata
  -> 为 RTSP SDP / header 处理提供 SPS/PPS 来源

repeat-headers=1
  -> 让关键帧附近在码流中重复携带 SPS/PPS
```

这不是最终最精细的 RTSP 管理方式，但符合当前工程阶段：先保证 VLC / ffplay 能稳定解码，再把 SPS/PPS 管理收敛到 SDP 和客户端连接流程。

## 11. 当前未完善点

- 没有硬件编码器
- 没有音频编码
- 没有动态码率调整
- 没有完整 encoder 性能统计
- RTSP SPS/PPS 还没有完整收敛为 SDP 驱动的连接管理
