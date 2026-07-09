# Edge AI Camera Snapshot 导出说明

本文说明当前 IPC Recorder 如何从 C 主视频链路中每 1 秒导出一张 JPEG 图片，供后续 Python VLM 服务读取。

当前实现已经迁移为 `FrameSink` 插件机制。更完整的架构说明见：

```text
docs/edge_ai_camera_snapshot_sink.md
```

## 1. 功能位置

当前视频链路：

```text
V4L2 Capture
  -> Converter(YUYV422 to YUV420P)
  -> Processor / OSD
  -> FrameSink(snapshot_jpeg)
  -> Encoder(H264)
  -> Muxer(MP4 / RTSP)
```

`snapshot_jpeg` sink 通过 `FrameSinkOps` 接入 `process_thread`，输入是 processor 后的 `MediaFrame(YUV420P)`。如果未启用 processor，则使用 converter 输出的 `MediaFrame(YUV420P)`。

## 2. 输出文件

固定输出路径：

```text
edge_ai_camera_test/snapshot.jpg
```

写入策略：

```text
edge_ai_camera_test/snapshot.jpg.tmp
  ↓ 写完整 JPEG
rename()
  ↓
edge_ai_camera_test/snapshot.jpg
```

这样 Python VLM 服务不会读到半截 JPEG。

## 3. 导出频率

当前阶段 `snapshot_jpeg` sink 直接使用 `interval_frames` 控制导出频率。由于主视频 fps 固定为 30，因此 `interval_frames` 设置为 30：

```text
interval_frames = 30
```

也就是每 30 帧导出一次 `snapshot.jpg`，约等于每 1 秒更新一次。

后续如果需要支持动态 fps，可再扩展 `interval_seconds`，或从 `AppConfig` 中推导 `interval_frames`。

## 4. 编码方式

相关代码：

```text
sink/frame_sink_manager.h
sink/frame_sink_manager.c
sink/snapshot_jpeg_sink.h
sink/snapshot_jpeg_sink.c
```

实现方式：

- 输入格式：`PIX_FMT_YUV420P`
- 使用 `libswscale` 将 YUV420P 转为 MJPEG encoder 接受的像素格式
- 使用 `libavcodec` 的 `AV_CODEC_ID_MJPEG` 编码 JPEG
- 写入临时文件后再 `rename()` 成正式文件

导出失败时只打印 warning，不中断主视频链路。

## 5. 编译

```bash
cd ipc_recorder
make clean
make
```

Makefile 已加入：

```text
sink/*.c
libswscale
```

## 6. 运行验证

运行主程序，例如录制 120 帧：

```bash
./bin/ipc_recorder --device /dev/video0 --record output/test.mp4 --processor osd --frames 120
```

也可以只预览：

```bash
./bin/ipc_recorder --device /dev/video0 --preview --processor osd
```

查看 snapshot 是否每秒更新：

```bash
watch -n 1 'ls -lh edge_ai_camera_test/snapshot.jpg && stat edge_ai_camera_test/snapshot.jpg'
```

确认文件不是 0 字节：

```bash
ls -lh edge_ai_camera_test/snapshot.jpg
```

打开图片：

```bash
xdg-open edge_ai_camera_test/snapshot.jpg
```

成功日志示例：

```text
[ai_snapshot] wrote edge_ai_camera_test/snapshot.jpg
```

失败日志示例：

```text
[pipeline] warning: FrameSink(snapshot_jpeg)->write: Codec error (-8)
```

## 7. 当前边界

当前只完成 snapshot 导出，不包含：

- Python VLM 服务
- `event.json`
- C 进程读取 AI 结果
- AI 结果叠加 OSD

后续 Python 服务可以稳定读取：

```text
edge_ai_camera_test/snapshot.jpg
```

并把分析结果写入 JSON 文件，供 C 主链路后续扩展使用。
