# AppPipeline 代码思路

本文档说明当前 `app/app_pipeline.c` 的职责边界、线程生命周期和关闭流程。当前版本已经从早期 `AppPipeline_Run()` 模式调整为 Linux daemon 风格的 `Init -> Start -> Signal_Wait -> Stop -> Wait -> Deinit` 生命周期。

## 1. 为什么从 `main.c` 拆出 AppPipeline

早期 demo 容易把初始化、主循环、线程创建、错误处理、资源释放都写在 `main.c`。当前项目已经包含：

- capture
- converter
- frame_processor
- sink
- viewer
- encoder
- muxer
- thread_queue

如果全部留在 `main.c`，入口会变成业务细节集合。当前设计中：

```text
main.c
  -> 解析 AppConfig
  -> 初始化 signal 等进程级资源
  -> 调用 AppPipeline 生命周期接口
```

真正的模块编排放在 `app/app_pipeline.c`。

## 2. `main.c` 当前负责什么

`app/main.c` 当前只负责应用生命周期控制：

```text
AppConfig_SetDefault
AppConfig_ParseArgs
AppConfig_Print
Signal_Init
AppPipeline_Init
AppPipeline_Start
Signal_Wait
AppPipeline_Stop
AppPipeline_Wait
AppPipeline_Deinit
```

它不直接调用：

- `CaptureManager_GetFrame`
- `ConverterManager_Convert`
- `EncoderManager_Encode`
- `MuxerManager_WritePacket`
- `ViewerManager_Display`

`main.c` 也不再创建额外的 pipeline wrapper thread。worker threads 由 `AppPipeline_Start()` 创建。

## 3. AppPipeline 负责什么

`AppPipeline` 定义在 `app/app_pipeline.h`，内部保存：

- `AppConfig config`
- 各模块 manager
- `FrameQueue raw_queue`
- `FrameQueue encode_queue`
- `PacketQueue packet_queue`
- worker thread 句柄
- 初始化 / 打开 / 启动状态标志
- `stop` 和 `error`

它是应用层 orchestration object，负责把组件串起来，但不实现模块内部逻辑。

## 4. 生命周期 API

当前 API：

```text
AppPipeline_Init
AppPipeline_Start
AppPipeline_Wait
AppPipeline_Stop
AppPipeline_Deinit
```

职责划分：

| API | 职责 |
|---|---|
| `AppPipeline_Init()` | 注册插件，初始化 manager，初始化队列，不启动线程 |
| `AppPipeline_Start()` | open/start capture，创建 worker threads，立即返回 |
| `AppPipeline_Stop()` | 设置 stop，停止 capture，关闭 raw queue，不 join 线程 |
| `AppPipeline_Wait()` | join capture/process/encode/mux worker threads |
| `AppPipeline_Deinit()` | 释放队列和 manager 资源 |

设计重点：

- `Start` 不等待线程结束。
- `Wait` 只负责等待线程结束。
- `Stop` 只负责发出停止请求。
- muxer 的 trailer/close 不在 `Deinit` 中写文件，而在 `mux_thread` 退出前完成。

## 5. `AppPipeline_Init()` 做什么

主要流程：

```text
memset pipeline
保存 AppConfig
RegisterAllModules
DeviceScanner_FindFirstCamera(optional)
CaptureManager_Init
ConverterManager_Init
FrameSink(snapshot_jpeg)->init
FrameProcessorManager_Init(optional)
ViewerManager_Init(optional, ENABLE_VIEWER only)
EncoderManager_Init(if record or rtsp)
MuxerManager_Init(mp4/segment optional)
MuxerManager_Init(rtsp optional)
FrameQueue_Init(raw_queue)
FrameQueue_Init(encode_queue if encoder needed)
PacketQueue_Init(packet_queue if encoder needed)
```

关键点：

- `device_path` 为空时自动扫描 V4L2 camera。
- `enable_segment` 时选择 `segment` muxer。
- `enable_preview` 只有在编译启用 `ENABLE_VIEWER` 时才初始化 viewer。
- `enable_record || enable_rtsp` 才初始化 encoder。
- `Init` 不启动 worker threads。

## 6. `AppPipeline_Start()` 做什么

主要流程：

```text
CaptureManager_Open
CaptureManager_Start
create mux_thread(if encoder needed)
create encode_thread(if encoder needed)
create process_thread
create capture_thread
return immediately
```

`Start` 不执行 `pthread_join()`。这样 main thread 可以进入 `Signal_Wait()`，等待 SIGINT / SIGTERM。

