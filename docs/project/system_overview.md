# 系统总览

本文档说明 IPC Recorder / Edge AI Camera 原型系统的目标、主视频链路、AI 旁路链路、当前实现状态和项目边界。

## 1. 项目目标

本项目目标是构建一个可运行的 Linux IPC Camera 原型：

```text
摄像头采集
  → 像素格式转换
  → 图像处理
  → H264 编码
  → MP4/RTSP 输出
  → snapshot 旁路
  → Python Edge AI 推理
```

工程重点：

- 用 C 实现稳定的主视频链路。
- 用 Manager + Ops + Register / Find 实现插件化模块管理。
- 用 ThreadQueue 解耦 capture、process、encode、mux 阶段。
- 用 FrameSink 插件导出低频 JPEG snapshot。
- 用 Python AI 服务读取 snapshot 并输出结构化事件。

## 2. 系统功能

| 功能 | 当前实现 |
|---|---|
| 摄像头采集 | V4L2 mmap，默认 `/dev/video0` |
| 像素格式转换 | `YUYV422 -> YUV420P` |
| 图像处理 | OSD 时间水印 |
| 编码 | FFmpeg/libx264 H264 |
| 录像 | MP4 muxer |
| 推流 | RTSP muxer publisher 结构 |
| 预览 | SDL display 结构 |
| Snapshot | `snapshot_jpeg_sink` 每 30 帧输出 JPEG |
| AI 推理 | `ai_service.py` 调用本地 SmolVLM2-500M |
| 事件输出 | `event.json` |

## 3. 主视频链路

主视频链路由 C 程序实现，负责实时采集、转换、处理、编码、封装、预览和 snapshot 导出。

```text
V4L2 Capture
  ↓
MediaFrame(YUYV422)
  ↓
Converter(YUYV422 -> YUV420P)
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

主链路由 `app/app_pipeline.c` 编排，入口由 `app/main.c` 和 `app/app_config.c` 负责命令行配置。

当前已验证：

```bash
cd ipc_recorder
./bin/ipc_recorder --record output/snapshot_test.mp4
```

该命令可以启动 C 主链路并生成 MP4，同时按固定间隔导出：

```text
edge_ai_camera_test/snapshot.jpg
```

## 4. AI 旁路链路

AI 旁路由 Python 实现，不进入 C 主视频链路。

```text
C 主程序
  ↓
snapshot_jpeg_sink
  ↓
edge_ai_camera_test/snapshot.jpg
  ↓
edge_ai_camera_test/ai_service.py
  ↓
models/SmolVLM2-500M-Video-Instruct
  ↓
edge_ai_camera_test/event.json
```

`ai_service.py` 的职责：

- 读取 `snapshot.jpg`。
- 调用本地 SmolVLM2-500M-Video-Instruct。
- 解析模型输出。
- 生成固定结构的 `event.json`。

AI 旁路不会阻塞 C 主视频链路。C 程序只负责输出 snapshot 文件；Python 推理速度、模型异常或解析失败不影响采集、编码和 muxer。

当前已验证：

- C 程序可以每秒导出 `edge_ai_camera_test/snapshot.jpg`。
- Python `ai_service.py` 可以读取 `snapshot.jpg` 并输出 `edge_ai_camera_test/event.json`。

## 5. 当前已实现功能

| 子系统 | 已实现内容 |
|---|---|
| app | AppConfig 参数解析，AppPipeline 线程编排 |
| core | 错误码、日志、MediaFrame、MediaPacket、FrameQueue、PacketQueue |
| capture | fake capture、V4L2 capture |
| converter | fake converter、YUYV422 到 YUV420P |
| frame_processor | OSD processor |
| encoder | fake encoder、H264 FFmpeg encoder |
| muxer | fake muxer、MP4 muxer、RTSP muxer |
| sink | FrameSink manager、snapshot_jpeg sink |
| viewer | SDL display |
| modules | 静态插件注册 |
| edge_ai_camera_test | AI 服务脚本、snapshot、event 输出 |
| tests | core 模块错误码和队列测试 |

## 6. 当前未完成内容

| 未完成项 | 说明 |
|---|---|
| 可靠报警 | SmolVLM2-500M 输出不稳定，不能作为可靠报警模型 |
| C 侧读取 event.json | 当前 C 主程序不读取 AI 结果 |
| 动态 snapshot 配置 | 当前 snapshot 间隔固定为 30 帧 |
| 完整 RTSP Server | 当前是 RTSP publisher，需要外部 mediamtx |
| 音频链路 | 当前没有 audio capture / encode / mux |
| 硬件编码 | 当前未接入 RK MPP 等硬件 H264 编码器 |
| 长时间稳定性验证 | 需要补充 24h 运行、内存、队列堆积和异常恢复测试 |

## 7. 项目边界

本项目当前是工程原型，不是量产系统。

明确边界：

- C 主视频链路负责实时多媒体处理。
- Python AI 旁路负责低频 snapshot 推理。
- AI 不阻塞 C 主链路。
- AI 输出仅作为后续事件输入，不代表已实现可靠报警。
- SmolVLM2-500M 只用于验证本地 VLM 推理链路。

当前更有价值的工程结论：

```text
主视频链路和 AI 推理旁路通过 snapshot.jpg / event.json 文件边界解耦。
```
