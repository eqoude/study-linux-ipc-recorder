# IPC Recorder 测试报告

## 1. 测试环境

当前文档记录的是项目代码层面的测试方法和历史问题。实机环境需要按当前机器填写：

```text
OS:
GCC:
FFmpeg version:
SDL2 version:
Camera device: /dev/video0
RTSP server: mediamtx
```

## 2. 编译测试

命令：

```bash
cd ipc_recorder
make clean
make
```

帮助输出：

```bash
make run
```

当前 Makefile：

- 编译 `app/*.c`
- 编译 `core/*.c`
- 编译 capture / converter / encoder / frame_processor / muxer / modules / viewer
- 链接 FFmpeg、SDL2、pthread

## 3. MP4 早期问题

历史现象：

```text
Duration: 00:00:02.63
start: 0.133008
16.58 fps
30 tbr
15360 tbn
```

问题原因：

早期 muxer 直接使用上游 packet/capture 的 pts，导致 MP4 时间戳不从 0 开始，间隔不稳定。

## 4. MP4 当前修复策略

当前 `muxer/mp4_muxer.c` 使用：

```text
ctx->frame_index
src_time_base = {1, fps}
dst_time_base = stream->time_base
pts = av_rescale_q(frame_index, src_time_base, dst_time_base)
dts = pts
duration = av_rescale_q(1, src_time_base, dst_time_base)
```

写入成功后：

```text
frame_index++
```

预期结果：

```text
start ≈ 0.000000
fps ≈ 30
Duration = frames / fps
```

例子：

```text
42 frames / 30 fps = 1.40s
100 frames / 30 fps = 3.33s
300 frames / 30 fps = 10.00s
```

## 5. ffprobe 测试方法

录制：

```bash
./bin/ipc_recorder --device /dev/video0 --record output/test.mp4 --frames 300
```

检查：

```bash
ffprobe output/test.mp4
```

重点看：

- `Duration`
- `start`
- `fps`
- `tbr`
- `tbn`

## 6. ffmpeg decode 测试方法

命令：

```bash
ffmpeg -i output/test.mp4 -f null -
```

预期：

- 能正常读取 header
- 能正常解码 H264
- 能跑到文件结束
- 无明显 decode error

## 7. RTSP 测试问题

### 7.1 Connection refused

现象：

```text
[tcp] Connection to tcp://127.0.0.1:8554 failed: Connection refused
[rtsp] avformat_write_header failed: -111
RTSP server not available. Please start mediamtx or another RTSP server first.
```

原因：

没有启动 mediamtx。

当前设计：

```text
ipc_recorder = RTSP Publisher
mediamtx = RTSP Server
ffplay = RTSP Client
```

ipc_recorder 不实现 RTSP Server。

### 7.2 non-existing PPS 0 referenced

现象：

```text
non-existing PPS 0 referenced
decode_slice_header error
no frame!
```

原因：

RTSP client 没有正确拿到 H264 SPS/PPS。

当前修复：

- encoder 打开后复制 `AVCodecContext.extradata`
- `MediaPacket` 携带 extradata 引用
- RTSP muxer 在真正写 header 前 copy extradata 到 `stream->codecpar`
- libx264 设置 `repeat-headers=1`

### 7.3 RTP packets are too big

现象：

mediamtx 可能提示 RTP packet 太大并 remux。

说明：

当前项目依赖 FFmpeg RTSP muxer 和 mediamtx 做 RTP packetization，没有自己实现 RTP 分包。

## 8. RTSP 测试方法

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

如果 UDP 播放不稳定：

```bash
ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/live
```

## 9. Preview 测试方法

命令：

```bash
./bin/ipc_recorder --device /dev/video0 --preview
```

预期：

- 弹出 SDL 窗口
- 显示摄像头实时画面
- 关闭窗口后程序正常退出

如果没有窗口：

- 检查是否有图形环境
- 检查 SDL2 是否安装
- 检查输入是否为 YUV420P

## 10. thread_queue 验证点

当前使用：

```text
raw_queue
encode_queue
packet_queue
```

验证重点：

- queue 满时是否阻塞上游
- queue 空时是否阻塞下游
- 退出时 close queue 是否唤醒线程
- `FrameQueue` deep copy 是否避免 V4L2 buffer 生命周期问题
- `PacketQueue` deep copy 是否避免 H264 packet 悬空

当前仍需进一步验证：

- 长时间运行内存是否稳定
- 高分辨率下 deep copy 性能
- RTSP 网络阻塞对整体 fps 的影响

## 11. 当前状态总结

### MP4

当前状态：

```text
基本稳定
```

已处理：

- `MediaPacket.data` deep copy
- H264 extradata 写入 MP4 header
- MP4 muxer frame_index 时间戳
- start / fps / duration 异常

### RTSP

当前状态：

```text
已连通，SPS/PPS 已补充，但仍需实机持续验证
```

依赖：

- mediamtx
- ffplay / VLC

### Preview

当前状态：

```text
已接入 SDL viewer，需要本地图形环境单独验证
```

### thread_queue

当前状态：

```text
基础设施已接入 AppPipeline，需要进一步理解和压力测试
```

## 12. 下一步建议

- 实机录制 300 帧，保存 ffprobe 输出
- RTSP 连续推流 10 分钟，观察 ffplay 和 mediamtx 日志
- Preview + Record + RTSP 同时启用，观察是否掉帧
- 用 valgrind 检查退出路径内存释放
- 增加 V4L2 `QUERYCAP / G_FMT / S_PARM`
