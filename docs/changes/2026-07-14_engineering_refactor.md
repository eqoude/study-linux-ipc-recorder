# 2026-07-14 工程化改造记录

本文档根据当前 `git diff` 和工作区新增文件整理，用于说明本次 IPC Recorder 工程化改造的背景、目标、架构变化、文件级修改、测试记录和当前风险。

## 1. 修改背景

原系统主链路已经具备基础录像能力：

```text
Linux V4L2 Camera
  -> YUYV422
  -> YUV420P
  -> H264 Encoder
  -> MP4 Muxer
```

但在工程化使用中存在几个明显问题：

1. **Ctrl+C 退出可能损坏 MP4 文件**
   - 用户按 Ctrl+C 后进程直接退出或过早关闭线程。
   - MP4 muxer 没有稳定执行 `av_write_trailer()`。
   - 结果表现为 `ffplay` 报错：`moov atom not found`。

2. **摄像头设备路径依赖手工配置**
   - 运行环境从 PC 切换到嵌入式板卡后，摄像头节点可能不是固定 `/dev/video0`。
   - 如果写死 `/dev/videoX`，拔插 USB 摄像头或换平台后容易启动失败。

3. **长时间录像缺少分段能力**
   - 原 MP4 muxer 只输出单个文件。
   - 录像时间较长时，单文件过大，不利于存储管理、异常恢复和后续上传。

4. **模块裁剪需求增强**
   - RK3588 / NanoPC-T6 Plus 部署目标是嵌入式录像设备。
   - 设备侧通常不需要 SDL GUI preview。
   - viewer 模块需要在构建层面可选，避免默认依赖 SDL2。

## 2. 改造目标

本次改造目标是保持现有 Manager + Ops、多线程 pipeline 和主视频链路不变，只补齐工程级能力：

- 支持 SIGINT / SIGTERM 优雅退出。
- Ctrl+C 后仍能 flush encoder、drain mux queue，并写入 MP4 trailer。
- 支持自动扫描 `/dev/video0` 到 `/dev/video99`，自动选择可采集、支持 streaming 的 V4L2 camera。
- 支持 `--segment-time` 参数，按时间生成多个完整 MP4 文件。
- 保持 SDL viewer 为可选模块，默认嵌入式构建不依赖 SDL。
- 保持 `make`、`make test-error`、`make ENABLE_VIEWER=1` 可用。

## 3. 架构变化

### 3.1 修改前 pipeline

```text
main
  |
  v
AppPipeline_Run()
  |
  +--> capture_thread
  |       V4L2 Capture
  |       -> raw FrameQueue
  |
  +--> process_thread
  |       Converter / Processor / Snapshot Sink
  |       -> encode FrameQueue
  |
  +--> encode_thread
  |       H264 Encoder
  |       -> PacketQueue
  |
  +--> mux_thread
          MP4 / RTSP Muxer
```

退出路径较弱：

```text
Ctrl+C
  -> process exits or pipeline stops too early
  -> encoder may not flush
  -> mux queue may not drain
  -> av_write_trailer may not run
  -> MP4 missing moov atom
```

设备选择较固定：

```text
AppConfig.device_path
  -> default /dev/video0
  -> CaptureManager
  -> V4L2 Capture
```

录像输出为单个文件：

```text
H264 MediaPacket
  -> mp4_muxer
  -> output/record.mp4
```

### 3.2 修改后 pipeline

```text
main
  |
  +--> Signal_Init(SIGINT, SIGTERM)
  |
  +--> pipeline_run_thread
  |       AppPipeline_Run()
  |         |
  |         +--> capture_thread
  |         |       V4L2 Capture
  |         |       -> raw FrameQueue
  |         |
  |         +--> process_thread
  |         |       Converter / Processor / Snapshot Sink
  |         |       -> encode FrameQueue
  |         |
  |         +--> encode_thread
  |         |       H264 Encoder
  |         |       -> encoder flush on queue EOF / stop
  |         |       -> PacketQueue
  |         |
  |         +--> mux_thread
  |                 MP4 / Segment MP4 / RTSP Muxer
  |
  +--> main loop waits Signal_IsRunning()
          |
          +-- SIGINT/SIGTERM
                -> AppPipeline_Stop()
                -> stop capture
                -> close raw queue
                -> process thread closes encode queue
                -> encode thread flushes encoder
                -> packet queue closes after flush
                -> mux thread drains packet queue
                -> AppPipeline_Deinit()
                -> MuxerManager_WriteTrailer()
                -> close AVFormatContext
```

自动设备选择：

```text
AppConfig.device_path == ""
  -> DeviceScanner_FindFirstCamera()
       scan /dev/video0 ... /dev/video99
       VIDIOC_QUERYCAP
       require V4L2_CAP_VIDEO_CAPTURE
       require V4L2_CAP_STREAMING
  -> CaptureConfig.device_path
  -> V4L2 Capture
```

