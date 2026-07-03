# IPC Recorder 架构总览

## 1. 当前系统定位

IPC Recorder 是一个 Linux IPC Camera / Recorder 工程原型。它使用 C 语言实现，以 `Manager + Ops`、静态插件注册、`MediaFrame / MediaPacket` 和 `thread_queue` 为核心，把视频采集、格式转换、图像处理、预览、编码、MP4 封装和 RTSP 推流拆成独立组件。

当前定位：

```text
engineering prototype
```

不是量产级 IPC 系统。

## 2. 数据流架构

```text
V4L2 Camera
  ↓
Capture
  ↓ MediaFrame(YUYV422)
Converter
  ↓ MediaFrame(YUV420P)
FrameProcessor(optional)
  ↓ MediaFrame(YUV420P)
  ├── Viewer(SDL Preview)
  ↓
Encoder
  ↓ MediaPacket(H264)
Muxer
  ├── MP4
  └── RTSP publisher
```

Preview 是 YUV420P 显示旁路，不消费 H264 packet。

RTSP 是 publisher 模式，需要 mediamtx 等外部 RTSP Server。

## 3. 当前多线程架构

```text
capture_thread
  ↓ CaptureManager_GetFrame
  ↓ FrameQueue(raw_queue)

process_thread
  ↓ ConverterManager_Convert
  ↓ FrameProcessorManager_Process(optional)
  ├── ViewerManager_Display(optional)
  ↓ FrameQueue(encode_queue)

encode_thread
  ↓ EncoderManager_Encode
  ↓ PacketQueue(packet_queue)

mux_thread
  ↓ MuxerManager_WritePacket(mp4)
  ↓ MuxerManager_WritePacket(rtsp)
```

队列满时阻塞上游，队列空时阻塞下游，queue close 用于退出唤醒。

## 4. 三层结构

### Data Flow Layer

核心数据结构：

- `MediaFrame`：原始图像帧
- `MediaPacket`：编码后 H264 packet

### Component Layer

每个模块都有 Manager：

- `CaptureManager`
- `ConverterManager`
- `FrameProcessorManager`
- `ViewerManager`
- `EncoderManager`
- `MuxerManager`

每个 Manager 保存 `config / priv / ops`。

### Plugin Layer

`modules/module_register.c` 统一注册插件。

当前真实插件：

```text
capture:         v4l2
converter:       yuyv_to_yuv420
frame_processor: osd
viewer:          sdl
encoder:         h264_ffmpeg
muxer:           mp4
muxer:           rtsp
```

测试插件：

```text
fake_capture
fake_converter
fake_encoder
fake_muxer
```

当前是 static registry，不是 `.so` 动态插件。

## 5. AppConfig

`app/app_config.c` 解析命令行。

当前关键参数：

```text
--device /dev/videoX
--width 640
--height 480
--fps 30
--preview
--record output/test.mp4
--rtsp rtsp://127.0.0.1:8554/live
--processor osd
--no-processor
--frames 300
```

AppConfig 决定是否初始化 viewer、processor、encoder、MP4 muxer、RTSP muxer。

## 6. AppPipeline

`app/app_pipeline.c` 是当前应用层编排中心。

它负责：

- `RegisterAllModules()`
- 初始化各 Manager
- 初始化队列
- 打开 capture / muxer
- 创建线程
- join 线程
- stop / trailer / close / deinit

`main.c` 只保留配置和生命周期调用。

## 7. 关键文档

建议阅读：

```text
docs/project/code_reading_guide.md
docs/architecture/app_pipeline.md
docs/architecture/thread_queue.md
docs/architecture/data_contract.md
docs/architecture/plugin_register.md
```

模块文档：

```text
docs/capture/v4l2_workflow.md
docs/converter/yuyv422_to_yuv420p.md
docs/ffmpeg/encoder.md
docs/ffmpeg/muxer.md
docs/ffmpeg/rtsp.md
docs/viewer/sdl_display.md
docs/processor/frame_processor.md
```