## 7. `AppPipeline_Wait()` 做什么

主要流程：

```text
pthread_join(capture_thread)
pthread_join(process_thread)
pthread_join(encode_thread)
pthread_join(mux_thread)
return pipeline error state
```

`Wait` 是唯一等待 worker threads 的 API。

## 8. `AppPipeline_Stop()` 做什么

停止请求流程：

```text
pipeline->stop = 1
CaptureManager_Stop
FrameQueue_Close(raw_queue)
```

它不直接关闭 encode queue / packet queue，也不 join 线程。

原因：

- `process_thread` 在退出时关闭 `encode_queue`。
- `encode_thread` 收到 encode queue EOF 后执行 encoder flush，再关闭 `packet_queue`。
- `mux_thread` 收到 packet queue EOF 后写 trailer、close muxer，然后退出。

## 9. shutdown 顺序

Ctrl+C 或 SIGTERM 后的目标顺序：

```text
SIGINT / SIGTERM
  ↓
main thread: sigwait returns
  ↓
AppPipeline_Stop
  ↓
CaptureManager_Stop
  ↓
raw_queue close
  ↓
process_thread exit
  ↓
encode_queue close
  ↓
encode_thread drains queue and flushes encoder
  ↓
packet_queue close
  ↓
mux_thread drains packet queue
  ↓
MuxerManager_WriteTrailer
  ↓
MuxerManager_Close
  ↓
AppPipeline_Wait returns
  ↓
AppPipeline_Deinit
```

这个设计的核心目标是：MP4 trailer 必须由 mux thread 写入，避免 Ctrl+C 后出现 `moov atom not found`。

## 10. muxer 生命周期

muxer 由 `mux_thread_main()` 独占写操作：

```text
MuxerManager_Open
MuxerManager_WriteHeader
while PacketQueue_Pop != EOF:
    MuxerManager_WritePacket
MuxerManager_WriteTrailer
MuxerManager_Close
```

这样避免 `AppPipeline_Deinit()` 在主线程中写 `AVFormatContext`。

## 11. AppPipeline 如何管理各模块

### capture

```text
CaptureManager_Init
CaptureManager_Open
CaptureManager_Start
CaptureManager_GetFrame
CaptureManager_ReleaseFrame
CaptureManager_Stop
CaptureManager_Close
```

capture 线程把 frame push 到 `raw_queue`。

### converter

```text
ConverterManager_Convert(raw_frame, yuv420_frame)
```

### processor

只有 `enable_processor` 时启用：

```text
FrameProcessorManager_Process(yuv420_frame, processed_frame)
```

### sink

`snapshot_jpeg` 是旁路 sink：

```text
FrameSink(snapshot_jpeg)->write(output_frame)
```

sink 写失败只记录 warning，不中断主视频链路。

### viewer

只有 `enable_preview` 且编译启用 `ENABLE_VIEWER` 时启用：

```text
ViewerManager_Display(output_frame)
```

### encoder

只有 `enable_record || enable_rtsp` 时启用：

```text
EncoderManager_Encode(frame, packet)
EncoderManager_Flush(packet)
```

### muxer

MP4 / segment / RTSP 都由 mux thread 写入：

```text
MuxerManager_WritePacket(muxer, packet)
```

同一个 encoded packet 会依次送给启用的输出插件。

## 12. AppPipeline 和 AppConfig 的关系

`AppConfig` 决定 pipeline 形态：

```text
enable_preview   -> viewer
enable_record    -> encoder + mp4/segment muxer
enable_segment   -> segment muxer
segment_time     -> segment duration
enable_rtsp      -> encoder + rtsp muxer
enable_processor -> frame_processor
device_path      -> capture device; empty means auto scan
width/height/fps -> capture/converter/encoder/muxer/viewer config
```

AppPipeline 不解析命令行，只消费已经解析好的 `AppConfig`。

## 13. AppPipeline 和 Manager + Ops 的关系

AppPipeline 只调用 manager：

```text
CaptureManager_*
ConverterManager_*
FrameProcessorManager_*
ViewerManager_*
EncoderManager_*
MuxerManager_*
```

它不直接调用具体插件实现文件：

```text
v4l2_capture.c
yuyv_to_yuv420_converter.c
h264_ffmpeg_encoder.c
mp4_muxer.c
segment_muxer.c
rtsp_muxer.c
sdl_display_sdl.c
osd_processor.c
```

具体实现由插件注册和 `ops` 函数表决定。
