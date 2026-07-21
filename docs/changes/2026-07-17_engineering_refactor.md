# 2026-07-17 工程化改造记录：Pipeline 生命周期重构

本文档记录本次 IPC Recorder pipeline lifecycle redesign。目标是把当前应用生命周期调整为更接近 Linux daemon 的模型，降低 Ctrl+C / SIGTERM 退出时 MP4 损坏风险，并明确 main、pipeline、worker threads、muxer 的职责边界。

## 1. 修改背景

改造前生命周期存在以下问题：

1. `AppPipeline_Run()` 职责过重
   - 同时负责启动 pipeline worker threads。
   - 同时负责 `pthread_join()` 等待线程结束。
   - Start 和 Wait 职责混合，不利于 main 统一管理进程生命周期。

2. `main.c` 存在额外 pipeline wrapper thread
   - main 创建一个额外 thread 去调用 `AppPipeline_Run()`。
   - `AppPipeline_Run()` 内部再创建 capture/process/encode/mux worker threads。
   - 线程层级变成：

```text
main
  ↓
pipeline thread
  ↓
worker threads
```

3. signal 处理采用 flag polling
   - 原逻辑使用 `Signal_IsRunning()` 加 `sleep(1)` 轮询。
   - 这不是典型 Linux daemon / service 的 signal 等待模型。

4. muxer trailer 在 deinit 阶段写入
   - `AppPipeline_Deinit()` 中调用 `MuxerManager_WriteTrailer()`。
   - 这会让 main/deinit 阶段写 muxer，而 muxer packet 写入在 mux thread 中完成。
   - 对 FFmpeg `AVFormatContext` 来说，跨线程写入生命周期不清晰。

## 2. 修改目标

采用 Linux daemon 风格生命周期：

```text
Init
  ↓
Start
  ↓
Signal_Wait
  ↓
Stop
  ↓
Wait
  ↓
Deinit
```

目标职责划分：

| 模块 | 职责 |
|---|---|
| `main.c` | 配置解析、signal 等待、pipeline 生命周期调度 |
| `AppPipeline_Init()` | 插件注册、manager 初始化、queue 初始化 |
| `AppPipeline_Start()` | 启动 capture，创建 worker threads，立即返回 |
| `Signal_Wait()` | main thread 使用 `sigwait()` 等待 SIGINT / SIGTERM |
| `AppPipeline_Stop()` | 请求停止，不 join |
| `AppPipeline_Wait()` | join worker threads |
| `mux_thread` | muxer open/header/write/trailer/close 的唯一执行线程 |
| `AppPipeline_Deinit()` | 释放资源，不写文件 trailer |

## 3. 架构变化

### 3.1 修改前

```text
main
  |
  | pthread_create
  v
pipeline thread
  |
  | AppPipeline_Run
  v
+-------------------+
| create workers    |
|                   |
| capture_thread    |
| process_thread    |
| encode_thread     |
| mux_thread        |
|                   |
| pthread_join all  |
+-------------------+
```

signal 处理：

```text
SIGINT
  ↓
signal handler sets flag
  ↓
main sleep polling Signal_IsRunning()
  ↓
AppPipeline_Stop
```

muxer 关闭：

```text
mux_thread writes packets
  ↓
thread exits
  ↓
AppPipeline_Deinit
  ↓
MuxerManager_WriteTrailer
  ↓
MuxerManager_Close
```

### 3.2 修改后

```text
main
  |
  | AppPipeline_Init
  | AppPipeline_Start
  v
worker threads
  ├── capture_thread
  ├── process_thread
  ├── encode_thread
  └── mux_thread

main
  |
  | Signal_Wait(SIGINT / SIGTERM)
  v
AppPipeline_Stop
  |
  v
AppPipeline_Wait
  |
  v
AppPipeline_Deinit
```

signal 处理：

```text
Signal_Init
  ↓
pthread_sigmask(SIG_BLOCK, SIGINT/SIGTERM)
  ↓
worker threads inherit blocked signal mask
  ↓
main thread calls sigwait()
  ↓
main receives SIGINT/SIGTERM synchronously
```

muxer 关闭：

```text
packet_queue EOF
  ↓
mux_thread drains pending packet
  ↓
MuxerManager_WriteTrailer
  ↓
MuxerManager_Close
  ↓
mux_thread exit
```

## 4. API 变化

