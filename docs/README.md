# IPC Recorder / Edge AI Camera 文档中心

本文档说明当前 `docs/` 的分类方式、推荐阅读顺序和文档导航。当前项目是 Linux IPC Recorder / Edge AI Camera 原型系统：C 主链路负责视频采集、转换、处理、编码、封装、预览和 snapshot 导出；Python AI 旁路读取 snapshot 并输出 `event.json`。

## 1. 项目简介

IPC Recorder 当前定位为工程原型：

```text
V4L2 Capture
  -> Converter(YUYV422 to YUV420P)
  -> FrameProcessor/OSD
  -> FrameSink(snapshot_jpeg)
  -> H264 Encoder
  -> Muxer(MP4/RTSP)
```

Edge AI 旁路：

```text
edge_ai_camera_test/snapshot.jpg
  -> edge_ai_camera_test/ai_service.py
  -> models/SmolVLM2-500M-Video-Instruct
  -> edge_ai_camera_test/event.json
```

当前 `SmolVLM2-500M-Video-Instruct` 只用于验证本地 VLM 推理链路，不能作为可靠报警模型。

## 2. 文档分类说明

| 分类 | 说明 |
|---|---|
| `project/` | 项目总览、目录结构、编译运行、路线图和阅读指南 |
| `architecture/` | pipeline、插件注册、数据契约、错误处理、线程队列等架构文档 |
| `modules/` | Capture、Converter、FrameProcessor、Encoder、Muxer、FrameSink、Viewer 模块文档 |
| `knowledge/` | V4L2、ioctl、像素格式、H264、FFmpeg、RTSP、SDL 等知识文档 |
| `edge_ai/` | Edge AI Camera 旁路、环境配置、snapshot、AI service、模型评估 |
| `tests/` | 测试计划、手动测试、稳定性测试和测试报告 |

## 3. 推荐阅读顺序

新读者建议按以下顺序阅读：

```text
project/system_overview.md
  ↓
project/directory_structure.md
  ↓
project/build_and_run.md
  ↓
architecture/README.md
  ↓
architecture/pipeline_overview.md
  ↓
modules/module_overview.md
  ↓
edge_ai/edge_ai_camera_design.md
  ↓
tests/test_plan.md
```

如果目标是理解代码实现，建议继续阅读：

```text
architecture/plugin_register.md
architecture/data_contract.md
modules/sink.md
knowledge/yuyv422_to_yuv420p.md
knowledge/ffmpeg_encoder.md
knowledge/ffmpeg_muxer.md
```

## 4. 文档导航

