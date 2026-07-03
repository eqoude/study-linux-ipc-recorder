# RTSP 推流代码思路

## 1. RTSP 是什么

RTSP 是实时流控制协议，常用于网络摄像头预览。

在当前项目中：

```text
MediaPacket(H264)
  ↓
RTSP muxer
  ↓
external RTSP Server
  ↓
ffplay / VLC
```

## 2. Preview 和 RTSP 的区别

Preview：

- 本地显示
- 输入 `MediaFrame(YUV420P)`
- 使用 SDL window
- 不编码、不走网络

RTSP：

- 网络推流
- 输入 `MediaPacket(H264)`
- 使用 FFmpeg RTSP muxer
- 需要外部 RTSP Server

## 3. 当前 RTSP 是 publisher，不是 server

当前 `muxer/rtsp_muxer.c` 是 RTSP Publisher。

它不会监听 `8554` 端口，也不会处理客户端 DESCRIBE / SETUP / PLAY。

必须有外部 RTSP Server，例如 mediamtx：

```text
ipc_recorder -> mediamtx -> ffplay
```

## 4. 三个终端运行方式

终端 1：

```bash
./mediamtx
```

终端 2：

```bash
./bin/ipc_recorder --device /dev/video0 --rtsp rtsp://127.0.0.1:8554/live
```

终端 3：

```bash
ffplay rtsp://127.0.0.1:8554/live
```

如果 UDP 有问题：

```bash
ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/live
```

## 5. 当前代码流程

相关代码：

```text
muxer/rtsp_muxer.c
muxer/muxer_manager.c
encoder/h264_ffmpeg_encoder.c
core/media_packet.h
```

RTSP muxer open：

```text
avformat_alloc_output_context2(..., "rtsp", url)
avformat_new_stream
设置 codecpar
```

stream 参数：

```text
codec_type = AVMEDIA_TYPE_VIDEO
codec_id = AV_CODEC_ID_H264
width / height
format = AV_PIX_FMT_YUV420P
time_base = {1, fps}
avg_frame_rate = {fps, 1}
r_frame_rate = {fps, 1}
```

WriteHeader：

当前 `rtsp_muxer_write_header()` 只标记：

```text
header_requested = 1
```

原因：

真正的 SPS/PPS extradata 要等 encoder 输出第一帧 packet 后才能拿到。

第一帧 WritePacket：

```text
copy packet.extradata -> stream->codecpar->extradata
avformat_write_header
write AVPacket
```

## 6. 当前遇到过的问题

### Connection refused

现象：

```text
[tcp] Connection to tcp://127.0.0.1:8554 failed: Connection refused
[rtsp] avformat_write_header failed: -111
```

原因：

没有启动 mediamtx 或其他 RTSP Server。

解决：

先启动：

```bash
./mediamtx
```

### non-existing PPS 0 referenced

现象：

```text
non-existing PPS 0 referenced
decode_slice_header error
no frame!
```

原因：

RTSP client 收到 H264 slice，但没有正确收到 SPS/PPS。

当前修复：

- encoder 打开后复制 `AVCodecContext.extradata`
- 每个 `MediaPacket` 携带 `extradata / extradata_size`
- RTSP muxer 在 `avformat_write_header()` 前复制 extradata 到 `stream->codecpar`
- libx264 设置 `repeat-headers=1`

### RTP packets are too big

mediamtx 可能打印 RTP 包过大的提示，并进行 remux / packetize。

这通常说明 H264 NALU 需要被 RTSP/RTP 层拆包。当前项目依赖 FFmpeg RTSP muxer 和 mediamtx 处理，不在 ipc_recorder 内自己实现 RTP packetizer。

## 7. SPS / PPS / extradata / repeat-headers

### SPS

Sequence Parameter Set，描述视频序列参数，例如分辨率、profile 等。

### PPS

Picture Parameter Set，描述图像级参数。

### extradata

FFmpeg 中 H264 的 codec config 通常保存在：

```text
AVCodecContext.extradata
AVCodecParameters.extradata
```

当前 encoder 把 `AVCodecContext.extradata` 复制到 encoder 私有上下文。

RTSP muxer 把 `MediaPacket.extradata` 复制到：

```text
stream->codecpar->extradata
```

### repeat-headers

libx264 参数：

```text
repeat-headers=1
```

作用：

让关键帧附近重复 SPS/PPS，提升 RTSP client 加入播放时的解码成功率。

## 8. RTSP packet 时间戳

当前 RTSP muxer 和 MP4 muxer 一样，内部维护：

```text
frame_index
```

写 packet 时：

```text
src_time_base = {1, fps}
dst_time_base = stream->time_base
pts = av_rescale_q(frame_index, src_time_base, dst_time_base)
dts = pts
duration = av_rescale_q(1, src_time_base, dst_time_base)
```

写成功后：

```text
frame_index++
```

## 9. 当前边界

当前没有实现：

- 内置 RTSP Server
- RTSP 鉴权
- 多 client 管理
- 音频流
- 断线重连
- 自己实现 RTP packetizer

这些应该作为后续网络模块继续扩展。