分段录像：

```text
H264 MediaPacket
  -> segment_muxer
       record_000001.mp4
       record_000002.mp4
       record_000003.mp4
```

分段切换逻辑：

```text
frames_in_segment >= segment_time * fps
  && packet is keyframe
      -> av_write_trailer(current)
      -> close current file
      -> open next AVFormatContext
      -> avformat_write_header(next)
      -> continue writing packets
```

## 4. 每个修改文件说明

### 文件：`app/app_config.h`

修改内容：

- 新增 `enable_segment`。
- 新增 `segment_time`。

原因：

- 需要通过运行时配置控制是否启用 MP4 分段录像。
- `segment_time` 用于表示每个 MP4 分段的目标时长。

影响：

- `AppConfig` 可以表达普通 MP4 录像和分段 MP4 录像两种模式。
- 不改变原有 `--record`、`--preview`、`--rtsp` 参数语义。

### 文件：`app/app_config.c`

修改内容：

- `--help` 增加 `--segment-time seconds`。
- 默认 `device_path` 从固定 `/dev/video0` 改为空字符串。
- 默认 `segment_time = 60`。
- 解析 `--segment-time` 参数。
- 设置 `enable_record = 1` 和 `enable_segment = 1`。
- `AppConfig_Print()` 输出 `enable_segment` 和 `segment_time`。

原因：

- 空设备路径表示交给 device scanner 自动选择摄像头。
- 分段录像是录制模式的一种，因此设置 `--segment-time` 时自动启用 record。

影响：

- 不传 `--device` 时会自动扫描摄像头。
- 可以使用：

```bash
./bin/ipc_recorder --record output/record.mp4 --segment-time 10
```

### 文件：`app/main.c`

修改内容：

- 引入 `pthread` 和 `signal_handler`。
- 将 `AppPipeline_Run()` 放入独立 `pipeline_run_thread`。
- 主线程执行信号等待循环。
- 收到 SIGINT / SIGTERM 后调用 `AppPipeline_Stop()`。
- 等待 pipeline 线程退出后执行 `AppPipeline_Deinit()`。

原因：

- 如果主线程直接阻塞在 `AppPipeline_Run()` 内部，Ctrl+C 时无法统一控制退出顺序。
- 独立主线程负责信号处理，pipeline 线程负责业务运行，退出路径更清晰。

影响：

- Ctrl+C 不再直接破坏 MP4 写入流程。
- 为后续 systemd / kill SIGTERM 管理进程提供基础。

### 文件：`app/app_pipeline.h`

修改内容：

- 新增接口：

```c
void AppPipeline_Stop(AppPipeline *pipeline);
```

原因：

- main 收到信号后需要通过 pipeline 公共接口请求停止。

影响：

- 外部不需要直接操作 pipeline 内部线程和队列。

### 文件：`app/app_pipeline.c`

修改内容：

- 引入 `capture/device_scanner.h`。
- 当 `config.device_path` 为空时调用 `DeviceScanner_FindFirstCamera()`。
- 初始化 muxer 时根据 `enable_segment` 选择 `segment` muxer。
- 将 `segment_time` 写入 `MuxerConfig`。
- 新增 `AppPipeline_Stop()`。
- capture 获取帧失败时，如果 pipeline 已经 stop，则按正常退出处理。
- encoder flush 循环不再依赖 `pipeline->stop`，确保停止后仍能 flush。

原因：

- 支持自动摄像头选择。
- 支持 MP4 分段 muxer 插件。
- 支持 Ctrl+C 后完整收尾。

影响：

- pipeline 停止顺序变为：停止 capture、关闭 raw queue、process 退出、encoder flush、packet queue 关闭、mux drain、deinit 写 trailer。
- 分段录像只影响 muxer 选择，不停止 capture / converter / encoder 线程。

### 文件：`core/signal_handler.h`

修改内容：

- 新增信号处理接口：

```c
int Signal_Init(void);
int Signal_IsRunning(void);
void Signal_Stop(void);
```

原因：

- 把 POSIX signal 处理从 main 里独立出来，避免 main 直接依赖 signal 细节。

影响：

- main 只需要判断 `Signal_IsRunning()`。

### 文件：`core/signal_handler.c`

修改内容：

- 使用 `sigaction()` 注册 SIGINT 和 SIGTERM。
- signal handler 内只修改 `sig_atomic_t` 标志。
- `Signal_IsRunning()` 返回当前运行状态。

原因：

- signal handler 内不能执行复杂逻辑，不能直接关闭 FFmpeg / V4L2 资源。
- 使用 flag 让主线程在安全上下文中触发 pipeline stop。

影响：

- 支持 Ctrl+C 和 `kill` 的统一停止路径。

