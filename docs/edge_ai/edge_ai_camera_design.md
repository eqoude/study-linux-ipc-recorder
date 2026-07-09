# Edge AI Camera 接入设计

本文档说明当前 IPC Recorder 工程如何以旁路方式接入本地 VLM 推理，为什么 AI 不进入 C 主视频链路，以及当前 SmolVLM2-500M 原型验证的限制。

## 1. 为什么 AI 不直接进入 C 主链路

C 主视频链路需要稳定处理实时视频：

```text
capture -> converter -> processor -> encoder -> muxer
```

VLM 推理不适合直接放入该链路：

- 推理耗时不稳定，可能阻塞 capture 或 encoder。
- Python / Transformers / 模型依赖和 C 实时链路耦合会增加故障面。
- 模型输出存在不稳定性，不应影响 MP4 录制或 RTSP 推流。
- AI 只需要低频图像输入，不需要每帧参与处理。

因此当前架构把 AI 设计成旁路：

```text
C 主链路输出 snapshot.jpg
Python AI 服务读取 snapshot.jpg
Python 输出 event.json
```

## 2. 为什么使用 snapshot.jpg 文件通信

当前使用文件作为 C 与 Python 的边界：

```text
edge_ai_camera_test/snapshot.jpg
edge_ai_camera_test/event.json
```

优点：

- C 程序和 Python 服务可以独立启动。
- Python 崩溃不影响 C 主视频链路。
- snapshot 文件可直接查看，便于定位输入问题。
- event 文件可直接检查，便于定位模型输出和解析问题。
- 不需要在原型阶段引入 IPC、RPC、消息队列或共享内存。

snapshot 写入使用 tmp + rename：

```text
snapshot.jpg.tmp -> snapshot.jpg
```

这避免 Python 读取到半截 JPEG。

## 3. snapshot_jpeg_sink 的作用

`snapshot_jpeg_sink` 是 FrameSink 插件，位于 converter / processor 后面。

输入：

```text
MediaFrame(YUV420P)
```

输出：

```text
edge_ai_camera_test/snapshot.jpg
```

当前行为：

- 每 30 帧导出一次。
- 默认视频 fps 为 30，因此约等于每秒一次。
- 使用 FFmpeg MJPEG 编码。
- 写入失败只打印 warning，不中断主视频链路。

关键文件：

```text
sink/frame_sink_manager.c
sink/frame_sink_manager.h
sink/snapshot_jpeg_sink.c
sink/snapshot_jpeg_sink.h
```

## 4. ai_service.py 的作用

`edge_ai_camera_test/ai_service.py` 是 Python AI 旁路服务。

职责：

- 读取 `edge_ai_camera_test/snapshot.jpg`。
- 调用本地 `models/SmolVLM2-500M-Video-Instruct`。
- 要求模型输出结构化字段。
- 对不稳定模型输出做解析和归一化。
- 写出 `edge_ai_camera_test/event.json`。

它不控制 C 主程序，也不直接参与编码、封装或推流。

## 5. event.json 的作用

`event.json` 是 AI 结果的结构化输出。

当前字段：

```json
{
  "timestamp": 1234567890.123,
  "person_visible": true,
  "scene_summary": "A person is standing in a room.",
  "risk_level": "low",
  "should_alert": false,
  "raw_answer": "original model output"
}
```

说明：

- `person_visible`：归一化后的布尔值。
- `scene_summary`：场景摘要。
- `risk_level`：`low` / `medium` / `high`。
- `should_alert`：由 Python 规则生成，不直接相信模型输出。
- `raw_answer`：保留原始模型输出，便于调试。

当前 C 主程序还不读取 `event.json`。

## 6. SmolVLM2-500M 的实验结论

当前模型：

```text
models/SmolVLM2-500M-Video-Instruct
```

实验结论：

- 可以验证本地 VLM 推理链路。
- 可以读取 snapshot 并生成自然语言或结构化输出。
- 输出格式不稳定，可能返回 JSON object、JSON array、类 JSON 或普通文本。
- 对 `risk_level` 的判断不稳定，普通室内画面也可能误判。
- 不能直接作为可靠报警依据。

当前更有价值的工程成果是：

```text
C 主视频链路与 Python AI 推理旁路的解耦架构
```

而不是 SmolVLM2-500M 的报警准确率。

## 7. 当前限制

| 限制 | 说明 |
|---|---|
| 模型能力有限 | 500M VLM 对风险判断不稳定 |
| 低频采样 | 当前每 30 帧只分析一张 snapshot |
| 无历史上下文 | 当前 Python 只分析单张图片 |
| C 不读取 event | 当前没有 C 侧报警闭环 |
| 文件通信简单 | 适合原型，不适合高频复杂事件流 |
| 没有模型置信度 | 当前输出不包含可靠置信度 |

## 8. 后续增强方向

可替换或增强：

- 更强 VLM。
- 专用目标检测模型，例如 person / smoke / fire / helmet / intrusion detector。
- Python 侧加入多帧状态机，减少单帧误判。
- C 侧读取 `event.json` 后叠加告警 OSD。
- 使用 Unix domain socket、ZeroMQ 或共享内存替代文件通信。
- 将 snapshot 间隔从固定帧数改为可配置参数。

后续如果目标是可靠报警，应优先考虑专用检测模型和清晰规则，而不是直接依赖小型通用 VLM 的自由文本判断。
