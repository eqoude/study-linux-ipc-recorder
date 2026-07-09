# Linux IPC Recorder / Edge AI Camera

## 项目简介

本项目是一个 Linux 用户态 C 语言视频采集、编码、录制和边缘 AI 旁路分析原型系统。主视频链路使用 V4L2、FFmpeg、H264、MP4/RTSP 等技术实现；AI 旁路通过 FrameSink 导出 `edge_ai_camera_test/snapshot.jpg`，由 Python 服务读取图片并生成 `edge_ai_camera_test/event.json`。

核心设计目标是把实时视频链路和 AI 推理链路解耦：

- C 主链路负责采集、转换、处理、编码、封装、预览和 snapshot 导出。
- Python AI 旁路负责读取 snapshot、调用本地 VLM、输出结构化事件。
- AI 不阻塞 C 主视频链路。
- `SmolVLM2-500M-Video-Instruct` 当前仅用于验证本地 VLM 接入链路，不作为最终可靠报警模型。

当前项目定位：

```text
Linux IPC Recorder / Edge AI Camera engineering prototype
```

## 功能概览

| 功能 | 状态 | 说明 |
|---|---|---|
| V4L2 采集 | 已实现 | 默认设备 `/dev/video0`，用户态 V4L2 capture |
| YUYV422 -> YUV420P 转换 | 已实现 | 纯 C converter，输出 encoder/viewer 可用的 YUV420P |
| H264 编码 | 已实现 | 使用 FFmpeg/libx264 输出 H264 packet |
| MP4 录制 | 已实现 | 通过 MP4 muxer 写入 `output/*.mp4` |
| RTSP 输出结构 | 结构已预留 | 当前为 RTSP publisher / muxer 结构，能力以代码为准 |
| SDL preview 结构 | 结构已预留 | SDL display 模块用于本地预览，能力以代码为准 |
| 插件化模块注册 | 已实现 | Capture / Converter / Encoder / Muxer / Sink 等使用 Register / Find / Ops |
| FrameSink snapshot_jpeg | 已实现 | 每 30 帧导出一次 `edge_ai_camera_test/snapshot.jpg` |
| Python AI service | 原型验证 | `ai_service.py` 读取 snapshot 并调用本地 VLM |
| event.json 输出 | 原型验证 | Python 输出结构化 `edge_ai_camera_test/event.json` |
| 测试文档 | 已实现 | 测试计划、手动测试、稳定性测试位于 `docs/tests/` |

## 系统架构图

主视频链路：

```text
V4L2 Camera
  -> Capture
  -> Converter
  -> FrameProcessor
  -> Encoder
  -> Muxer
  -> MP4 / RTSP
```

Edge AI 旁路：

```text
FrameProcessor
  -> FrameSink(snapshot_jpeg)
  -> edge_ai_camera_test/snapshot.jpg
  -> edge_ai_camera_test/ai_service.py
  -> edge_ai_camera_test/event.json
```

完整关系：

```text
V4L2 Camera
  -> Capture
  -> MediaFrame(YUYV422)
  -> Converter
  -> MediaFrame(YUV420P)
  -> FrameProcessor / OSD
       |\
       | -> FrameSink(snapshot_jpeg)
       |      -> edge_ai_camera_test/snapshot.jpg
       |      -> Python ai_service.py
       |      -> edge_ai_camera_test/event.json
       |
       -> Encoder(H264)
       -> MediaPacket(H264)
       -> Muxer(MP4 / RTSP)
```

## 目录结构说明

| 目录 | 说明 |
|---|---|
| `app/` | 程序入口、配置解析、pipeline 编排 |
| `core/` | 错误码、日志、`MediaFrame`、`MediaPacket`、`ThreadQueue` |
| `capture/` | `fake_capture` / `v4l2_capture` |
| `converter/` | `fake_converter` / `yuyv_to_yuv420_converter` |
| `frame_processor/` | `frame_processor_manager` / `osd_processor` |
| `encoder/` | `fake_encoder` / `h264_ffmpeg_encoder` |
| `muxer/` | `fake_muxer` / `mp4_muxer` / `rtsp_muxer` |
| `sink/` | `frame_sink_manager` / `snapshot_jpeg_sink` |
| `viewer/` | SDL display 相关实现 |
| `modules/` | `module_register` 插件统一注册入口 |
| `edge_ai_camera_test/` | `ai_service.py`、`snapshot.jpg`、`event.json`、测试脚本 |
| `models/` | `SmolVLM2-500M-Video-Instruct` 本地模型 |
| `tests/` | core 模块单元测试 |
| `docs/` | 工程文档体系 |
| `output/` | 录制输出文件 |
| `build/` | 编译中间文件 |
| `bin/` | 可执行文件 |