旧 API：

```text
AppPipeline_Init
AppPipeline_Run
AppPipeline_Stop
AppPipeline_Deinit
```

新 API：

```text
AppPipeline_Init
AppPipeline_Start
AppPipeline_Wait
AppPipeline_Stop
AppPipeline_Deinit
```

变化说明：

- 删除 `AppPipeline_Run()`。
- `AppPipeline_Start()` 只启动线程，不等待线程。
- `AppPipeline_Wait()` 只等待线程。
- `AppPipeline_Stop()` 只请求停止，不负责 join。

## 5. signal_handler 变化

旧模型：

```text
sigaction handler
  ↓
set running flag
  ↓
main loop polling Signal_IsRunning()
```

新模型：

```text
Signal_Init
  ↓
sigemptyset / sigaddset(SIGINT, SIGTERM)
  ↓
pthread_sigmask(SIG_BLOCK)
  ↓
Signal_Wait
  ↓
sigwait
```

设计原因：

- signal handler 中不直接 stop pipeline。
- signal handler 中不释放 FFmpeg / V4L2 / queue 资源。
- main thread 以同步方式接收 SIGINT / SIGTERM。
- worker threads 不接收 SIGINT / SIGTERM。

## 6. shutdown 流程

最终 Ctrl+C 退出流程：

```text
SIGINT
  ↓
main thread: sigwait returns
  ↓
AppPipeline_Stop
  ↓
pipeline->stop = 1
  ↓
CaptureManager_Stop
  ↓
raw_queue close
  ↓
process_thread exit
  ↓
encode_queue close
  ↓
encode_thread drains encode_queue
  ↓
EncoderManager_Flush
  ↓
packet_queue close
  ↓
mux_thread drains packet_queue
  ↓
MuxerManager_WriteTrailer
  ↓
MuxerManager_Close
  ↓
AppPipeline_Wait returns
  ↓
AppPipeline_Deinit releases resources
```

目标结果：

- MP4 muxer 可以写入 trailer。
- Ctrl+C 后不应出现 `moov atom not found`。
- muxer 文件写入操作集中在 mux thread 内。

## 7. 修改文件列表

### 文件：`app/main.c`

修改内容：

- 删除 `PipelineRunContext`。
- 删除额外 `pipeline_run_thread`。
- 删除 `pthread_create()` 包装 `AppPipeline_Run()` 的逻辑。
- 删除 `Signal_IsRunning()` + `sleep(1)` 轮询。
- 改为：

```text
Signal_Init
AppPipeline_Init
AppPipeline_Start
Signal_Wait
AppPipeline_Stop
AppPipeline_Wait
AppPipeline_Deinit
```

原因：

- main 只负责进程生命周期控制。
- 避免多余 pipeline wrapper thread。
- 使用 Linux daemon 常见 signal wait 模型。

影响：

- worker threads 仍由 AppPipeline 管理。
- main 不参与视频处理。

### 文件：`app/app_pipeline.h`

修改内容：

- 删除 `AppPipeline_Run()` 声明。
- 新增：

```c
int AppPipeline_Start(AppPipeline *pipeline);
int AppPipeline_Wait(AppPipeline *pipeline);
```

原因：

- 拆分 Start 和 Wait 职责。

影响：

- 外部调用方必须使用新的生命周期 API。

### 文件：`app/app_pipeline.c`

修改内容：

- `AppPipeline_Run()` 拆分为 `AppPipeline_Start()` 和 `AppPipeline_Wait()`。
- `AppPipeline_Start()` 负责：
  - `CaptureManager_Open()`
  - `CaptureManager_Start()`
  - 创建 mux/encode/process/capture worker threads
- `AppPipeline_Wait()` 负责 join worker threads。
- `AppPipeline_Stop()` 保持只发停止请求：停止 capture，关闭 raw queue。
- `encode_thread` 在 encode queue EOF 后执行 `EncoderManager_Flush()`。
- `mux_thread` 在 packet queue EOF 后执行：
  - `MuxerManager_WriteTrailer()`
  - `MuxerManager_Close()`
- `AppPipeline_Deinit()` 不再写 muxer trailer / close muxer 文件句柄。

原因：

- 明确 pipeline 启动、停止、等待、释放职责。
- muxer 的 FFmpeg `AVFormatContext` 写操作集中在 mux thread。

影响：

