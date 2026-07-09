# 模块总览

本文档说明当前 IPC Recorder 工程中各模块的输入、输出、典型实现、插件 ops 名称和当前实现文件。

## 1. 模块表

| 模块 | 输入 | 输出 | 典型实现 | 插件 ops 名称 | 管理器 / 当前实现文件 |
|---|---|---|---|---|---|
| Capture | 设备或 fake source | `MediaFrame` | V4L2 mmap capture | `g_fake_capture_ops`, `g_v4l2_capture_ops` | `capture/capture_manager.c`, `capture/fake_capture.c`, `capture/v4l2_capture.c` |
| Converter | `MediaFrame(YUYV422)` | `MediaFrame(YUV420P)` | YUYV422 到 YUV420P | `g_fake_converter_ops`, `g_yuyv_to_yuv420_ops` | `converter/converter_manager.c`, `converter/fake_converter.c`, `converter/yuyv_to_yuv420_converter.c` |
| FrameProcessor | `MediaFrame(YUV420P)` | `MediaFrame(YUV420P)` | OSD 时间水印 | `g_osd_processor_ops` | `frame_processor/frame_processor_manager.c`, `frame_processor/osd_processor.c` |
| FrameSink | `MediaFrame(YUV420P)` | snapshot 等旁路结果 | JPEG snapshot 导出 | `g_snapshot_jpeg_sink_ops` | `sink/frame_sink_manager.c`, `sink/snapshot_jpeg_sink.c` |
| Viewer | `MediaFrame(YUV420P)` | SDL 窗口画面 | SDL YUV texture 显示 | `g_sdl_display_ops` | `viewer/sdl_display_manager.c`, `viewer/sdl_display_sdl.c` |
| Encoder | `MediaFrame(YUV420P)` | `MediaPacket(H264)` | FFmpeg H264 encoder | `g_fake_encoder_ops`, `g_h264_ffmpeg_encoder_ops` | `encoder/encoder_manager.c`, `encoder/fake_encoder.c`, `encoder/h264_ffmpeg_encoder.c` |
| Muxer | `MediaPacket(H264)` | MP4 文件或 RTSP 输出 | MP4 muxer / RTSP publisher | `g_fake_muxer_ops`, `g_mp4_muxer_ops`, `g_rtsp_muxer_ops` | `muxer/muxer_manager.c`, `muxer/fake_muxer.c`, `muxer/mp4_muxer.c`, `muxer/rtsp_muxer.c` |

## 2. Capture

职责：

- 从视频源获取原始帧。
- 当前真实实现是 V4L2 mmap。
- 输出 `MediaFrame(YUYV422)`。

关键文件：

```text
capture/capture_manager.c
capture/capture_manager.h
capture/v4l2_capture.c
capture/fake_capture.c
```

典型调用：

```text
CaptureManager_Init
CaptureManager_Open
CaptureManager_Start
CaptureManager_GetFrame
CaptureManager_ReleaseFrame
```

## 3. Converter

职责：

- 完成像素格式转换。
- 当前真实实现是 `YUYV422 -> YUV420P`。
- 保持 frame pts 不变。

关键文件：

```text
converter/converter_manager.c
converter/converter_manager.h
converter/yuyv_to_yuv420_converter.c
converter/fake_converter.c
```

输入输出：

```text
MediaFrame(YUYV422) -> MediaFrame(YUV420P)
```

## 4. FrameProcessor

职责：

- 在原始图像帧上做图像处理。
- 当前 `osd_processor.c` 在 YUV420P 左上角叠加系统时间水印。
- 不负责格式转换、编码或封装。

关键文件：

```text
frame_processor/frame_processor_manager.c
frame_processor/frame_processor_manager.h
frame_processor/osd_processor.c
```

输入输出：

```text
MediaFrame(YUV420P) -> MediaFrame(YUV420P)
```

## 5. FrameSink

职责：

- 旁路消费 `MediaFrame`。
- 不改变主链路 frame。
- 当前用于低频 JPEG snapshot 导出。

关键文件：

```text
sink/frame_sink_manager.c
sink/frame_sink_manager.h
sink/snapshot_jpeg_sink.c
sink/snapshot_jpeg_sink.h
```

当前实现：

```text
snapshot_jpeg
```

行为：

- 每 30 帧导出一次 `edge_ai_camera_test/snapshot.jpg`。
- 写入时先写 `snapshot.jpg.tmp`，再 rename。
- 写失败只作为 warning，不中断主视频链路。

## 6. Viewer

职责：

- 本地实时预览。
- 当前实现使用 SDL2。
- 只负责显示，不编码、不封装、不推流。

关键文件：

```text
viewer/sdl_display_manager.c
viewer/sdl_display_manager.h
viewer/sdl_display_sdl.c
```

输入：

```text
MediaFrame(YUV420P)
```

## 7. Encoder

职责：

- 把原始 YUV420P 帧编码成 H264 packet。
- 当前真实实现使用 FFmpeg/libavcodec 和 libx264。

关键文件：

```text
encoder/encoder_manager.c
encoder/encoder_manager.h
encoder/h264_ffmpeg_encoder.c
encoder/fake_encoder.c
```

输入输出：

```text
MediaFrame(YUV420P) -> MediaPacket(H264)
```

## 8. Muxer

职责：

- 消费 H264 `MediaPacket`。
- 输出 MP4 文件或推送 RTSP。

关键文件：

```text
muxer/muxer_manager.c
muxer/muxer_manager.h
muxer/mp4_muxer.c
muxer/rtsp_muxer.c
muxer/fake_muxer.c
```

当前实现：

- `mp4`：输出 MP4 文件。
- `rtsp`：作为 publisher 推到外部 RTSP Server。
- `fake_muxer`：测试插件框架。

## 9. 插件注册关系

所有插件在 `modules/module_register.c` 中注册：

```text
RegisterAllModules()
  ├── CaptureManager_Register(...)
  ├── ConverterManager_Register(...)
  ├── EncoderManager_Register(...)
  ├── FrameProcessorManager_Register(...)
  ├── MuxerManager_Register(...)
  ├── RegisterViewer(...)
  └── FrameSink_Register(...)
```

AppPipeline 通过插件名选择实现，例如：

```text
capture_name   = "v4l2"
converter_name = "yuyv_to_yuv420"
encoder_name   = "h264_ffmpeg"
muxer_name     = "mp4"
viewer_name    = "sdl"
```
