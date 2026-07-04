# IPC Recorder 面试讲解稿

## 1. 项目一句话介绍

这是一个 Linux IPC Camera / Recorder 工程原型，用 C 语言实现从 V4L2 摄像头采集、格式转换、可选 OSD、H264 编码，到 MP4 保存、RTSP 推流和 SDL 本地预览的完整视频 pipeline。

准确定位：

```text
engineering prototype
```

不是量产级 IPC 产品。

## 2. 技术栈

- C 语言
- V4L2 mmap capture
- YUYV422 / YUV420P
- FFmpeg libavcodec H264 encoder
- FFmpeg libavformat MP4 muxer
- FFmpeg libavformat RTSP publisher
- SDL2 preview
- pthread
- Manager + Ops
- static plugin registry
- FrameQueue / PacketQueue

## 3. 系统架构

整体数据流：

```text
V4L2 Camera
  ↓
Capture
  ↓ MediaFrame(YUYV422)
Converter
  ↓ MediaFrame(YUV420P)
FrameProcessor(optional)
  ↓ MediaFrame(YUV420P)
  ├── Viewer(SDL Preview)
  ↓
Encoder
  ↓ MediaPacket(H264)
Muxer
  ├── MP4
  └── RTSP
```

线程结构：

```text
capture_thread
  ↓ FrameQueue
process_thread
  ↓ FrameQueue
encode_thread
  ↓ PacketQueue
mux_thread
```

## 4. 为什么用 Manager + Ops

C 没有类和虚函数，所以用结构体模拟面向对象：

- Manager 保存配置、ops、私有上下文
- Ops 保存函数指针

例如 `MuxerManager` 不关心具体是 MP4 还是 RTSP，只调用：

```text
MuxerManager_Open
MuxerManager_WriteHeader
MuxerManager_WritePacket
MuxerManager_WriteTrailer
```

具体行为由 `g_mp4_muxer_ops` 或 `g_rtsp_muxer_ops` 决定。

好处：

- AppPipeline 不直接依赖具体实现
- fake 插件和真实插件可替换
- 后续增加新插件成本低

## 5. 为什么用 Plugin Register

`modules/module_register.c` 统一注册所有插件。

例如：

```text
g_v4l2_capture_ops
g_yuyv_to_yuv420_ops
g_h264_ffmpeg_encoder_ops
g_mp4_muxer_ops
g_rtsp_muxer_ops
g_sdl_display_ops
g_osd_processor_ops
```

AppPipeline 只使用插件名：

```text
capture_name = "v4l2"
converter_name = "yuyv_to_yuv420"
encoder_name = "h264_ffmpeg"
muxer_name = "mp4"
viewer_name = "sdl"
processor_name = "osd"
```

当前是静态注册，不是 `.so` 动态加载。

## 6. 为什么用 MediaFrame / MediaPacket

如果所有模块都直接使用 FFmpeg `AVFrame / AVPacket`：

- capture 会依赖 FFmpeg
- converter 会依赖 FFmpeg
- viewer 和 muxer 边界混乱
- 模块替换困难

所以项目定义：

```text
MediaFrame: 原始图像帧
MediaPacket: 编码后码流
```

数据边界：

```text
capture/converter/processor/viewer -> MediaFrame
encoder/muxer -> MediaPacket
```

## 7. 为什么需要 AppPipeline

早期如果所有逻辑都写在 `main.c`，入口会变得很长。

现在：

```text
main.c:
  AppConfig
  AppPipeline_Init
  AppPipeline_Run
  AppPipeline_Deinit
```

`AppPipeline` 负责：

- 初始化各 manager
- 打开 capture / muxer
- 创建线程
- 连接 queue
- 退出清理

这样入口清晰，模块编排集中。

## 8. 为什么需要多线程 / queue

视频 pipeline 中各模块速度不同：

- capture 按 fps 产生 frame
- encoder 可能波动
- MP4 写盘可能阻塞
- RTSP 网络可能阻塞
- SDL 显示不应该卡住采集

所以引入：

```text
FrameQueue
PacketQueue
```

作用：

- 解耦不同速度模块
- queue 满时阻塞上游，形成背压
- queue 空时阻塞下游，避免 busy loop
- shutdown 时 close queue 唤醒线程

## 9. MP4 时间戳问题怎么解决

历史问题：

```text
start 不为 0
fps 16.58
duration 异常
```

原因：

直接使用上游 pts，时间戳不连续或不从 0 开始。

修复：

MP4 muxer 内部维护：

```text
frame_index
```

并使用：

```text
time_base = 1 / fps
pts = frame_index
duration = 1 frame
```

写入成功后 `frame_index++`。

结果：

```text
start ≈ 0
fps ≈ 30
Duration = frames / fps
```

## 10. RTSP 当前实现模式

当前 RTSP 是 publisher 模式：

```text
ipc_recorder -> mediamtx -> ffplay/VLC
```

ipc_recorder 不实现 RTSP Server。

测试方式：

```bash
./mediamtx
./bin/ipc_recorder --device /dev/video0 --rtsp rtsp://127.0.0.1:8554/live
ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/live
```

