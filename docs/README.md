# IPC Recorder 文档总入口

## 1. 一句话简介

IPC Recorder 是一个 Linux IPC Camera / Recorder 工程原型：从 V4L2 摄像头采集 YUYV422 图像，转换为 YUV420P，可选做 OSD 和 SDL 本地预览，再编码为 H264，最后保存为 MP4 或推送到外部 RTSP Server。

当前定位是 `engineering prototype`，不是量产级 IPC 系统。

## 2. 当前能做什么

- V4L2 mmap 方式采集 `/dev/videoX`
- 纯 C 实现 `YUYV422 -> YUV420P`
- 可选 `frame_processor/osd_processor.c` 做简单 OSD 矩形绘制
- FFmpeg libavcodec H264 编码
- FFmpeg libavformat MP4 封装
- FFmpeg libavformat RTSP publisher 推流到外部 RTSP Server
- SDL2 本地实时 Preview
- Manager + Ops + Plugin Register 插件架构
- AppConfig 命令行配置
- AppPipeline 多线程 pipeline 编排
- FrameQueue / PacketQueue 跨线程解耦

## 3. 当前完整 pipeline

主数据流：

```text
V4L2 Camera
  ↓
capture
  ↓ MediaFrame(YUYV422)
converter
  ↓ MediaFrame(YUV420P)
frame_processor(optional)
  ↓ MediaFrame(YUV420P)
encoder
  ↓ MediaPacket(H264)
muxer
  ├── MP4: output/test.mp4
  └── RTSP: rtsp://127.0.0.1:8554/live
```

Preview 是 converter / processor 后面的显示旁路：

```text
MediaFrame(YUV420P)
  ├── viewer(SDL Preview)
  └── encoder -> muxer
```

线程化后的实际运行结构：

```text
capture_thread
  ↓ FrameQueue(raw_queue)
process_thread: converter + optional processor + optional viewer
  ↓ FrameQueue(encode_queue)
encode_thread
  ↓ PacketQueue(packet_queue)
mux_thread: MP4 / RTSP
```

## 4. 目录结构

```text
ipc_recorder/
├── app/                 # main、命令行配置、AppPipeline 编排
├── core/                # MediaFrame、MediaPacket、thread_queue
├── capture/             # CaptureManager、fake_capture、v4l2_capture
├── converter/           # ConverterManager、fake_converter、YUYV->YUV420P
├── frame_processor/     # FrameProcessorManager、osd_processor
├── encoder/             # EncoderManager、fake_encoder、H264 FFmpeg encoder
├── muxer/               # MuxerManager、fake_muxer、MP4、RTSP
├── viewer/              # ViewerManager、SDL preview
├── modules/             # RegisterAllModules 插件注册入口
├── docs/                # 工程文档
├── build/               # 编译中间文件
├── bin/                 # ipc_recorder 可执行文件
├── output/              # MP4 输出目录
└── Makefile
```

## 5. 如何编译

依赖：

- GCC
- FFmpeg development package: `libavcodec libavformat libavutil`
- SDL2 development package
- pthread

编译：

```bash
cd ipc_recorder
make clean
make
```

帮助：

```bash
./bin/ipc_recorder --help
```

## 6. 如何运行

### 6.1 只录制 MP4

```bash
./bin/ipc_recorder --device /dev/video0 --record output/test.mp4
```

含义：

- 开启 capture / converter / encoder / mp4_muxer
- 不初始化 SDL viewer
- 不初始化 RTSP muxer

### 6.2 只 Preview

```bash
./bin/ipc_recorder --device /dev/video0 --preview
```

含义：

- 开启 capture / converter / SDL viewer
- 不进入 encoder / muxer
- 关闭 SDL 窗口后 pipeline 请求停止

### 6.3 RTSP 推流

当前项目不是 RTSP Server，而是 RTSP Publisher。必须先启动外部 RTSP Server，例如 mediamtx。

终端 1：

```bash
./mediamtx
```

终端 2：

```bash
./bin/ipc_recorder --device /dev/video0 --rtsp rtsp://127.0.0.1:8554/live
```

终端 3：

```bash
ffplay rtsp://127.0.0.1:8554/live
```

如果 UDP 播放异常：

```bash
ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/live
```

### 6.4 Preview + Record

```bash
./bin/ipc_recorder --device /dev/video0 --preview --record output/test.mp4
```

同一帧 YUV420P 会送给 SDL 显示，同时进入 encoder / MP4 muxer。

### 6.5 Preview + Record + RTSP

```bash
./bin/ipc_recorder --device /dev/video0 \
  --preview \
  --record output/test.mp4 \
  --rtsp rtsp://127.0.0.1:8554/live
```

## 7. 如何验证

### 7.1 ffprobe 检查 MP4

```bash
ffprobe output/test.mp4
```

重点看：

- `Duration`
- `start`
- `fps`
- `tbr/tbn`

当前 MP4 muxer 使用内部 `frame_index` 生成连续时间戳，目标是：

```text
start ≈ 0
fps ≈ config.fps
Duration ≈ written_frames / fps
```

### 7.2 ffmpeg 解码检查

```bash
ffmpeg -i output/test.mp4 -f null -
```

如果能正常解码到末尾，说明 MP4 封装和 H264 基本可用。

### 7.3 ffplay 播放 MP4

```bash
ffplay output/test.mp4
```

### 7.4 ffplay 播放 RTSP

```bash
ffplay -rtsp_transport tcp rtsp://127.0.0.1:8554/live
```

## 8. 当前已知问题

- 当前 V4L2 插件未实现 `VIDIOC_QUERYCAP`、`VIDIOC_G_FMT`、`VIDIOC_S_PARM`，格式能力检测仍需完善。
- `FrameQueue` 会 deep copy frame 数据，可靠但增加内存带宽消耗。
- `PacketQueue` 会 deep copy H264 packet 数据，但 `extradata` 当前是只读引用，由 encoder 生命周期保证。
- RTSP 依赖外部 mediamtx；项目没有实现内置 RTSP Server。
- RTSP 网络断线后没有自动重连。
- SDL Preview 需要本地图形环境。
- 当前日志较多，逐帧打印会影响性能测试。

## 9. 建议阅读顺序

先看：

```text
docs/project/code_reading_guide.md
```

再看：

```text
docs/project/system_overview.md
docs/architecture/app_pipeline.md
docs/architecture/thread_queue.md
docs/architecture/data_contract.md
docs/architecture/plugin_register.md
```

然后按模块阅读 capture / converter / encoder / muxer / viewer / processor 文档。

git init
git rev-parse --show-toplevel
git status
git status --short
git status --ignored --short