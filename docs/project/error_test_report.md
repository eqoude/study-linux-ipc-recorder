# IPC Recorder 错误码机制测试报告

## 1. 测试目的

本报告记录统一错误码机制的测试和迁移结果。目标不是证明项目已经达到量产级，而是确认当前 `engineering prototype` 已经具备清晰的错误返回语义和可读日志。

统一错误码解决的问题：

- 避免业务模块继续混用 `return 0 / return 1 / return -1`。
- 区分成功、暂时无数据、正常结束和真正错误。
- 让 `AppPipeline` 能根据错误类型决定继续、等待、退出或停止。
- 让 V4L2、MP4、RTSP、SDL、queue 等路径的调试日志更明确。

## 2. IpcResult 错误码表

| 错误码 | 数值 | 含义 | 字符串 |
|---|---:|---|---|
| `IPC_OK` | `0` | 成功 | `OK` |
| `IPC_EAGAIN` | `1` | 暂时没有数据，不是严重错误 | `Temporary unavailable` |
| `IPC_EOF` | `2` | 正常结束 | `End of stream` |
| `IPC_ERROR` | `-1` | 通用错误 | `Error` |
| `IPC_EINVAL` | `-2` | 参数错误 | `Invalid argument` |
| `IPC_ENOMEM` | `-3` | 内存分配失败 | `Out of memory` |
| `IPC_EOPEN` | `-4` | 打开设备、文件、URL 失败 | `Open failed` |
| `IPC_EIO` | `-5` | IO 错误 | `I/O error` |
| `IPC_ESTATE` | `-6` | 状态错误 | `Invalid state` |
| `IPC_EUNSUPPORTED` | `-7` | 不支持的格式或功能 | `Unsupported` |
| `IPC_ECODEC` | `-8` | 编码器错误 | `Codec error` |
| `IPC_EMUXER` | `-9` | muxer 错误 | `Muxer error` |
| `IPC_EQUEUE` | `-10` | queue 错误 | `Queue error` |
| `IPC_ETHREAD` | `-11` | thread 错误 | `Thread error` |

统一语义：

- `IPC_OK`：成功。
- `IPC_EAGAIN`：暂时无数据，调用方继续等待或继续循环。
- `IPC_EOF`：正常结束，例如队列关闭、flush 完成、`--help`、用户关闭预览窗口。
- 负数：真正错误，需要输出日志并停止相关流程。

## 3. 测试命令

```bash
make test-error
```

该命令会编译并运行：

- `tests/test_ipc_error.c`
- `tests/test_media_packet.c`
- `tests/test_thread_queue.c`

全部测试通过后输出：

```text
All error handling tests passed.
```

## 4. 错误码字符串测试

`test_ipc_error` 验证 `IpcError_ToString()`：

- 已知错误码能输出预期字符串。
- 未知错误码输出 `Unknown error`。

这保证日志可以打印：

```text
Invalid argument (-2)
Open failed (-4)
Codec error (-8)
Muxer error (-9)
```

而不是只打印 `failed`。

## 5. MediaPacket 测试

`test_media_packet` 验证：

- `MediaPacket_Alloc(NULL, size)` 返回 `IPC_EINVAL`。
- `MediaPacket_Alloc(&packet, 0)` 返回 `IPC_EINVAL`。
- 正常分配返回 `IPC_OK`。
- `MediaPacket_Unref(NULL)` 不崩溃。
- `MediaPacket_Unref(&packet)` 释放并清空 `data / size / owns_data`。
- `MediaPacket_CopyFromAVPacket()` 参数错误返回 `IPC_EINVAL`。
- 正常 `AVPacket` 拷贝返回 `IPC_OK`。
- `MediaPacket_CopyFromAVPacket()` 会 deep copy 数据，不直接引用 `AVPacket->data`。

这对 encoder → muxer 的 packet 生命周期很关键。

## 6. ThreadQueue 测试

`test_thread_queue` 验证：

- `FrameQueue_Init(NULL, size)` 返回 `IPC_EINVAL`。
- `FrameQueue_Init(&queue, 0)` 返回 `IPC_EINVAL`。
- `FrameQueue_Init(&queue, size)` 返回 `IPC_OK`。
- `FrameQueue_Push()` 正常帧返回 `IPC_OK`。
- `FrameQueue_Pop()` 正常帧返回 `IPC_OK`。
- `FrameQueue_Close()` 后空队列 `Pop` 返回 `IPC_EOF`。
- `PacketQueue` 同样覆盖参数错误、正常 push/pop、关闭后 EOF。

当前 `thread_queue` 只有 blocking pop 接口，没有 non-blocking pop 接口，因此空队列非阻塞返回 `IPC_EAGAIN` 的路径暂时无法触发。`IPC_EAGAIN` 已定义，后续如果新增 `TryPop` 或 timeout pop，应在该路径返回 `IPC_EAGAIN`。

## 7. 模块迁移状态