### 文件：`capture/device_scanner.h`

修改内容：

- 新增 `CameraDeviceInfo`。
- 新增 `DeviceScanner_FindFirstCamera()`。

原因：

- Capture 初始化前需要获得自动扫描出来的设备路径和设备信息。

影响：

- AppPipeline 可以在不传 `--device` 时自动选择摄像头。

### 文件：`capture/device_scanner.c`

修改内容：

- 扫描 `/dev/video0` 到 `/dev/video99`。
- 对每个节点执行 `open()` 和 `VIDIOC_QUERYCAP`。
- 使用 `cap.device_caps` 或 `cap.capabilities` 判断设备能力。
- 要求设备具备：
  - `V4L2_CAP_VIDEO_CAPTURE`
  - `V4L2_CAP_STREAMING`
- 记录并打印：
  - device path
  - driver
  - card name

原因：

- 嵌入式平台上摄像头节点不稳定，不能依赖固定 `/dev/video0` 或 `/dev/video41`。
- Metadata 节点不应该作为视频采集节点使用。

影响：

- 程序启动时可以自动找到第一个可用 camera。
- 如果没有找到合适设备，返回 `IPC_EOPEN`。

### 文件：`muxer/muxer_manager.h`

修改内容：

- `MuxerConfig` 增加：

```c
int segment_time;
```

原因：

- segment muxer 需要知道目标分段时长。

影响：

- 不影响原 mp4 / rtsp muxer。
- 只有 `segment_muxer` 使用该字段。

### 文件：`muxer/segment_muxer.h`

修改内容：

- 声明：

```c
extern const MuxerOps g_segment_muxer_ops;
```

原因：

- 通过现有 MuxerOps 插件注册机制接入 segment muxer。

影响：

- `module_register.c` 可以注册 `segment` muxer。

### 文件：`muxer/segment_muxer.c`

修改内容：

- 新增 `SegmentMuxerContext`。
- 根据 `output_path` 生成分段文件名：

```text
output/record_000001.mp4
output/record_000002.mp4
...
```

- 每个分段单独创建 `AVFormatContext` 和 `AVStream`。
- 从 `MediaPacket.extradata` 拷贝 H264 SPS/PPS 到 `codecpar->extradata`。
- 每个 segment 调用：
  - `avformat_alloc_output_context2()`
  - `avformat_new_stream()`
  - `avio_open()`
  - `avformat_write_header()`
  - `av_interleaved_write_frame()`
  - `av_write_trailer()`
  - `avio_closep()`
  - `avformat_free_context()`
- 达到 `segment_time * fps` 后，在关键帧处切换到下一个文件。

原因：

- MP4 文件必须有完整 header / trailer。
- 分段文件必须从可解码关键帧开始，避免新文件无法独立播放。

影响：

- 分段只发生在 muxer 内部。
- capture / converter / encoder 不需要停止。
- 每个 segment 是独立 MP4 文件。

### 文件：`modules/module_register.c`

修改内容：

- 新增：

```c
extern const MuxerOps g_segment_muxer_ops;
```

- 在 `RegisterAllModules()` 中注册 `segment_muxer`。

原因：

- 保持 Manager + Ops 插件注册风格，不在 AppPipeline 中硬编码 segment muxer 实现细节。

影响：

- `MuxerManager_Init(..., "segment", ...)` 可以找到 segment muxer。

### 文件：`Makefile`

修改内容：

- 默认 `ENABLE_VIEWER ?= 0`。
- 默认不编译 `viewer/*.c`。
- 默认不调用 `pkg-config --cflags --libs sdl2`。
- `make ENABLE_VIEWER=1` 时才：
  - 定义 `-DENABLE_VIEWER`
  - 增加 `-Iviewer`
  - 编译 `viewer/*.c`
  - 链接 SDL2。

原因：

- RK3588 / NanoPC-T6 Plus 部署目标不需要 GUI preview。
- 默认构建不应强依赖 `libsdl2-dev`。

影响：

- 嵌入式默认构建：

```bash
make
```

- PC 调试 preview 构建：

```bash
make ENABLE_VIEWER=1
```

## 5. 新增功能说明

### 5.1 SIGINT / SIGTERM 优雅退出

新增 `core/signal_handler` 后，进程不在 signal handler 中直接释放资源，而是设置运行标志。主线程检测到停止信号后调用：

```text
AppPipeline_Stop()
  -> CaptureManager_Stop()
  -> FrameQueue_Close(raw_queue)
  -> process_thread exits
  -> encode_thread flushes encoder
  -> PacketQueue closes
  -> mux_thread drains queue
  -> AppPipeline_Deinit()
  -> MuxerManager_WriteTrailer()
```

目标是保证 MP4 muxer 执行 `av_write_trailer()`，避免 `moov atom not found`。

