# V4L2 Capture 代码思路

## 1. capture 模块作用

capture 是整个 pipeline 的数据源：

```text
/dev/video0
  ↓
V4L2 mmap
  ↓
MediaFrame(YUYV422)
```

相关代码：

```text
capture/capture_manager.c
capture/capture_manager.h
capture/v4l2_capture.c
core/media_frame.h
```

## 2. Manager + Ops 调用关系

AppPipeline 不直接调用 `v4l2_capture.c`，而是调用：

```text
CaptureManager_Init
CaptureManager_Open
CaptureManager_Start
CaptureManager_GetFrame
CaptureManager_ReleaseFrame
CaptureManager_Stop
CaptureManager_Close
CaptureManager_Deinit
```

这些接口最终转调 `g_v4l2_capture_ops` 中的函数。

## 3. `v4l2_capture.c` 当前完整流程

### 3.1 init

`v4l2_capture_init()`：

- 检查 `CaptureManager` 和 `CaptureConfig`
- 保存 `device_path / width / height / pixel_format / fps`
- 分配私有 `V4L2CaptureContext`

### 3.2 open

`v4l2_capture_open()` 当前做：

```text
open(device_path, O_RDWR)
VIDIOC_S_FMT
VIDIOC_REQBUFS
VIDIOC_QUERYBUF
mmap
VIDIOC_QBUF
```

当前没有实现：

- `VIDIOC_QUERYCAP`
- `VIDIOC_G_FMT`
- `VIDIOC_S_PARM`

也就是说，当前代码直接尝试设置格式，没有先完整查询设备能力。

### 3.3 VIDIOC_QUERYCAP 是什么

`VIDIOC_QUERYCAP` 用于查询设备能力，例如：

- 是否是 video capture 设备
- 是否支持 streaming
- driver/card/bus 信息

当前代码未实现这一项。后续应在 `open` 后、设置格式前增加。

### 3.4 VIDIOC_S_FMT 是什么

`VIDIOC_S_FMT` 用于设置采集格式。

当前设置：

```text
type = V4L2_BUF_TYPE_VIDEO_CAPTURE
width = manager->config.width
height = manager->config.height
pixelformat = V4L2_PIX_FMT_YUYV
field = V4L2_FIELD_NONE
```

目标输出：

```text
YUYV422
```

### 3.5 VIDIOC_G_FMT 是什么

`VIDIOC_G_FMT` 用于读取驱动最终接受的格式。

有些摄像头不一定接受用户请求的 width / height / pixfmt，驱动可能调整为其他值。

当前代码未实现 `VIDIOC_G_FMT`，所以默认认为设置成功后实际格式就是请求格式。

### 3.6 VIDIOC_S_PARM 是什么

`VIDIOC_S_PARM` 用于设置帧率。

当前 `CaptureConfig.fps` 会保存到上下文，但 `v4l2_capture.c` 未调用 `VIDIOC_S_PARM` 真正设置设备 fps。

这意味着实际摄像头 fps 可能和 AppConfig 不完全一致。

### 3.7 VIDIOC_REQBUFS

`VIDIOC_REQBUFS` 申请内核 buffer。

当前请求：

```text
count = 4
memory = V4L2_MEMORY_MMAP
```

含义：

让驱动准备 4 个可 mmap 的采集 buffer。

### 3.8 VIDIOC_QUERYBUF

`VIDIOC_QUERYBUF` 查询每个 buffer 的 offset 和 length。

当前代码遍历 buffer index：

```text
for each buffer:
  VIDIOC_QUERYBUF
  mmap
```

### 3.9 mmap

mmap 把内核 buffer 映射到用户态地址空间。

好处：

- 避免每帧 read/copy
- 用户态可以直接访问驱动填好的 buffer
- 适合视频流高频采集

当前保存：

```text
ctx->buffers[i].start
ctx->buffers[i].length
```

### 3.10 VIDIOC_QBUF

`VIDIOC_QBUF` 把空 buffer 放入驱动采集队列。

open 阶段 mmap 完后，会把所有 buffer 先 QBUF 给驱动。

### 3.11 VIDIOC_STREAMON

`v4l2_capture_start()` 调用：

```text
VIDIOC_STREAMON
```

表示正式开始采集。

### 3.12 VIDIOC_DQBUF

`v4l2_capture_get_frame()` 调用：

```text
VIDIOC_DQBUF
```

含义：

从驱动队列取出一个已经填好图像数据的 buffer。

## 4. mmap buffer 怎么变成 MediaFrame

`VIDIOC_DQBUF` 返回 `buffer.index` 后：

```text
frame->width = ctx->width
frame->height = ctx->height
frame->pixfmt = PIX_FMT_YUYV422
frame->data[0] = ctx->buffers[buffer.index].start
frame->linesize[0] = width * 2
frame->size = buffer.bytesused
frame->pts = ctx->frame_index++
```

为什么只有 `data[0]`：

YUYV422 是 packed format，Y/U/V 混在同一个连续 buffer 中，不是三平面格式。

为什么 `linesize[0] = width * 2`：

YUYV422 每个像素平均 2 字节。

## 5. buffer.index 的作用

`buffer.index` 表示这帧来自第几个 V4L2 buffer。

release 时必须用同一个 index QBUF 回去。

当前 `MediaFrame` 结构没有单独保存 `buffer.index` 字段。

当前代码的做法是：

```text
get_frame:
  frame->data[0] = ctx->buffers[buffer.index].start

release_frame:
  遍历 ctx->buffers
  找到 start == frame->data[0] 的 buffer
  用对应 i 调用 VIDIOC_QBUF
```

也就是说，当前通过 `data[0]` 地址反查 index。

## 6. bytesused 的作用

`buffer.bytesused` 是这次采集实际写入的有效字节数。

当前填到：

```text
frame->size
```

对于 YUYV422，理论大小通常是：

```text
width * height * 2
```

但以驱动返回的 `bytesused` 为准更安全。

## 7. release_frame 为什么必须 QBUF

`CaptureManager_ReleaseFrame()` 最终调用 `v4l2_capture_release_frame()`。

核心动作：

```text
VIDIOC_QBUF
```

它把 DQBUF 取出的 buffer 重新放回驱动队列。

如果不 release：

- buffer 会被用户态一直占用
- 驱动可用 buffer 变少
- 采集最终阻塞

## 8. stop / close / deinit

### STREAMOFF

`v4l2_capture_stop()` 调用：

```text
VIDIOC_STREAMOFF
```

停止视频流。

### close

`v4l2_capture_close()` 关闭 fd。

### deinit

`v4l2_capture_deinit()`：

- `munmap` 已映射 buffer
- free buffer 数组
- free 私有上下文

## 9. 当前待完善点

当前未实现或不完整：

- `VIDIOC_QUERYCAP`
- `VIDIOC_G_FMT`
- `VIDIOC_S_PARM`
- 对实际摄像头格式/fps 的回读校验
- 更完整的错误路径资源回收

所以当前 V4L2 capture 是可用原型，但不是完整工业级采集层。
