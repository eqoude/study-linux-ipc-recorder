# IPC Recorder 系统总览

## 1. 系统整体目标

IPC Recorder 的目标是搭建一个 Linux IPC 视频处理 pipeline：

```text
摄像头采集
  → 像素格式转换
  → 可选图像处理
  → 可选本地预览
  → H264 编码
  → MP4 保存 / RTSP 推流
```

它的重点不是单个 API，而是把 capture、converter、processor、viewer、encoder、muxer 拆成可替换组件，并用 `AppPipeline` 串成一个可运行工程。

## 2. 当前支持的功能

### V4L2 采集

代码：

```text
capture/v4l2_capture.c
capture/capture_manager.c
```

输出：

```text
MediaFrame(YUYV422)
```

当前使用 mmap buffer、`VIDIOC_DQBUF` 获取帧、`VIDIOC_QBUF` 归还帧。

### YUYV422 转 YUV420P

代码：

```text
converter/yuyv_to_yuv420_converter.c
```

输入 / 输出：

```text
MediaFrame(YUYV422) -> MediaFrame(YUV420P)
```

当前使用纯 C 转换，不依赖 swscale。

### OSD / frame_processor

代码：

```text
frame_processor/osd_processor.c
```

输入 / 输出：

```text
MediaFrame(YUV420P) -> MediaFrame(YUV420P)
```

当前初步实现是绘制简单 OSD 区域，不是完整字体库。

### H264 编码

代码：

```text
encoder/h264_ffmpeg_encoder.c
```

输入 / 输出：

```text
MediaFrame(YUV420P) -> MediaPacket(H264)
```

当前使用 FFmpeg libavcodec，配置低延迟参数，并输出 keyframe / SPS / PPS extradata 信息。

### MP4 保存

代码：

```text
muxer/mp4_muxer.c
```

输入 / 输出：

```text
MediaPacket(H264) -> output/test.mp4
```

MP4 muxer 当前使用内部 `frame_index` 生成稳定 1/fps 时间戳。

### RTSP 推流

代码：

```text
muxer/rtsp_muxer.c
```

输入 / 输出：

```text
MediaPacket(H264) -> external RTSP Server
```

当前是 publisher 模式，需要 mediamtx 等外部 RTSP Server，不是内置 RTSP Server。

### SDL Preview

代码：

```text
viewer/sdl_display_sdl.c
```

输入：

```text
MediaFrame(YUV420P)
```

当前 SDL viewer 内部创建显示线程，使用 `SDL_UpdateYUVTexture()` 显示 YUV420P。

## 3. 整体数据流图

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
  └── RTSP
```

当前 `AppPipeline` 中的线程图：

```text
capture_thread
  ↓ raw_queue: FrameQueue
process_thread
  ├── ConverterManager_Convert
  ├── FrameProcessorManager_Process(optional)
  └── ViewerManager_Display(optional)
  ↓ encode_queue: FrameQueue
encode_thread
  ↓ packet_queue: PacketQueue
mux_thread
  ├── MuxerManager_WritePacket(mp4)
  └── MuxerManager_WritePacket(rtsp)
```

## 4. 架构类型

### Pipeline architecture

数据按固定顺序流动：

```text
capture -> converter -> processor/viewer -> encoder -> muxer
```

每个模块只处理自己负责的阶段。

### Component-based architecture

每个模块都有独立 manager：

- `CaptureManager`
- `ConverterManager`
- `FrameProcessorManager`
- `ViewerManager`
- `EncoderManager`
- `MuxerManager`

AppPipeline 只调用 manager 接口，不直接调用具体插件文件。

### Plugin registry

`modules/module_register.c` 统一注册插件：

- `g_v4l2_capture_ops`
- `g_yuyv_to_yuv420_ops`
- `g_osd_processor_ops`
- `g_sdl_display_ops`
- `g_h264_ffmpeg_encoder_ops`
- `g_mp4_muxer_ops`
- `g_rtsp_muxer_ops`

当前是静态插件注册，不是 `.so` 动态加载。

### Engineering prototype

当前工程已经具备完整链路，但仍不是工业级量产系统：

- V4L2 能力检测不完整
- RTSP 没有重连
- 没有音频
- 没有性能统计
- 没有完整错误恢复策略

## 5. 运行模式

### Record only

```bash
./bin/ipc_recorder --device /dev/video0 --record output/test.mp4
```

### Preview only

```bash
./bin/ipc_recorder --device /dev/video0 --preview
```

### RTSP only

```bash
./bin/ipc_recorder --device /dev/video0 --rtsp rtsp://127.0.0.1:8554/live
```

### Record + Preview + RTSP

```bash
./bin/ipc_recorder --device /dev/video0 \
  --preview \
  --record output/test.mp4 \
  --rtsp rtsp://127.0.0.1:8554/live
```
