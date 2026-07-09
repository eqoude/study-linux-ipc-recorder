# Edge AI Camera Snapshot FrameSink 说明

本文说明当前 IPC Recorder 如何通过 `FrameSink` 插件机制导出 `snapshot.jpg`，供后续 Python VLM 服务读取。

## 1. 为什么使用 FrameSink

snapshot 导出属于旁路消费，不应该写死在 `process_thread` 中。

`process_thread` 的核心职责是：

```text
raw frame
  -> converter
  -> optional processor
  -> viewer / encoder queue
```

如果把 JPEG 导出逻辑直接写进线程函数，`process_thread` 会依赖具体业务功能，后续再增加 AI 抽帧、调试 dump、统计分析等旁路能力时会继续膨胀。

当前采用：

```text
FrameSinkOps
  -> FrameSink_Register
  -> FrameSink_Find
  -> sink->write(MediaFrame)
```

这样 `snapshot_jpeg` 和 capture / converter / encoder / muxer 一样，遵循项目现有 ops/register 插件风格。

## 2. FrameSink 定位

`FrameSink` 是旁路消费者：

```text
MediaFrame(YUV420P)
  ├── FrameSink(snapshot_jpeg)
  ├── Viewer(SDL)
  └── Encoder(H264)
```

它只读取当前 `MediaFrame`，不改变主视频链路，不修改 frame 元信息，也不影响 encoder / muxer。

失败策略：

```text
sink write failed
  -> print warning
  -> main video pipeline continues
```

## 3. snapshot_jpeg 插件

相关代码：

```text
sink/frame_sink_manager.h
sink/frame_sink_manager.c
sink/snapshot_jpeg_sink.h
sink/snapshot_jpeg_sink.c
```

插件名：

```text
snapshot_jpeg
```

注册位置：

```text
modules/module_register.c
```

注册流程：

```text
extern const FrameSinkOps g_snapshot_jpeg_sink_ops;
FrameSink_Register(&g_snapshot_jpeg_sink_ops);
```

AppPipeline 初始化阶段：

```text
FrameSink_Find("snapshot_jpeg")
snapshot_sink_ops->init(&snapshot_sink_ctx, &config)
```

process thread 中只调用通用接口：

```text
snapshot_sink_ops->write(snapshot_sink_ctx, frame)
```

## 4. 导出频率和路径

当前阶段 `snapshot_jpeg` sink 直接使用 `interval_frames` 控制导出频率。由于主视频 fps 固定为 30，因此 `interval_frames` 设置为 30：

```text
interval_frames = 30
```

也就是每 30 帧导出一次 `snapshot.jpg`，约等于每 1 秒更新一次。

后续如果需要支持动态 fps，可再扩展 `interval_seconds`，或从 `AppConfig` 中推导 `interval_frames`。

输出路径：

```text
edge_ai_camera_test/snapshot.jpg
```

临时路径：

```text
edge_ai_camera_test/snapshot.jpg.tmp
```

写入流程：

```text
write snapshot.jpg.tmp
  ↓
rename(snapshot.jpg.tmp, snapshot.jpg)
```

这样可以避免 Python VLM 服务读到半截 JPEG。

## 5. JPEG 编码方式

输入要求：

```text
MediaFrame.pixfmt = PIX_FMT_YUV420P
```

编码方式：

- 使用 `AV_CODEC_ID_MJPEG` 查找 FFmpeg MJPEG encoder。
- 使用 `AVCodecContext` 配置宽高、time_base、pix_fmt。
- 使用 `libswscale` 将 YUV420P 转成 MJPEG encoder 接受的像素格式。
- 使用 `avcodec_send_frame()` / `avcodec_receive_packet()` 得到 JPEG packet。
- 写 tmp 文件，再 `rename()` 成正式文件。

如果输入不是 `PIX_FMT_YUV420P`，`snapshot_jpeg` 只打印 warning 并返回 `IPC_OK`，不影响主链路。

## 6. 编译

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

## 7. 运行验证

录制 120 帧：

```bash
./bin/ipc_recorder --device /dev/video0 --record output/test.mp4 --processor osd --frames 120
```

只预览：

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

成功日志：

```text
[snapshot_jpeg] wrote edge_ai_camera_test/snapshot.jpg
```

## 8. 当前边界

本次只实现：

```text
FrameSink 插件机制
snapshot_jpeg sink
每 30 帧导出 snapshot.jpg
```

本次不实现：

- Python VLM 服务。
- `event.json`。
- C 进程读取 AI 结果。
- AI 结果叠加 OSD。

一句话结论：

```text
snapshot_jpeg 是一个 FrameSink 插件，不是 process_thread 里的硬编码临时函数。
```