| 模块 | 迁移状态 | 主要错误映射 |
|---|---|---|
| `core` | 已完成 | `media_packet` 使用 `IPC_EINVAL / IPC_ENOMEM / IPC_OK`，`thread_queue` 使用 `IPC_EINVAL / IPC_ENOMEM / IPC_EOF / IPC_OK`。 |
| `capture` | 已完成 | 设备打开失败 `IPC_EOPEN`，ioctl 失败 `IPC_EIO`，`DQBUF` 暂时无数据 `IPC_EAGAIN`。 |
| `converter` | 已完成 | 参数错误 `IPC_EINVAL`，格式不支持 `IPC_EUNSUPPORTED`，内存失败 `IPC_ENOMEM`。 |
| `encoder` | 已完成 | FFmpeg 编码错误 `IPC_ECODEC`，暂时无 packet `IPC_EAGAIN`，flush 完成 `IPC_EOF`。 |
| `muxer` | 已完成 | 文件/URL 打开失败 `IPC_EOPEN`，FFmpeg muxer 错误 `IPC_EMUXER`，状态错误 `IPC_ESTATE`。 |
| `viewer` | 已完成 | SDL 错误 `IPC_EIO`，SDL thread 失败 `IPC_ETHREAD`，用户关闭窗口 `IPC_EOF`。 |
| `frame_processor` | 已完成 | 参数错误 `IPC_EINVAL`，格式不支持 `IPC_EUNSUPPORTED`，内存失败 `IPC_ENOMEM`。 |
| `module_register` | 已完成 | 注册失败透传底层错误码，并打印具体插件名。 |
| `app_config` | 已完成 | 成功 `IPC_OK`，`--help` `IPC_EOF`，命令行参数错误 `IPC_EINVAL`。 |

最终审计结果：

- 业务模块中未发现需要修复的裸 `return 0 / return 1 / return -1`。
- `main.c` 中的 `return 0 / return 1` 是 shell 进程退出码边界，允许保留。
- `tests/` 中的 `return 0 / return 1` 是测试 pass/fail 退出码，允许保留。

## 8. AppPipeline 错误处理

`AppPipeline` 当前处理规则：

- `IPC_OK`：继续 pipeline。
- `IPC_EAGAIN`：继续循环或继续等待，不设置 `pipeline->error`。
- `IPC_EOF`：正常结束对应线程或请求 pipeline 正常退出。
- `ret < 0`：打印 `IpcError_ToString(ret)` 和错误码，设置 `pipeline->error`，触发 stop。

日志格式示例：

```text
[pipeline] CaptureManager_GetFrame failed: I/O error (-5)
[pipeline] EncoderManager_Encode failed: Codec error (-8)
[pipeline] MuxerManager_WritePacket(mp4) failed: Muxer error (-9)
[pipeline] MuxerManager_WritePacket(rtsp) failed: Open failed (-4)
[pipeline] FrameProcessorManager_Process failed: Unsupported (-7)
```

## 9. main.c 退出码转换

`main.c` 负责把 IPC 错误码转换为 shell 进程退出码：

- `AppConfig_ParseArgs()` 返回 `IPC_OK`：继续启动。
- `AppConfig_ParseArgs()` 返回 `IPC_EOF`：说明用户执行 `--help`，进程退出码 `0`。
- `AppConfig_ParseArgs()` 返回负数：打印错误信息，进程退出码 `1`。
- pipeline 返回负数：进程退出码 `1`。
- pipeline 正常结束：进程退出码 `0`。

示例：

```bash
./bin/ipc_recorder --help
echo $?
# 0

./bin/ipc_recorder --fps 0
echo $?
# 1
```

## 10. 对 RTSP / MP4 / V4L2 调试的帮助

### V4L2

- `/dev/videoX` 打不开：`IPC_EOPEN`
- ioctl 失败：`IPC_EIO`
- 暂时没有 frame：`IPC_EAGAIN`
- 格式不支持：`IPC_EUNSUPPORTED`

### MP4

- 输出文件打开失败：`IPC_EOPEN`
- header/write/trailer 失败：`IPC_EMUXER`
- 未 open 或状态不完整：`IPC_ESTATE`

### RTSP

当前项目是 RTSP publisher，不是 RTSP server。没有启动 mediamtx 时，RTSP muxer 会返回 `IPC_EOPEN`，并打印：

```text
[rtsp] RTSP server not available: Open failed (-4). Please start mediamtx or another RTSP server first.
```

这能直接区分 RTSP server 不可用和 H264/muxer 写入错误。

## 11. 当前结论

当前错误码机制已经覆盖核心业务模块和 app 层，适合继续作为工程原型迭代基础。

本轮配置设计修复后：

- `AppConfig` 增加 `pixel_format`，默认值为 `PIX_FMT_YUYV422`。
- `AppPipeline` 构造 `CaptureConfig` 时使用 `pipeline->config.pixel_format`，不再写死 capture 像素格式。
- `CaptureManager` 删除重复的 `device_path / width / height / pixel_format / fps` 字段，只保留 `CaptureConfig config`。
- `CaptureConfig / MuxerConfig / ViewerConfig` 中的字符串字段使用 char 数组，避免保存外部裸指针。
- 插件读取配置时统一通过 `manager->config.xxx`。

项目定位仍然是：

```text
engineering prototype
```

后续如果进入更高工程化阶段，可以继续补充：

- 日志级别；
- 统一日志模块；
- 错误恢复策略；
- 更完整的集成测试；
- V4L2/RTSP/MP4 实机回归脚本。
