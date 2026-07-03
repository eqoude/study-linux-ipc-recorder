# MP4 Muxer 代码思路

## 1. MP4 muxer 输入和输出

MP4 muxer 位于 encoder 后：

```text
MediaPacket(H264)
  ↓
output/test.mp4
```

相关代码：

```text
muxer/muxer_manager.c
muxer/muxer_manager.h
muxer/mp4_muxer.c
core/media_packet.h
```

## 2. Manager + Ops

插件：

```text
g_mp4_muxer_ops
name = "mp4"
```

AppPipeline 调用：

```text
MuxerManager_Init
MuxerManager_Open
MuxerManager_WriteHeader
MuxerManager_WritePacket
MuxerManager_WriteTrailer
MuxerManager_Close
MuxerManager_Deinit
```

## 3. FFmpeg 核心对象

### AVFormatContext

表示一个输出容器。

MP4 muxer 中：

```text
avformat_alloc_output_context2(&format_ctx, NULL, "mp4", output_path)
```

### AVStream

表示容器中的一路流。

当前只有一路 video stream：

```text
codec_type = AVMEDIA_TYPE_VIDEO
codec_id = AV_CODEC_ID_H264
width / height
format = AV_PIX_FMT_YUV420P
time_base = {1, fps}
avg_frame_rate = {fps, 1}
r_frame_rate = {fps, 1}
```

### AVPacket

表示写入容器的一包编码数据。

MP4 muxer 会把 `MediaPacket.data` copy 到 FFmpeg `AVPacket` buffer，再调用：

```text
av_interleaved_write_frame()
```

## 4. Header / extradata

MP4 需要 H264 SPS/PPS 等 codec config。

当前 `MuxerManager_WriteHeader()` 调用 `mp4_muxer_write_header()` 时，encoder 还没有输出第一包 packet，muxer 拿不到 extradata。

所以当前策略是：

```text
WriteHeader:
  标记 header_requested

WritePacket(first packet):
  copy packet.extradata -> stream->codecpar->extradata
  avformat_write_header
  write packet
```

如果第一包没有 `extradata`，MP4 muxer 会报：

```text
[mp4] missing H264 extradata
```

## 5. 为什么使用 frame_index 生成连续 pts

早期如果直接使用 capture / encoder 的原始 pts，出现过：

```text
start 不为 0
fps 只有 16.58
duration 异常
```

当前 MP4 muxer 使用内部：

```text
int64_t frame_index
```

生成连续时间戳。

规则：

```text
src_time_base = {1, fps}
dst_time_base = video_stream->time_base
pts = av_rescale_q(frame_index, src_time_base, dst_time_base)
dts = pts
duration = av_rescale_q(1, src_time_base, dst_time_base)
frame_index++ 只在写入成功后执行
```

## 6. time_base = 1/fps

创建 stream 时：

```text
stream->time_base = {1, fps}
stream->avg_frame_rate = {fps, 1}
stream->r_frame_rate = {fps, 1}
```

这样每帧 duration 理论上就是：

```text
1 / fps
```

FFmpeg muxer 最终可能把 MP4 `tbn` 调整成容器内部更合适的值，但 packet pts/duration 会通过 `av_rescale_q()` 正确转换。

## 7. 为什么 start 变成 0

因为 MP4 muxer 不再使用摄像头 timestamp 或 encoder 的不连续 pts，而是从：

```text
frame_index = 0
```

开始生成 packet pts。

第一帧：

```text
pts = 0
dts = 0
```

所以 ffprobe 中 `start` 应接近 0。

## 8. 为什么 ffprobe 看到 30 fps

假设：

```text
fps = 30
写入 300 帧
```

则：

```text
duration = 300 / 30 = 10s
```

`avg_frame_rate` 和 packet duration 都按 30fps 设置，ffprobe 应显示接近 30fps。

## 9. 写 packet 流程

`mp4_muxer_write_packet()`：

```text
检查 packet 是 H264
必要时写 header
copy packet data 到 av_malloc buffer
av_packet_from_data
设置 stream_index
设置 pts/dts/duration
设置 AV_PKT_FLAG_KEY
av_interleaved_write_frame
成功后 frame_index++
```

失败路径会 `av_packet_unref()`，避免 packet data 泄漏。

## 10. ffprobe 检查方法

```bash
ffprobe output/test.mp4
```

重点检查：

```text
Duration
start
fps
tbr
tbn
```

预期：

```text
start ≈ 0.000000
fps ≈ config.fps
Duration ≈ written_frames / fps
```

## 11. ffmpeg 解码检查方法

```bash
ffmpeg -i output/test.mp4 -f null -
```

如果能正常解码到末尾，说明：

- MP4 header 可读
- H264 extradata 有效
- packet 数据可解码
- trailer 基本正常

## 12. 当前未完善点

- 没有音频 stream
- 没有字幕 / metadata
- 没有异常中途断电后的文件修复
- 没有统计实际写入 fps
