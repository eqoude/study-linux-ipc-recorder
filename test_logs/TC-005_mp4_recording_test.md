# TC-005 MP4 Recording Test

## 1. Test Objective

验证 V4L2 摄像头采集、YUYV422 到 YUV420P 转换、H264 编码、MP4 封装和 snapshot 输出链路是否可用。

本测试覆盖主视频链路：

```text
V4L2 Camera
  -> Capture
  -> Converter(YUYV422 to YUV420P)
  -> H264 Encoder
  -> MP4 Muxer
```

同时观察 FrameSink 旁路是否有 snapshot 输出日志。

## 2. Test Environment

- Host OS: Ubuntu Linux
- Target: Local x86_64 development machine
- Program: `bin/ipc_recorder`
- Camera: Integrated Camera, `/dev/video0`
- Build Type: Debug build, `gcc -Wall -Wextra -g -O0`

## 3. Test Command

摄像头枚举命令：

```bash
v4l2-ctl --list-devices
```

摄像头格式查询命令：

```bash
v4l2-ctl --device=/dev/video0 --list-formats-ext
```

录制测试命令：

```bash
./bin/ipc_recorder \
  --device /dev/video0 \
  --record output/tc005_record_300frames.mp4 \
  --frames 300 \
  2>&1 | tee test_logs/tc005_record_300frames.log
```

生成文件检查：

```bash
ls -lh output/tc005_record_300frames.mp4
file output/tc005_record_300frames.mp4
ffprobe output/tc005_record_300frames.mp4
ffplay output/tc005_record_300frames.mp4
```

## 4. Expected Result

- `/dev/video0` 可以被枚举。
- `/dev/video0` 支持 YUYV 640x480 30fps。
- 程序使用 `/dev/video0` 采集。
- 程序生成 `output/tc005_record_300frames.mp4`。
- MP4 文件可被 `file` 识别为 MP4。
- MP4 文件可被 `ffprobe` 识别为 H264 / yuv420p / 640x480 / 30fps。
- MP4 文件可被 `ffplay` 播放。
- snapshot 旁路输出日志出现。

## 5. Actual Result

### 摄像头枚举结果

命令：

```bash
v4l2-ctl --list-devices
```

实际结果：

```text
Integrated Camera: Integrated C (usb-0000:80:14.0-11):
    /dev/video0
    /dev/video1
    /dev/media0
```

使用的采集节点：

```text
/dev/video0
```

### 摄像头格式查询结果

命令：

```bash
v4l2-ctl --device=/dev/video0 --list-formats-ext
```

关键结果：

- `/dev/video0` 支持 MJPG 和 YUYV。
- YUYV 支持 640x480 30fps。
- 当前程序使用 640x480 YUYV 30fps。

### AppConfig 关键日志

```text
device_path      : /dev/video0
width            : 640
height           : 480
fps              : 30
enable_record    : 1
output_path      : output/tc005_record_300frames.mp4
capture_name     : v4l2
converter_name   : yuyv_to_yuv420
encoder_name     : h264_ffmpeg
muxer_name       : mp4
max_frames       : 300
```

### V4L2 关键日志

```text
[v4l2] requested format: width=640 height=480 pixelformat=YUYV
[v4l2] actual format: width=640 height=480 pixelformat=YUYV
[v4l2] open /dev/video0 width=640 height=480 pixfmt=YUYV fps=30
```

### MP4 关键日志

```text
[mp4] open output/tc005_record_300frames.mp4
[mp4] write_header
[mp4] write_trailer
[mp4] close
```

### snapshot 关键日志

```text
[snapshot_jpeg] wrote edge_ai_camera_test/snapshot.jpg
```

### 生成文件检查

命令：

```bash
ls -lh output/tc005_record_300frames.mp4
```

实际结果：

```text
-rw-rw-r-- 1 ioe ioe 981K Jul 12 14:11 output/tc005_record_300frames.mp4
```

### file 检查

命令：

```bash
file output/tc005_record_300frames.mp4
```

实际结果：

```text
ISO Media, MP4 Base Media v1 [ISO 14496-12:2003]
```

### ffprobe 检查

命令：

```bash
ffprobe output/tc005_record_300frames.mp4
```

关键结果：

```text
Duration: 00:00:10.00
Video: h264 (Constrained Baseline), yuv420p, 640x480, 801 kb/s, 30 fps
```

### ffplay 检查

命令：

```bash
ffplay output/tc005_record_300frames.mp4
```

实际结果：

```text
视频可以播放。
```

## 6. Result

PASS

## 7. Notes

300 帧录制成功。由于 fps=30，输出视频时长为 10 秒，符合预期。

主链路验证通过：

```text
V4L2 -> YUYV422 -> YUV420P -> H264 -> MP4
```

`snapshot_jpeg_sink` 旁路也有输出日志，后续需要单独做 snapshot 文件测试。

已知 warning：

```text
[swscaler] deprecated pixel format used, make sure you did set range correctly
```

处理意见：

该 warning 当前不影响 MP4 录制和播放结果，先记录为已知 warning。后续可在 `snapshot_jpeg_sink` 中单独清理 pixel format / color range 设置。
