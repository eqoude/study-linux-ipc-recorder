# AppPipeline 代码思路

## 1. 为什么从 `main.c` 拆出 AppPipeline

早期 demo 通常会把初始化、循环、错误处理、资源释放都写在 `main.c`。当前项目模块已经很多：

- capture
- converter
- frame_processor
- viewer
- encoder
- muxer
- thread_queue

如果全部留在 `main.c`，入口会很难维护。现在 `main.c` 只负责配置和生命周期：

```text
AppConfig_SetDefault
AppConfig_ParseArgs
AppConfig_Print
AppPipeline_Init
AppPipeline_Run
AppPipeline_Deinit
```

真正的模块编排放到 `app/app_pipeline.c`。

## 2. `main.c` 现在负责什么

`app/main.c` 当前负责：

- 创建 `AppConfig`
- 创建 `AppPipeline`
- 解析命令行参数
- 检查至少启用一个输出：`--preview`、`--record`、`--rtsp`
- 调用 pipeline 初始化、运行、释放

它不直接调用：

- `CaptureManager_GetFrame`
- `ConverterManager_Convert`
- `EncoderManager_Encode`
- `MuxerManager_WritePacket`
- `ViewerManager_Display`

## 3. AppPipeline 负责什么

`AppPipeline` 定义在 `app/app_pipeline.h`，内部保存：

- `AppConfig config`
- 各模块 manager
- `FrameQueue raw_queue`
- `FrameQueue encode_queue`
- `PacketQueue packet_queue`
- 线程句柄
- 初始化 / 打开 / 启动状态标志
- `stop` 和 `error`

它是应用层 orchestration object，负责把组件串起来，但不实现模块内部逻辑。

## 4. `AppPipeline_Init()` 做什么

主要流程：

```text
memset pipeline
保存 AppConfig
RegisterAllModules
CaptureManager_Init
ConverterManager_Init
FrameProcessorManager_Init(optional)
ViewerManager_Init(optional)
EncoderManager_Init(if record or rtsp)
MuxerManager_Init(mp4 optional)
MuxerManager_Init(rtsp optional)
FrameQueue_Init(raw_queue)
FrameQueue_Init(encode_queue if encoder needed)
PacketQueue_Init(packet_queue if encoder needed)
CaptureManager_Open
MuxerManager_Open + WriteHeader(mp4 optional)
MuxerManager_Open + WriteHeader(rtsp optional)
```

关键点：

- `enable_preview` 才初始化 viewer。
- `enable_record` 才初始化 MP4 muxer。
- `enable_rtsp` 才初始化 RTSP muxer。
- `enable_record || enable_rtsp` 才初始化 encoder。

## 5. `AppPipeline_Run()` 做什么

主要流程：

```text
CaptureManager_Start
create mux_thread(if encoder needed)
create encode_thread(if encoder needed)
create process_thread
create capture_thread
join all started threads
```

当前线程：

- `capture_thread_main`
- `process_thread_main`
- `encode_thread_main`
- `mux_thread_main`

## 6. `AppPipeline_Deinit()` 做什么

退出流程：

```text
app_pipeline_request_stop
join remaining threads
CaptureManager_Stop
MuxerManager_WriteTrailer(mp4 / rtsp)
MuxerManager_Close(mp4 / rtsp)
CaptureManager_Close
PacketQueue_Deinit
FrameQueue_Deinit
MuxerManager_Deinit
EncoderManager_Deinit
ViewerManager_Deinit
FrameProcessorManager_Deinit
ConverterManager_Deinit
CaptureManager_Deinit
```

设计重点：

- 先 stop / close queue，唤醒阻塞线程。
- 再 join 线程。
- 再释放 manager 和插件资源。

## 7. AppPipeline 如何管理各模块

### capture

初始化：

```text
CaptureManager_Init(&pipeline->capture, config.capture_name, &capture_config)
```

运行：

```text
CaptureManager_Start
CaptureManager_GetFrame
CaptureManager_ReleaseFrame
```

capture 线程把 frame push 到 `raw_queue`。

### converter

初始化：

```text
ConverterManager_Init(&pipeline->converter, config.converter_name, &converter_config)
```

运行：

```text
ConverterManager_Convert(&raw_frame, &yuv420_frame)
```

### processor

只有 `enable_processor` 时启用：

```text
FrameProcessorManager_Process(&yuv420_frame, &processed_frame)
```

### viewer

只有 `enable_preview` 时启用：

```text
ViewerManager_Display(&pipeline->viewer, output_frame)
```

SDL 窗口关闭时，display 返回退出信号，pipeline 请求停止。

### encoder

只有 `enable_record || enable_rtsp` 时启用：

```text
EncoderManager_Encode(&frame, &packet)
EncoderManager_Flush(&packet)
```

### muxer

MP4：

```text
MuxerManager_WritePacket(&pipeline->muxer, &packet)
```

RTSP：

```text
MuxerManager_WritePacket(&pipeline->rtsp_muxer, &packet)
```

同一个 encoded packet 会依次送给启用的输出插件。

## 8. AppPipeline 和 AppConfig 的关系

`AppConfig` 决定 pipeline 形态：

```text
enable_preview  -> viewer
enable_record   -> encoder + mp4 muxer
enable_rtsp     -> encoder + rtsp muxer
enable_processor -> frame_processor
device_path     -> capture device
width/height/fps -> capture/converter/encoder/muxer/viewer config
```

AppPipeline 不解析命令行，只消费已经解析好的 `AppConfig`。

## 9. AppPipeline 和 Manager + Ops 的关系

AppPipeline 只调用 manager：

```text
CaptureManager_*
ConverterManager_*
FrameProcessorManager_*
ViewerManager_*
EncoderManager_*
MuxerManager_*
```

它不直接调用：

```text
v4l2_capture.c
yuyv_to_yuv420_converter.c
h264_ffmpeg_encoder.c
mp4_muxer.c
rtsp_muxer.c
sdl_display_sdl.c
osd_processor.c
```

具体实现由插件注册和 `ops` 函数表决定。