| 分类 | 文档 | 说明 |
|---|---|---|
| Project | [project/system_overview.md](project/system_overview.md) | 系统目标、主视频链路、AI 旁路和项目边界 |
| Project | [project/directory_structure.md](project/directory_structure.md) | 当前目录职责、关键文件和链路关系 |
| Project | [project/build_and_run.md](project/build_and_run.md) | 编译、运行和基础验证命令 |
| Project | [project/dependencies.md](project/dependencies.md) | 系统库和第三方依赖说明 |
| Project | [project/roadmap.md](project/roadmap.md) | 后续开发路线 |
| Project | [project/interview_overview.md](project/interview_overview.md) | 面试讲解视角的项目总结 |
| Project | [project/code_reading_guide.md](project/code_reading_guide.md) | 代码阅读顺序建议 |
| Architecture | [architecture/README.md](architecture/README.md) | 架构文档入口和总览 |
| Architecture | [architecture/pipeline_overview.md](architecture/pipeline_overview.md) | 主链路、旁路、线程和队列说明 |
| Architecture | [architecture/app_pipeline.md](architecture/app_pipeline.md) | AppPipeline 编排说明 |
| Architecture | [architecture/plugin_register.md](architecture/plugin_register.md) | Register / Find / Ops 插件机制 |
| Architecture | [architecture/data_contract.md](architecture/data_contract.md) | MediaFrame / MediaPacket 生命周期契约 |
| Architecture | [architecture/media_data.md](architecture/media_data.md) | 媒体数据结构设计 |
| Architecture | [architecture/error_handling.md](architecture/error_handling.md) | 统一错误码和错误处理机制 |
| Architecture | [architecture/thread_queue.md](architecture/thread_queue.md) | FrameQueue / PacketQueue 说明 |
| Modules | [modules/module_overview.md](modules/module_overview.md) | 各模块输入、输出、插件和文件总览 |
| Modules | [modules/capture.md](modules/capture.md) | Capture 模块文档入口 |
| Modules | [modules/converter.md](modules/converter.md) | Converter 模块文档入口 |
| Modules | [modules/frame_processor.md](modules/frame_processor.md) | FrameProcessor / OSD 模块说明 |
| Modules | [modules/encoder.md](modules/encoder.md) | Encoder 模块文档入口 |
| Modules | [modules/muxer.md](modules/muxer.md) | Muxer 模块文档入口 |
| Modules | [modules/sink.md](modules/sink.md) | FrameSink / snapshot_jpeg 模块文档入口 |
| Modules | [modules/viewer.md](modules/viewer.md) | SDL Viewer 模块说明 |
| Knowledge | [knowledge/v4l2.md](knowledge/v4l2.md) | V4L2 capture 工作流 |
| Knowledge | [knowledge/ioctl.md](knowledge/ioctl.md) | Linux ioctl 说明 |
| Knowledge | [knowledge/pixel_format.md](knowledge/pixel_format.md) | PixelFormat 知识入口 |
| Knowledge | [knowledge/yuyv422_to_yuv420p.md](knowledge/yuyv422_to_yuv420p.md) | YUYV422 到 YUV420P 转换说明 |
| Knowledge | [knowledge/h264.md](knowledge/h264.md) | H264 编码基础 |
| Knowledge | [knowledge/ffmpeg_encoder.md](knowledge/ffmpeg_encoder.md) | FFmpeg H264 encoder 说明 |
| Knowledge | [knowledge/ffmpeg_muxer.md](knowledge/ffmpeg_muxer.md) | FFmpeg muxer / MP4 说明 |
| Knowledge | [knowledge/rtsp.md](knowledge/rtsp.md) | RTSP publisher 测试说明 |
| Knowledge | [knowledge/sdl.md](knowledge/sdl.md) | SDL 显示知识入口 |
| Edge AI | [edge_ai/edge_ai_camera_design.md](edge_ai/edge_ai_camera_design.md) | Edge AI 旁路设计 |
| Edge AI | [edge_ai/env_setup.md](edge_ai/env_setup.md) | Python / Transformers 环境配置 |
| Edge AI | [edge_ai/snapshot.md](edge_ai/snapshot.md) | snapshot.jpg 导出说明 |
| Edge AI | [edge_ai/snapshot_sink.md](edge_ai/snapshot_sink.md) | snapshot_jpeg FrameSink 说明 |
| Edge AI | [edge_ai/ai_service.md](edge_ai/ai_service.md) | AI service 文档入口 |
| Edge AI | [edge_ai/model_evaluation.md](edge_ai/model_evaluation.md) | 模型评估和限制记录 |
| Tests | [tests/test_plan.md](tests/test_plan.md) | 总体测试计划 |
| Tests | [tests/manual_test_cases.md](tests/manual_test_cases.md) | 手动测试用例 |
| Tests | [tests/stability_test.md](tests/stability_test.md) | 稳定性测试计划 |
| Tests | [tests/error_test_report.md](tests/error_test_report.md) | 错误码测试报告 |
| Tests | [tests/test_report.md](tests/test_report.md) | 项目测试记录 |

## 5. 当前文档维护原则

- 根目录只保留 `README.md`。
- 架构类文档放入 `architecture/`。
- 模块类文档放入 `modules/`。
- 多媒体知识说明放入 `knowledge/`。
- Edge AI 相关内容放入 `edge_ai/`。
- 测试计划和报告放入 `tests/`。
- 不在文档中声明“可靠报警已实现”。
