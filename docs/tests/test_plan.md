# 测试计划

本文档说明 IPC Recorder / Edge AI Camera 原型系统的功能测试、异常测试、稳定性测试和性能测试计划。

## 1. 测试目标

测试目标：

- 验证 C 主视频链路可以运行。
- 验证 MP4 录制可用。
- 验证 snapshot 每秒导出。
- 验证 Python AI 服务可以读取 snapshot 并生成 `event.json`。
- 验证异常场景不会误认为功能正常。
- 为后续稳定性和性能优化提供基线。

## 2. 测试环境

建议记录：

| 项目 | 示例 |
|---|---|
| OS | Ubuntu 22.04 / Debian / target Linux |
| GCC | `gcc --version` |
| FFmpeg | `ffmpeg -version` |
| SDL2 | `pkg-config --modversion sdl2` |
| Camera | `/dev/video0` |
| Model | `models/SmolVLM2-500M-Video-Instruct` |
| Python | `python3 --version` |

基础检查：

```bash
cd ipc_recorder
make clean
make
./bin/ipc_recorder --help
```

## 3. 功能测试用例

| 编号 | 用例 | 命令 / 操作 | 预期结果 |
|---|---|---|---|
| F01 | 无输出参数启动 | `./bin/ipc_recorder` | 程序按默认配置启动或按当前 AppConfig 行为运行，不应崩溃 |
| F02 | 帮助信息 | `./bin/ipc_recorder --help` | 打印命令行参数，退出码为 0 |
| F03 | MP4 录制 | `./bin/ipc_recorder --record output/snapshot_test.mp4 --frames 60` | 生成 MP4 文件 |
| F04 | MP4 检查 | `ffprobe output/snapshot_test.mp4` | 能识别 H264 视频流 |
| F05 | MP4 解码 | `ffmpeg -i output/snapshot_test.mp4 -f null -` | 能解码到结束 |
| F06 | snapshot 导出 | 运行 C 程序后查看 snapshot | `edge_ai_camera_test/snapshot.jpg` 周期更新 |
| F07 | snapshot 文件格式 | `file edge_ai_camera_test/snapshot.jpg` | 显示 JPEG image data |
| F08 | snapshot 非空 | `ls -lh edge_ai_camera_test/snapshot.jpg` | 文件大小大于 0 |
| F09 | Python AI 启动 | `python3 edge_ai_camera_test/ai_service.py` | 进程启动并周期读取 snapshot |
| F10 | event.json 生成 | 查看 `edge_ai_camera_test/event.json` | 文件存在且周期更新 |
| F11 | event.json 合法 JSON | `python3 -m json.tool edge_ai_camera_test/event.json` | JSON 可解析 |
| F12 | preview 模式 | `./bin/ipc_recorder --preview` | SDL 窗口打开并显示画面 |
| F13 | processor 模式 | `./bin/ipc_recorder --record output/osd.mp4 --processor osd --frames 60` | MP4 中可见时间水印 |

snapshot 更新检查：

```bash
watch -n 1 'ls -lh edge_ai_camera_test/snapshot.jpg && stat edge_ai_camera_test/snapshot.jpg'
```

event JSON 检查：

```bash
python3 -m json.tool edge_ai_camera_test/event.json
```

## 4. 异常测试用例

| 编号 | 用例 | 命令 / 操作 | 预期结果 |
|---|---|---|---|
| E01 | 摄像头不存在 | `./bin/ipc_recorder --device /dev/video999 --record output/fail.mp4 --frames 60` | 打印清晰 open failed 错误，不应崩溃 |
| E02 | snapshot.jpg 不存在 | 删除 snapshot 后运行 `ai_service.py` | 打印 snapshot not found，Python 不崩溃 |
| E03 | 模型目录不存在 | 临时改名或移走 `models/SmolVLM2-500M-Video-Instruct` 后运行 AI | Python 报模型加载错误 |
| E04 | event.json 半写风险 | 观察 `event.json.tmp` 和 `event.json` | 正常情况下最终只消费完整 `event.json` |
| E05 | 非法参数 | `./bin/ipc_recorder --fps 0` | 返回参数错误 |
| E06 | RTSP Server 未启动 | `./bin/ipc_recorder --rtsp rtsp://127.0.0.1:8554/live --frames 60` | 打印 RTSP server not available 或 open failed |

## 5. 稳定性测试

建议测试：

| 项目 | 方法 | 关注点 |
|---|---|---|
| 长时间 MP4 | 连续运行 1h / 8h / 24h | 是否崩溃、文件是否可播放 |
| snapshot 长时间导出 | 长时间观察 snapshot 更新时间 | 是否停止更新、是否 0 字节 |
| Python 长时间推理 | 连续运行 AI 服务 | 是否内存增长、是否异常退出 |
| 队列压力 | 同时开启 record + preview + AI | capture 是否阻塞、fps 是否明显下降 |
| 文件原子性 | Python 持续读 snapshot | 是否读到损坏 JPEG |

示例：

```bash
./bin/ipc_recorder --record output/long_run.mp4
```

另一个终端：

```bash
python3 edge_ai_camera_test/ai_service.py
```

## 6. 性能测试

建议记录：

| 指标 | 获取方式 |
|---|---|
| CPU 占用 | `top`, `htop` |
| 内存占用 | `ps`, `pmap`, `valgrind` |
| MP4 fps | `ffprobe output/file.mp4` |
| snapshot 更新间隔 | `stat` 时间戳 |
| AI 单次推理耗时 | Python 中增加时间统计后测试 |
| 队列堆积 | 后续可在 ThreadQueue 增加统计 |

当前重点：

- C 主链路不能被 Python AI 推理阻塞。
- snapshot 写入不能显著影响编码和 muxer。
- Python 模型推理慢时，只影响 `event.json` 更新频率。

## 7. 已知问题

| 问题 | 当前状态 |
|---|---|
| SmolVLM2-500M 输出不稳定 | 已在 `ai_service.py` 中做解析和归一化，但不能保证报警可靠 |
| `should_alert` 可靠性 | 当前由 Python 规则从 `risk_level` 生成，不直接相信模型字段 |
| C 不读取 event.json | 当前未实现 AI 结果回传主链路 |
| snapshot 间隔固定 | 当前固定每 30 帧 |
| RTSP 依赖外部 Server | 当前需要 mediamtx 等外部 RTSP Server |
| 长时间稳定性未充分验证 | 需要补充长期测试报告 |

## 8. 推荐测试顺序

```text
make clean && make
  ↓
./bin/ipc_recorder --help
  ↓
./bin/ipc_recorder --record output/snapshot_test.mp4 --frames 60
  ↓
ffprobe output/snapshot_test.mp4
  ↓
file edge_ai_camera_test/snapshot.jpg
  ↓
python3 edge_ai_camera_test/ai_service.py
  ↓
python3 -m json.tool edge_ai_camera_test/event.json
```
