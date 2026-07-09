# Pipeline 数据流总览

本文档说明当前 IPC Recorder / Edge AI Camera 原型系统中的完整数据流、线程队列、MediaFrame / MediaPacket 变化，以及 snapshot 和 AI 旁路为什么不进入主视频链路。

## 1. 完整数据流

```text
V4L2 Capture
  ↓
MediaFrame(YUYV422)
  ↓
Converter(YUYV422 to YUV420P)
  ↓
MediaFrame(YUV420P)
  ↓
FrameProcessor/OSD
  ↓
FrameSink(snapshot_jpeg) side output
  ↓
H264 Encoder
  ↓
MediaPacket(H264)
  ↓
Muxer(MP4/RTSP)
```

带旁路的结构：

```text
                         ┌──────────────────────────────┐
                         │ edge_ai_camera_test/snapshot.jpg
                         │              ↓
                         │ edge_ai_camera_test/ai_service.py
                         │              ↓
                         │ edge_ai_camera_test/event.json
                         └──────────────▲───────────────┘
                                        │
V4L2 -> Capture -> Converter -> FrameProcessor -> FrameSink(snapshot_jpeg)
                                │
                                ├── Viewer(SDL preview, optional)
                                │
                                ↓
                              Encoder
                                ↓
                              Muxer
                         ┌──────┴──────┐
                         │             │
                        MP4           RTSP
```

## 2. MediaFrame 在主链路中的变化

| 阶段 | 数据类型 | 像素格式 | 说明 |
|---|---|---|---|
| V4L2 capture | `MediaFrame` | `PIX_FMT_YUYV422` | 引用 V4L2 mmap buffer |
| converter 输出 | `MediaFrame` | `PIX_FMT_YUV420P` | 转换为 encoder/viewer 可用格式 |
| frame_processor 输出 | `MediaFrame` | `PIX_FMT_YUV420P` | 叠加 OSD 后仍保持同一格式 |
| viewer 输入 | `MediaFrame` | `PIX_FMT_YUV420P` | SDL 显示旁路 |
| sink 输入 | `MediaFrame` | `PIX_FMT_YUV420P` | snapshot JPEG 导出旁路 |
| encoder 输入 | `MediaFrame` | `PIX_FMT_YUV420P` | H264 编码输入 |

`MediaFrame` 是原始图像帧描述，不直接绑定 FFmpeg `AVFrame`。这样 capture 和 converter 不需要依赖 FFmpeg。

## 3. MediaPacket 的产生位置

`MediaPacket` 只在 encoder 后产生：

```text
MediaFrame(YUV420P)
  ↓
encoder/h264_ffmpeg_encoder.c
  ↓
MediaPacket(H264)
```

`MediaPacket` 保存：

- H264 encoded data
- packet size
- pts / dts
- time_base
- keyframe 标记
- extradata / SPS / PPS 信息

Muxer 只消费 `MediaPacket`，不再处理原始像素。

## 4. snapshot_jpeg 为什么是旁路

`snapshot_jpeg_sink` 的定位是 FrameSink：

```text
MediaFrame(YUV420P) -> snapshot.jpg
```

它不改变主链路中的 `MediaFrame`，也不影响 encoder / muxer。

设计原因：

- snapshot 是低频输出，不应该每帧阻塞编码链路。
- Python AI 只需要低频图像，不需要完整视频流。
- snapshot 失败只打印 warning，不中断主视频链路。
- 文件边界清晰，便于 Python 服务独立运行和调试。

当前配置：

```text
interval_frames = 30
output_path = edge_ai_camera_test/snapshot.jpg
tmp_path = edge_ai_camera_test/snapshot.jpg.tmp
```

写入策略：

```text
write snapshot.jpg.tmp
  ↓
rename to snapshot.jpg
```

这样可以避免 Python 读取到半截 JPEG。

## 5. AI 为什么不进入主视频链路

AI 推理没有放进 C 主链路，原因是：

- VLM 推理耗时明显高于单帧视频处理。
- 模型输出不稳定，不能影响录制和推流稳定性。
- Python 依赖和模型加载不适合耦合到 C 实时 pipeline。
- 文件通信可以让 C 程序和 Python 服务分别启动、停止和调试。

当前边界：

```text
C 主链路负责 snapshot.jpg
Python AI 旁路负责 event.json
```

C 主程序当前不读取 `event.json`，也不根据 AI 结果做报警动作。

## 6. 线程和队列作用

当前 `app/app_pipeline.c` 使用多线程和队列解耦主链路。

```text
capture_thread
  ↓ raw_queue: FrameQueue
process_thread
  ├── ConverterManager_Convert
  ├── FrameProcessorManager_Process(optional)
  ├── FrameSink(snapshot_jpeg)->write
  └── ViewerManager_Display(optional)
  ↓ encode_queue: FrameQueue
encode_thread
  └── EncoderManager_Encode / Flush
  ↓ packet_queue: PacketQueue
mux_thread
  ├── MuxerManager_WritePacket(mp4)
  └── MuxerManager_WritePacket(rtsp)
```

| 线程 | 主要职责 | 队列边界 |
|---|---|---|
| `capture_thread` | 从 V4L2 获取帧并归还 buffer | push `raw_queue` |
| `process_thread` | 转换、OSD、snapshot、preview | pop `raw_queue`, push `encode_queue` |
| `encode_thread` | H264 编码和 flush | pop `encode_queue`, push `packet_queue` |
| `mux_thread` | MP4 / RTSP 写 packet | pop `packet_queue` |

队列作用：

- 解耦不同处理阶段。
- 避免 muxer 或 encoder 短时阻塞直接卡住 capture。
- 通过 max size 防止无限内存积压。

## 7. 当前可运行模式

MP4 录制：

```bash
cd ipc_recorder
./bin/ipc_recorder --device /dev/video0 --record output/snapshot_test.mp4
```

Preview：

```bash
./bin/ipc_recorder --device /dev/video0 --preview
```

RTSP publisher：

```bash
./bin/ipc_recorder --device /dev/video0 --rtsp rtsp://127.0.0.1:8554/live
```

RTSP 需要外部 Server，例如 mediamtx。
