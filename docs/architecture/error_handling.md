# IPC Recorder 统一错误码机制

## 1. 为什么引入统一错误码

早期各模块大量使用 `return 0 / return 1 / return -1`，不同文件的含义并不完全一致：

- 有的 `1` 表示暂时没有数据；
- 有的 `-1` 表示参数错误；
- 有的 `-1` 表示设备打开失败、FFmpeg 失败或状态错误；
- `AppPipeline` 只能看到“某个模块 failed”，无法判断错误类型。

统一错误码的目标是让 pipeline 能区分：

- 成功；
- 暂时无数据；
- 正常结束；
- 真正错误；
- 具体错误来源。

这样调试 V4L2、MP4、RTSP、SDL、queue 等路径时，日志能直接给出错误类型，而不是只打印 `failed`。

## 2. IpcResult 错误码表

统一错误码定义在 `core/ipc_error.h`，字符串转换函数定义在 `core/ipc_error.c`。

| 错误码 | 数值 | 含义 | 字符串 |
|---|---:|---|---|
| `IPC_OK` | `0` | 成功 | `OK` |
| `IPC_EAGAIN` | `1` | 暂时没有数据，不是严重错误 | `Temporary unavailable` |
| `IPC_EOF` | `2` | 正常结束，例如队列关闭、flush 完成、用户关闭预览窗口、`--help` | `End of stream` |
| `IPC_ERROR` | `-1` | 通用错误 | `Error` |
| `IPC_EINVAL` | `-2` | 参数错误 | `Invalid argument` |
| `IPC_ENOMEM` | `-3` | 内存分配失败 | `Out of memory` |
| `IPC_EOPEN` | `-4` | 打开设备、文件、URL 失败 | `Open failed` |
| `IPC_EIO` | `-5` | IO 错误 | `I/O error` |
| `IPC_ESTATE` | `-6` | 状态错误，例如未 init 就调用 | `Invalid state` |
| `IPC_EUNSUPPORTED` | `-7` | 不支持的格式或功能 | `Unsupported` |
| `IPC_ECODEC` | `-8` | 编码器错误 | `Codec error` |
| `IPC_EMUXER` | `-9` | muxer 错误 | `Muxer error` |
| `IPC_EQUEUE` | `-10` | queue 错误 | `Queue error` |
| `IPC_ETHREAD` | `-11` | thread 错误 | `Thread error` |

## 3. 返回值语义

项目当前统一遵守以下规则：

- `IPC_OK = 0`：成功。
- `IPC_EAGAIN = 1`：暂时无数据，不是严重错误，调用方可以继续等待或继续循环。
- `IPC_EOF = 2`：正常结束，不作为错误处理。
- `ret < 0`：真正错误，需要打印错误信息，并根据上下文停止当前流程或整个 pipeline。

注意：`main.c` 和 `tests/` 是 IPC 错误码到 shell 进程退出码的边界，保留 `return 0 / return 1` 是合理的，不属于业务模块错误码语义。

## 4. 各模块迁移状态

| 模块 | 当前状态 | 说明 |
|---|---|---|
| `core` | 已迁移 | `ipc_error / media_packet / thread_queue` 使用统一错误码。 |
| `capture` | 已迁移 | V4L2 打开失败返回 `IPC_EOPEN`，ioctl 失败返回 `IPC_EIO`，`DQBUF` 的 `EAGAIN` 返回 `IPC_EAGAIN`。 |
| `converter` | 已迁移 | 参数错误返回 `IPC_EINVAL`，输入格式不支持返回 `IPC_EUNSUPPORTED`，分配失败返回 `IPC_ENOMEM`。 |
| `encoder` | 已迁移 | FFmpeg 编码错误返回 `IPC_ECODEC`，无输出 packet 返回 `IPC_EAGAIN`，flush 完成返回 `IPC_EOF`。 |
| `muxer` | 已迁移 | MP4/RTSP muxer 使用 `IPC_EMUXER / IPC_EOPEN / IPC_ESTATE / IPC_EINVAL` 区分错误。 |
| `viewer` | 已迁移 | SDL 初始化、窗口、renderer、texture、render/update 错误返回 `IPC_EIO`，用户关闭窗口返回 `IPC_EOF`。 |
| `frame_processor` | 已迁移 | OSD 参数错误返回 `IPC_EINVAL`，格式不支持返回 `IPC_EUNSUPPORTED`，分配失败返回 `IPC_ENOMEM`。 |
| `module_register` | 已迁移 | `RegisterAllModules()` 透传每个 register 的错误码，并打印具体插件名。 |
| `app_config` | 已迁移 | 参数解析成功返回 `IPC_OK`，`--help` 返回 `IPC_EOF`，非法参数返回 `IPC_EINVAL`。 |
| `app_pipeline` | 已迁移 | 统一根据 `IPC_OK / IPC_EAGAIN / IPC_EOF / ret < 0` 处理 pipeline 状态。 |