- Ctrl+C shutdown 路径更清晰。
- MP4 trailer 写入位置从 deinit 阶段移动到 mux thread 退出阶段。
- 保持自动摄像头扫描、segment muxer、snapshot sink、optional viewer 逻辑不变。

### 文件：`core/signal_handler.h`

修改内容：

- 删除：

```c
int Signal_IsRunning(void);
void Signal_Stop(void);
```

- 新增：

```c
int Signal_Wait(void);
```

原因：

- 不再使用 flag polling。

影响：

- main 使用同步 signal wait。

### 文件：`core/signal_handler.c`

修改内容：

- 删除 `sigaction` handler 和 `sig_atomic_t` flag。
- 使用 `sigemptyset()`、`sigaddset()`、`pthread_sigmask()`。
- 使用 `sigwait()` 等待 SIGINT / SIGTERM。

原因：

- worker threads 继承 blocked signal mask。
- 只有 main thread 负责处理退出信号。

影响：

- 不在异步 signal handler 中执行复杂资源操作。

### 文件：`docs/architecture/app_pipeline.md`

修改内容：

- 更新 AppPipeline 生命周期说明。
- 用 `Init -> Start -> Wait -> Stop -> Deinit` 替换旧 `Run` 模型。
- 补充 mux thread 写 trailer / close 的职责。

原因：

- 文档与当前代码一致。

影响：

- 代码阅读路径更清晰。

### 文件：`docs/architecture/pipeline_overview.md`

修改内容：

- 补充 pipeline 生命周期章节。
- 更新 mux thread 职责：open/header/write/trailer/close。

原因：

- pipeline 总览需要体现当前 shutdown 设计。

影响：

- 架构图与当前 tree 一致。

### 文件：`docs/project/system_overview.md`

修改内容：

- 更新系统能力说明：自动扫描 camera、Segment MP4 muxer。
- 补充 AppPipeline 生命周期。

原因：

- 项目总览需要同步当前工程状态。

影响：

- 对外项目说明更准确。

## 8. 测试记录

### 8.1 默认构建

命令：

```bash
make clean
make
```

结果：

```text
PASS
```

### 8.2 viewer 可选构建

命令：

```bash
make clean
make ENABLE_VIEWER=1
```

结果：

```text
PASS
```

### 8.3 错误码测试

命令：

```bash
make test-error
```

结果：

```text
PASS
All error handling tests passed.
```

## 9. 当前架构说明

当前 pipeline lifecycle：

```text
main
  ↓
Signal_Init
  ↓
AppPipeline_Init
  ↓
AppPipeline_Start
  ↓
Signal_Wait
  ↓
AppPipeline_Stop
  ↓
AppPipeline_Wait
  ↓
AppPipeline_Deinit
```

当前 worker pipeline：

```text
capture_thread
  ↓ raw_queue
process_thread
  ↓ encode_queue
encode_thread
  ↓ packet_queue
mux_thread
```

当前 shutdown 责任边界：

| 阶段 | 负责者 |
|---|---|
| 接收 SIGINT / SIGTERM | main thread / `Signal_Wait()` |
| 停止 capture | `AppPipeline_Stop()` |
| 关闭 raw queue | `AppPipeline_Stop()` |
| 关闭 encode queue | `process_thread` |
| encoder flush | `encode_thread` |
| 关闭 packet queue | `encode_thread` |
| 写 MP4 / RTSP trailer | `mux_thread` |
| join worker threads | `AppPipeline_Wait()` |
| manager 资源释放 | `AppPipeline_Deinit()` |

## 10. 当前风险

1. `Signal_Wait()` 是 daemon 模式入口
   - 程序启动后等待 SIGINT / SIGTERM。
   - 适合长期运行服务。

2. `--frames` 有限帧模式需要后续单独确认交互语义
   - 当前生命周期以 signal-driven daemon 为主。
   - 如果需要保留有限帧自动退出，需要增加 pipeline completion notification，而不是恢复 sleep polling。

3. V4L2 stop 唤醒行为依赖驱动
   - `CaptureManager_Stop()` 后阻塞中的 DQBUF 是否立即返回，需要在目标硬件上验证。

4. MP4 完整性仍需实机验证
   - 编译和单元测试通过。
   - Ctrl+C 后 `ffprobe / ffplay` 验证需要摄像头和真实输出文件。