### 5.2 自动 V4L2 设备扫描

当用户不传 `--device` 时，系统扫描 `/dev/video0` 到 `/dev/video99`。

筛选条件：

```text
V4L2_CAP_VIDEO_CAPTURE
V4L2_CAP_STREAMING
```

找到后打印：

```text
[device_scanner] Found camera:
[device_scanner] path: /dev/videoX
[device_scanner] driver: xxx
[device_scanner] card: xxx
```

### 5.3 MP4 分段录像

新增 `segment_muxer`，命令示例：

```bash
./bin/ipc_recorder --record output/record.mp4 --segment-time 10
```

输出示例：

```text
output/record_000001.mp4
output/record_000002.mp4
output/record_000003.mp4
```

分段依据：

```text
segment_time * fps
```

为保证每个 MP4 独立可播放，实际切换点选择在达到目标时长后的关键帧。

### 5.4 SDL viewer 可选

默认构建面向嵌入式录像设备，不包含 SDL viewer：

```bash
make
```

PC 调试时启用 viewer：

```bash
make ENABLE_VIEWER=1
```

这样可以避免 RK3588 部署时安装 GUI / SDL 相关依赖。

## 6. 测试记录

### 6.1 默认构建

命令：

```bash
make clean
make
```

结果：

```text
PASS
```

说明：

- 默认构建不编译 `viewer/*.c`。
- 默认构建不链接 SDL2。
- 成功生成 `bin/ipc_recorder`。

### 6.2 错误码测试

命令：

```bash
make test-error
```

结果：

```text
PASS
All error handling tests passed.
```

说明：

- `ipc_error`、`media_packet`、`thread_queue`、`app_pipeline` 错误日志检查通过。

### 6.3 PC viewer 构建

命令：

```bash
make clean
make ENABLE_VIEWER=1
```

结果：

```text
PASS
```

说明：

- viewer 源文件参与编译。
- SDL2 通过 `pkg-config` 加入编译和链接参数。

### 6.4 建议的硬件验证

Ctrl+C MP4 完整性测试：

```bash
./bin/ipc_recorder --record output/record.mp4
# 运行几秒后 Ctrl+C
ffprobe output/record.mp4
ffplay output/record.mp4
```

分段录像测试：

```bash
./bin/ipc_recorder --record output/record.mp4 --segment-time 10
# 等待约 30 秒后 Ctrl+C
ls -lh output/record_*.mp4
ffprobe output/record_000001.mp4
ffprobe output/record_000002.mp4
ffprobe output/record_000003.mp4
```

自动摄像头扫描测试：

```bash
./bin/ipc_recorder --record output/auto_scan.mp4 --frames 60
```

预期日志包含：

```text
[device_scanner] Found camera:
```

## 7. 当前风险

1. **signal handler 文件当前可能被 `.gitignore` 的 `core` 规则隐藏**
   - 工作区存在 `core/signal_handler.c` 和 `core/signal_handler.h`。
   - 如果 `.gitignore` 仍包含单独的 `core` 规则，新增 core 文件可能不会出现在普通 `git status` 中。
   - 建议后续修正忽略规则，避免提交遗漏。

2. **分段切换依赖关键帧**
   - segment 到达目标时长后，需要等下一个 keyframe 才切文件。
   - 因此实际分段时长可能略大于 `segment_time`。

3. **segment muxer 依赖 encoder 输出 extradata**
   - 每个新 MP4 分段需要 SPS/PPS extradata。
   - 如果后续更换 encoder，需要确认 `MediaPacket.extradata` 仍然可用。

4. **Ctrl+C 路径需要硬件实测确认**
   - 编译和单元测试已通过。
   - 但 V4L2 DQBUF 是否能被 `VIDIOC_STREAMOFF` 及时唤醒，仍需在目标板卡和真实摄像头上验证。

5. **自动扫描只选择第一个符合条件的 camera**
   - 如果系统存在多个摄像头，当前策略不会交互选择。
   - 用户仍可通过 `--device /dev/videoX` 指定设备。

6. **Metadata 节点过滤仍依赖 capability 判断**
   - 当前过滤逻辑基于 `V4L2_CAP_VIDEO_CAPTURE` 和 `V4L2_CAP_STREAMING`。
   - 不同驱动暴露 capability 的方式可能不同，需要在 RK3588 实机上确认。

7. **默认构建禁用 viewer，但 CLI 仍保留 `--preview`**
   - 默认 `make` 下使用 `--preview` 会返回 viewer build disabled 类错误。
   - PC 调试必须使用 `make ENABLE_VIEWER=1`。

8. **文档记录的是当前改造状态，不等于完整稳定性结论**
   - 长时间录像、异常断电、磁盘满、摄像头热插拔过程中的稳定性仍需后续测试。