## 5. AppPipeline 如何处理错误

`AppPipeline` 是错误码语义的核心消费方。

通用规则：

```text
IPC_OK
  -> 当前步骤成功，继续 pipeline

IPC_EAGAIN
  -> 暂时无数据，不设置 error，继续循环或等待

IPC_EOF
  -> 正常结束当前线程或请求 pipeline 正常退出

ret < 0
  -> 打印 IpcError_ToString(ret)
  -> 设置 pipeline->error
  -> 触发 stop
```

典型日志格式：

```text
[pipeline] CaptureManager_GetFrame failed: I/O error (-5)
[pipeline] EncoderManager_Encode failed: Codec error (-8)
[pipeline] MuxerManager_WritePacket(rtsp) failed: Open failed (-4)
[pipeline] ViewerManager_Display failed: I/O error (-5)
```

特殊正常结束场景：

- `FrameQueue_Pop()` 返回 `IPC_EOF`：队列关闭，线程正常退出。
- `EncoderManager_Flush()` 返回 `IPC_EOF`：encoder flush 完成。
- `ViewerManager_Display()` 返回 `IPC_EOF`：用户关闭 SDL 预览窗口，pipeline 正常停止。

## 6. main.c 如何转换进程退出码

`main.c` 是 IPC 错误码和 shell 进程退出码的边界。

当前规则：

- `AppConfig_ParseArgs()` 返回 `IPC_OK`：继续启动 `AppPipeline`。
- `AppConfig_ParseArgs()` 返回 `IPC_EOF`：用户执行 `--help`，进程退出码为 `0`。
- `AppConfig_ParseArgs()` 返回负数：打印错误字符串，进程退出码为 `1`。
- pipeline 最终 `ret < 0`：进程退出码为 `1`。
- pipeline 正常结束：进程退出码为 `0`。

因此 `main.c` 中保留 `return 0 / return 1` 是合理的，它们表示 shell 退出码，不是业务模块错误码。

## 7. make test-error 如何验证

测试入口：

```bash
make test-error
```

该目标会编译并运行：

- `tests/test_ipc_error.c`
- `tests/test_media_packet.c`
- `tests/test_thread_queue.c`

覆盖内容：

- `IpcError_ToString()` 是否能输出可读字符串；
- `MediaPacket_Alloc()` 参数错误和正常分配；
- `MediaPacket_CopyFromAVPacket()` 是否 deep copy；
- `FrameQueue / PacketQueue` 参数错误、正常 push/pop、close 后 `IPC_EOF`；
- `app_pipeline.c` 错误日志是否包含错误字符串和错误码。

成功输出：

```text
All error handling tests passed.
```

## 8. 对 V4L2 / MP4 / RTSP 调试的帮助

### 8.1 V4L2

V4L2 capture 可能失败在多个阶段：

- `/dev/videoX` 打不开：`IPC_EOPEN`
- `VIDIOC_QUERYCAP / S_FMT / REQBUFS / QBUF / DQBUF` 失败：`IPC_EIO`
- `DQBUF` 暂时无数据：`IPC_EAGAIN`
- 不支持的像素格式：`IPC_EUNSUPPORTED`

统一错误码后，AppPipeline 能区分“摄像头暂时没帧”和“设备 IO 失败”。

### 8.2 MP4

MP4 muxer 可能失败在：

- 输出文件打开失败：`IPC_EOPEN`
- header/trailer/write packet 失败：`IPC_EMUXER`
- 未 open 或状态不完整：`IPC_ESTATE`

这能快速判断问题是文件路径、封装流程状态，还是 FFmpeg muxer 内部错误。

### 8.3 RTSP

当前 RTSP 是 publisher 模式，需要外部 RTSP Server，例如 mediamtx。

如果 mediamtx 没启动，RTSP muxer 会返回 `IPC_EOPEN`，并打印：

```text
[rtsp] RTSP server not available: Open failed (-4). Please start mediamtx or another RTSP server first.
```

这样能明确区分：

- RTSP Server 不可用：`IPC_EOPEN`
- FFmpeg write/header 错误：`IPC_EMUXER`
- 参数或 URL 错误：`IPC_EINVAL`

## 9. 当前项目定位

统一错误码机制让项目从 demo 式 `return -1` 逐步升级为可诊断的工程原型。

但当前仍然是：

```text
engineering prototype
```

不是量产级 IPC 系统。后续如果继续工程化，还需要补充更完整的运行时监控、日志分级、错误恢复策略、配置校验和自动化集成测试。