## 依赖说明

C 侧主要依赖：

- `gcc` / `make`
- Linux V4L2
- FFmpeg libraries
- libx264
- SDL2

Python 侧主要依赖：

- conda 环境 `edge_ai_camera`
- `transformers`
- `torch`
- `pillow`
- `SmolVLM2-500M-Video-Instruct` 本地模型

详细依赖说明见：

- `docs/project/dependencies.md`
- `docs/edge_ai/env_setup.md`

## 编译

```bash
make clean
make
```

编译完成后生成：

```text
bin/ipc_recorder
```

## 运行示例

无输出参数：

```bash
./bin/ipc_recorder
```

当前无输出参数运行时会提示未启用输出，例如 no output enabled，实际行为以当前 `AppConfig` 和 `AppPipeline` 代码为准。

MP4 录制：

```bash
./bin/ipc_recorder --record output/snapshot_test.mp4
```

查看 snapshot 是否更新：

```bash
watch -n 1 'ls -lh edge_ai_camera_test/snapshot.jpg && stat edge_ai_camera_test/snapshot.jpg'
```

运行 AI 服务：

```bash
conda activate edge_ai_camera
python ./edge_ai_camera_test/ai_service.py
```

查看 `event.json`：

```bash
python -m json.tool edge_ai_camera_test/event.json
```

## 当前验证状态

当前已验证：

```bash
./bin/ipc_recorder --record output/snapshot_test.mp4
```

验证结论：

- C 程序可以运行 MP4 录制链路。
- C 程序可以生成 `edge_ai_camera_test/snapshot.jpg`。
- Python `edge_ai_camera_test/ai_service.py` 可以读取 snapshot 并输出 `edge_ai_camera_test/event.json`。
- `SmolVLM2-500M-Video-Instruct` 可以完成本地图片输入和文本输出。
- 当前模型能力有限，输出不稳定，不能直接作为可靠报警依据。

## 文档导航

| 文档 | 说明 |
|---|---|
| `docs/README.md` | 文档系统入口 |
| `docs/project/system_overview.md` | 系统目标、主链路、AI 旁路和项目边界 |
| `docs/project/directory_structure.md` | 目录结构和职责说明 |
| `docs/project/build_and_run.md` | 编译与运行说明 |
| `docs/project/dependencies.md` | 系统依赖和第三方库说明 |
| `docs/architecture/pipeline_overview.md` | Pipeline、线程队列和旁路数据流说明 |
| `docs/architecture/plugin_register.md` | Register / Find / Ops 插件机制说明 |
| `docs/modules/module_overview.md` | 各模块输入、输出、实现文件总览 |
| `docs/modules/sink.md` | FrameSink / snapshot_jpeg 模块说明 |
| `docs/edge_ai/edge_ai_camera_design.md` | Edge AI 旁路设计说明 |
| `docs/edge_ai/env_setup.md` | Python / Transformers 环境配置 |
| `docs/edge_ai/snapshot_sink.md` | snapshot_jpeg sink 设计说明 |
| `docs/edge_ai/ai_service.md` | AI service 文档入口 |
| `docs/edge_ai/model_evaluation.md` | 模型评估和限制记录 |
| `docs/tests/test_plan.md` | 总体测试计划 |
| `docs/tests/manual_test_cases.md` | 手动测试用例 |

## 测试

core 错误码、packet、queue 测试入口：

```bash
./tests/run_error_tests.sh
```

如果使用 Makefile 测试目标：

```bash
make test-error
```

详细测试计划见：

- `docs/tests/test_plan.md`
- `docs/tests/manual_test_cases.md`
- `docs/tests/stability_test.md`

## 已知限制

- `SmolVLM2-500M-Video-Instruct` 模型能力有限，不适合直接作为最终报警模型。
- 当前 AI 旁路使用 `snapshot.jpg` + `event.json` 文件通信，简单可靠但性能不是最优。
- 当前 snapshot 导出按帧数控制，不是严格 wall-clock 定时。
- RTSP / SDL / OSD 等模块结构存在，但具体能力以当前代码为准。
- `models/` 目录体积较大，通常不应提交到 Git。
- 当前 C 主链路尚未实现读取 `event.json` 后的告警闭环。

## 后续方向

- 完善测试体系。
- 加强 RTSP 推流验证。
- 引入专用 person / fire / smoke detector。
- 将 VLM 定位为语义摘要模块。
- C 侧读取 `event.json` 并进行 OSD 叠加。
- 后续考虑 socket / shared memory 替代文件通信。
