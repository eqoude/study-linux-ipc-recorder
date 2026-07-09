# 工程依赖说明

本文说明当前 IPC Recorder 工程依赖的系统库、第三方库，以及它们服务的模块。内容以当前 Makefile 和代码实际 include / link 为准。

## 1. 基础系统依赖

当前工程运行在 Linux 用户态，依赖 Linux 系统接口。

| 依赖 | 对应模块 | 作用 |
|---|---|---|
| libc | 所有 C 源文件 | 基础 C 运行库，提供 `malloc/free`、`printf`、`memcpy`、`time`、`localtime_r`、`strftime` 等基础函数。 |
| pthread | `app/app_pipeline.c`、`core/thread_queue.c`、capture / process / encode / mux thread | 创建线程、互斥锁、条件变量，实现多线程流水线和队列同步。 |
| Linux V4L2 | `capture/v4l2_capture.c` | 访问摄像头设备，执行 `VIDIOC_S_FMT`、`VIDIOC_REQBUFS`、`VIDIOC_QBUF`、`VIDIOC_DQBUF` 等采集流程。 |
| Linux kernel headers | `capture/v4l2_capture.c` | 提供 `<linux/videodev2.h>`，用于 V4L2 ioctl 结构体和宏定义。 |

## 2. FFmpeg 相关依赖

FFmpeg 是当前工程中 H264 编码、MP4 封装和 RTSP publisher 的核心依赖。Makefile 当前通过 `pkg-config` 获取 `libavcodec`、`libavformat`、`libavutil` 的编译和链接参数。

| 依赖库 | 对应模块 | 作用 |
|---|---|---|
| libavcodec | `encoder/h264_ffmpeg_encoder.c`、`core/media_packet.h` | 创建 H264 encoder，执行 `avcodec_send_frame()`、`avcodec_receive_packet()`，输出 H264 `AVPacket`。 |
| libavformat | `muxer/mp4_muxer.c`、`muxer/rtsp_muxer.c` | 创建 MP4 / RTSP 输出上下文，创建 `AVStream`，写 header、packet、trailer。 |
| libavutil | encoder、muxer、core、sink | 提供 `AVRational`、`av_malloc`、`av_free`、`av_rescale_q`、`AVFrame`、`AVPacket` 等辅助能力。 |
| libswscale | `sink/snapshot_jpeg_sink.c` | 将 `MediaFrame(YUV420P)` 转成 MJPEG encoder 接受的像素格式，用于 JPEG snapshot 导出。 |

当前工程的 YUYV422 到 YUV420P 转换仍是纯 C 实现；`libswscale` 只用于 `snapshot_jpeg` FrameSink 的 JPEG 导出路径。

## 3. 图像显示依赖

| 依赖库 | 对应模块 | 作用 |
|---|---|---|
| SDL2 | `viewer/sdl_display_sdl.c` | 创建本地预览窗口、renderer、YUV texture，将 YUV420P 图像显示到窗口，用于本地实时预览和调试。 |

SDL2 是当前 Makefile 的实际链接依赖。即使运行时不启用 `--preview`，工程编译仍会链接 SDL2。

## 4. 当前自研模块不属于第三方依赖

以下模块是工程内部代码，不需要系统安装，只参与本工程编译链接：

- `core/media_frame`
- `core/media_packet`
- `core/thread_queue`
- `capture_manager`
- `converter_manager`
- `encoder_manager`
- `muxer_manager`
- `frame_processor`
- `viewer_manager`
- `app_pipeline`
- `module_register`

这些模块通过 Manager + Ops、MediaFrame / MediaPacket、thread_queue 等工程内部机制协作，不属于外部依赖库。

## 5. Ubuntu 安装示例

Ubuntu 22.04 下可安装当前工程编译所需依赖：

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    pkg-config \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libsdl2-dev
```

V4L2 调试工具：

```bash
sudo apt install -y v4l-utils
```

注意：

- `v4l-utils` 是摄像头调试工具，不是编译必须依赖。
- V4L2 编译主要依赖 Linux kernel headers 中的 `<linux/videodev2.h>`。
- 当前 Makefile 会编译 viewer 模块，因此 `libsdl2-dev` 是当前构建路径的编译依赖。

## 6. pkg-config / 编译链接说明

Makefile 当前使用：

```make
FFMPEG_CFLAGS := $(shell pkg-config --cflags libavcodec libavformat libavutil libswscale)
FFMPEG_LIBS := $(shell pkg-config --libs libavcodec libavformat libavutil libswscale)
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)
```

等价命令：

```bash
pkg-config --cflags --libs libavcodec libavformat libavutil libswscale
pkg-config --cflags --libs sdl2
```

pthread 当前通过 Makefile 链接：

```make
LDFLAGS := -pthread
```

## 7. 可选依赖 / 后续依赖

这些依赖不是当前代码编译必须依赖，属于运行测试工具或后续增强方向。

| 依赖 | 对应模块 / 场景 | 作用 | 说明 |
|---|---|---|---|
| mediamtx | RTSP 测试环境 | 作为 RTSP server，接收 IPC Recorder 推流，供 ffplay / VLC 拉流测试。 | 不是代码编译依赖，是 RTSP publisher 测试工具。 |
| ffplay | 测试验证 | 播放 RTSP / MP4，验证画面、延迟、SPS/PPS、关键帧。 | 来自 FFmpeg 工具包，不参与工程链接。 |
| ffprobe | 测试验证 | 检查 MP4 / RTSP 的编码参数、PTS/DTS、duration、fps、码率、关键帧等。 | 来自 FFmpeg 工具包，不参与工程链接。 |
| RK MPP | 后续 RK3566 / Rockchip 硬件编码 | 替换软件 H264 编码，提高嵌入式平台编码性能。 | 当前未接入，不是主依赖。 |
| GStreamer / live555 | 后续可选 RTSP server 实现 | 如果未来不依赖 mediamtx，可考虑在设备端实现 RTSP server。 | 当前 FFmpeg RTSP muxer 是 publisher，不直接依赖它们。 |

## 8. 总结

当前工程的核心编译依赖是 Linux + pthread + FFmpeg + SDL2。

- V4L2 用于摄像头采集。
- FFmpeg 用于 H264 编码、MP4 / RTSP 封装，以及 snapshot JPEG 导出。
- pthread 用于多线程 pipeline 和 queue 同步。
- SDL2 用于本地实时 preview。

`mediamtx`、`ffplay`、`ffprobe`、RK MPP、GStreamer、live555 等属于运行测试、调试验证或后续平台增强依赖，不是当前工程主编译依赖。