RTSP H264 解码依赖 SPS/PPS。当前 encoder 提供 extradata，RTSP muxer 在 header 前复制到 `stream->codecpar`。

## 11. 统一错误码机制怎么设计

早期模块里大量使用 `return 0 / return 1 / return -1`，问题是调用方只能知道“失败”，不知道是参数错误、暂时无数据、EOF、设备打开失败、编码失败还是 muxer 失败。

现在项目定义 `IpcResult`：

```text
IPC_OK = 0          成功
IPC_EAGAIN = 1      暂时无数据
IPC_EOF = 2         正常结束
负数                真正错误
```

常用负数错误包括：

```text
IPC_EINVAL          参数错误
IPC_ENOMEM          内存失败
IPC_EOPEN           打开设备/文件/URL 失败
IPC_EIO             IO 错误
IPC_ESTATE          状态错误
IPC_EUNSUPPORTED    不支持的格式
IPC_ECODEC          编码器错误
IPC_EMUXER          muxer 错误
IPC_ETHREAD         线程错误
```

各模块迁移状态：

- `core / capture / converter / encoder / muxer / viewer / frame_processor` 已迁移。
- `module_register` 会透传具体插件注册失败错误码。
- `app_config` 中 `--help` 返回 `IPC_EOF`，非法参数返回 `IPC_EINVAL`。

`AppPipeline` 的处理方式：

```text
IPC_OK      -> 继续
IPC_EAGAIN  -> 继续等待
IPC_EOF     -> 正常退出
ret < 0     -> 打印 IpcError_ToString(ret)，设置 error 并 stop
```

`main.c` 负责把 IPC 错误码转换成 shell 退出码：

- `--help`：进程退出码 `0`
- 参数错误或 pipeline 错误：进程退出码 `1`

这个机制对调试有直接帮助：

- `/dev/video0` 打不开：`Open failed (-4)`
- V4L2 ioctl 失败：`I/O error (-5)`
- H264 编码失败：`Codec error (-8)`
- MP4 封装失败：`Muxer error (-9)`
- RTSP Server 没启动：`Open failed (-4)`，并提示先启动 mediamtx。

测试命令：

```bash
make test-error
```

它验证错误码字符串、`MediaPacket`、`FrameQueue / PacketQueue` 和 `AppPipeline` 错误日志。

## 12. 我在项目中解决过哪些问题

可以这样讲：

1. 把模块整理为 Manager + Ops + Plugin Register，降低 main 对具体实现的依赖。
2. 引入 AppConfig，让设备、分辨率、fps、preview、record、processor、RTSP 由命令行控制。
3. 把臃肿 main 拆为 AppPipeline，集中管理模块和线程生命周期。
4. 引入 FrameQueue / PacketQueue，让 capture、process、encode、mux 通过队列解耦。
5. 修复 `MediaPacket.data` 生命周期问题，encoder 输出 packet 时 deep copy H264 数据。
6. 修复 MP4 时间戳问题，用 muxer frame_index 生成稳定 pts/dts。
7. 补充 MP4 H264 extradata 写入，保证 MP4 可解码。
8. 实现 RTSP publisher，并明确依赖 mediamtx。
9. 处理 RTSP SPS/PPS 问题，避免 ffplay `non-existing PPS`。
10. 接入 SDL Preview，实现本地实时显示旁路。
11. 引入统一错误码，让 pipeline 能区分 `OK / EAGAIN / EOF / 负数错误`，并打印可读错误信息。

## 13. 当前项目还有哪些待完善点

不要夸大当前项目。可以明确说：

- V4L2 还缺 `VIDIOC_QUERYCAP / G_FMT / S_PARM`
- 没有音频采集和音视频同步
- RTSP 没有断线重连
- 没有内置 RTSP Server
- 没有硬件编码器
- 没有性能统计和丢帧统计
- queue 当前是固定容量阻塞模式，没有丢帧策略
- Preview 需要本地图形环境
- 还需要长时间稳定性和 valgrind 测试
- 统一错误码已经完成，但还没有独立日志系统和错误恢复策略

## 14. 面试回答模板

可以这样概括：

> 这个项目是一个 Linux IPC 视频 pipeline 原型。我把它拆成 capture、converter、processor、viewer、encoder、muxer 几个组件，每个组件用 Manager + Ops 模式封装，再通过 module_register 做静态插件注册。数据层用 MediaFrame 表示原始帧，用 MediaPacket 表示编码包，避免所有模块都直接依赖 FFmpeg。应用层由 AppPipeline 根据 AppConfig 初始化模块，并用 FrameQueue / PacketQueue 把 capture、process、encode、mux 拆成多线程流水线。错误处理上引入统一 IpcResult，区分 OK、EAGAIN、EOF 和真正错误，让 V4L2、FFmpeg、RTSP、SDL 的问题能通过可读日志定位。当前支持 V4L2 采集、YUYV422 到 YUV420P、H264 编码、MP4 保存、RTSP publisher 和 SDL Preview。项目定位是 engineering prototype，后续还需要补齐 V4L2 能力检测、RTSP 重连、音频同步和性能统计。
