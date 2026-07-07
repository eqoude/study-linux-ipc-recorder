# YUYV422 到 YUV420P 转换代码思路

本文基于当前 IPC Recorder 代码实现整理，重点解释 `converter/yuyv_to_yuv420_converter.c` 中 `YUYV422 -> YUV420P` 的转换过程。

相关代码：

```text
converter/converter_manager.c
converter/converter_manager.h
converter/yuyv_to_yuv420_converter.c
core/media_frame.h
app/app_pipeline.c
modules/module_register.c
```

当前项目没有使用 OpenCV，也没有使用 FFmpeg swscale。转换逻辑是纯 C 手写实现。

## 1. Converter 模块定位

converter 位于 capture 后、encoder / viewer 前，是原始图像帧进入编码或显示前的格式转换层。

当前 pipeline 中的位置：

```text
Camera / V4L2
    ↓
capture_thread
    ↓
MediaFrame(YUYV422)
    ↓
FrameQueue(raw_queue)
    ↓
process_thread
    ↓
ConverterManager_Convert
    ↓
MediaFrame(YUV420P)
    ↓
frame_processor(optional)
    ↓
viewer / encode_queue
    ↓
encode_thread
```

converter 的职责：

- 完成像素格式转换。
- 将 `MediaFrame(YUYV422)` 转成 `MediaFrame(YUV420P)`。
- 保持 `pts` 不变。
- 输出 encoder / viewer 可使用的数据格式。

converter 不负责：

- H264 编码。
- MP4 封装。
- RTSP 推流。
- SDL 显示逻辑。
- OSD / watermark / zoom / merge 等图像内容处理。

这些工作分别由 encoder、muxer、viewer、frame_processor 完成。

## 2. 插件和 Manager 关系

converter 模块使用 Manager + Ops 模式。

当前真实转换插件：

```text
g_yuyv_to_yuv420_ops
name = "yuyv_to_yuv420"
```

注册入口：

```text
modules/module_register.c
```

`RegisterAllModules()` 中注册：

```text
ConverterManager_Register(&g_yuyv_to_yuv420_ops)
```

`AppPipeline` 只通过插件名初始化 converter：

```text
pipeline->config.converter_name = "yuyv_to_yuv420"
ConverterManager_Init(&pipeline->converter, pipeline->config.converter_name, &converter_config)
```

运行时在 `process_thread` 中调用：

```text
ConverterManager_Convert(&pipeline->converter, &raw_frame, &yuv420_frame)
```

`ConverterManager` 不关心具体转换算法，只负责参数检查和 ops 转调用。真正的转换逻辑在：

```text
converter/yuyv_to_yuv420_converter.c
```

## 3. YUYV422 格式说明

YUYV422 属于 packed format。

packed 的意思是：亮度和色度数据交错存放在同一个连续 buffer 中。

每两个像素排列为：

```text
Y0 U0 Y1 V0
```

含义：

- `Y0`：第 0 个像素的亮度。
- `Y1`：第 1 个像素的亮度。
- `U0 / V0`：这两个像素共享的色度。

每两个像素占用：

```text
4 bytes
```

平均每个像素：

```text
2 bytes / pixel
```

所以一行数据大小是：

```text
linesize[0] = width * 2
```

在 `MediaFrame` 中，YUYV422 表示为：

```text
pixfmt = PIX_FMT_YUYV422

data[0] = YUYV 连续 buffer
data[1] = NULL
data[2] = NULL

linesize[0] = width * 2
```

内存布局示意：

```text
data[0]
  |
  Y0 U0 Y1 V0  Y2 U2 Y3 V2  Y4 U4 Y5 V4 ...
```

## 4. YUV420P 格式说明

YUV420P 属于 planar format。

planar 的意思是：Y、U、V 分开存放在不同 plane 中。

在 `MediaFrame` 中：

```text
pixfmt = PIX_FMT_YUV420P

data[0] = Y plane
data[1] = U plane
data[2] = V plane
```

Y plane：

```text
width * height
```

U plane：

```text
width / 2 * height / 2
```

V plane：

```text
width / 2 * height / 2
```

总大小：

```text
width * height * 3 / 2
```

linesize：

```text
linesize[0] = width
linesize[1] = width / 2
linesize[2] = width / 2
```

内存布局示意：

```text
data[0]
  |
  Y Y Y Y Y Y Y Y ...

data[1]
  |
  U U U U ...

data[2]
  |
  V V V V ...
```

YUV420P 是当前项目中 encoder 和 SDL viewer 使用的主要原始图像格式。

## 5. YUYV422 和 YUV420P 对比

| 项目 | YUYV422 | YUV420P |
|---|---|---|
| 存储方式 | packed | planar |
| plane 数量 | 1 | 3 |
| Y | 每像素一个 | 每像素一个 |
| U/V 采样 | 水平方向 2:1 | 水平 + 垂直 2:1 |
| 平均字节/像素 | 2 | 1.5 |
| 常见用途 | 摄像头输出 | 编码输入 |

从 YUYV422 转 YUV420P，本质上做了两件事：

1. 把 packed 的 Y 数据拆出来，形成独立 Y plane。
2. 把 U/V 从 4:2:2 降采样成 4:2:0。

## 6. 当前转换插件初始化

初始化函数：

```text
yuyv_to_yuv420_init()
```

当前代码会检查：

- `manager` 是否为空。
- `src_width / src_height / dst_width / dst_height` 是否有效。
- 输入输出宽高是否一致。
- 输出宽高是否为偶数。
- `src_format == PIX_FMT_YUYV422`。
- `dst_format == PIX_FMT_YUV420P`。

然后分配一块 YUV420P 输出 buffer：

```text
size = width * height * 3 / 2
ctx->buffer = malloc(size)
```

这个 buffer 由 converter 插件持有，`yuyv_to_yuv420_deinit()` 中释放。

## 7. 转换算法总览

转换函数：

```text
yuyv_to_yuv420_convert()
```

输入：

```text
MediaFrame(YUYV422)
```

输出：

```text
MediaFrame(YUV420P)
```

核心步骤：

```text
Step 1: 提取 Y plane
Step 2: 生成 U / V plane
Step 3: 填写输出 MediaFrame
```

## 8. Step 1：提取 Y plane

YUYV422 每两个像素：

```text
Y0 U0 Y1 V0
```

YUV420P 的 Y plane 需要每个像素一个 Y：

```text
Y0 Y1
```

当前代码逐行处理：

```text
src_row = src + row * src_frame->linesize[0]
dst_y = y_plane + row * width
```

每两个像素读取：

```text
offset = col * 2
src_row[offset]      -> Y0
src_row[offset + 2]  -> Y1
```

写入：

```text
dst_y[col]     = src_row[offset]
dst_y[col + 1] = src_row[offset + 2]
```

也就是：

```text
输入: Y0 U0 Y1 V0
输出: Y0 Y1
```

这一步只提取亮度，不处理色度。

## 9. Step 2：生成 U / V plane

YUV420P 的 U/V 是 4:2:0 采样：

```text
2x2 像素共享一个 U 和一个 V
```

而 YUYV422 是 4:2:2 采样：

```text
水平方向 2 个像素共享一个 U/V
垂直方向每一行都有 U/V
```

所以从 YUYV422 转到 YUV420P 时，需要对垂直方向做降采样。

当前代码每两行处理一次：

```text
row = 0, 2, 4, ...
```

取上面一行：

```text
src_row0 = src + row * src_frame->linesize[0]
```

取下面一行：

```text
src_row1 = src + (row + 1) * src_frame->linesize[0]
```

对于每两个像素：

```text
Y0 U_top Y1 V_top
Y2 U_bottom Y3 V_bottom
```

计算：

```text
U = (U_top + U_bottom) / 2
V = (V_top + V_bottom) / 2
```

对应代码逻辑：

```text
u = (src_row0[offset + 1] + src_row1[offset + 1]) / 2
v = (src_row0[offset + 3] + src_row1[offset + 3]) / 2

dst_u[col / 2] = u
dst_v[col / 2] = v
```

这是一种简单平均降采样方法，足够满足当前 IPC Recorder 工程原型。

## 10. 2x2 像素转换示例

输入 YUYV422：

```text
第一行:
Y0 U0 Y1 V0

第二行:
Y2 U2 Y3 V2
```

输出 Y plane：

```text
Y0 Y1
Y2 Y3
```

输出 U plane：

```text
(U0 + U2) / 2
```

输出 V plane：

```text
(V0 + V2) / 2
```

这个例子说明：

- Y 仍然每个像素保留一个。
- U/V 从每行都有，变成两行共享。
- 这就是 4:2:2 到 4:2:0 的核心变化。

## 11. 输出 MediaFrame 如何填写

转换成功后，当前代码填写：

```text
dst_frame->width = width
dst_frame->height = height
dst_frame->pixfmt = PIX_FMT_YUV420P

dst_frame->data[0] = y_plane
dst_frame->data[1] = u_plane
dst_frame->data[2] = v_plane

dst_frame->linesize[0] = width
dst_frame->linesize[1] = width / 2
dst_frame->linesize[2] = width / 2

dst_frame->size = width * height * 3 / 2
dst_frame->pts = src_frame->pts
```

重点：

```text
pts 不重新生成
```

converter 只转换像素数据，不改变时间轴。时间戳由 capture 产生，经 converter 原样传递给后续 frame_processor、viewer、encoder。

## 12. linesize 说明

`linesize` 不是图片宽度。

`linesize` 表示一行数据占用多少字节。

YUYV422：

```text
linesize[0] = width * 2
```

原因：

```text
YUYV422 每像素平均 2 字节
```

YUV420P：

```text
linesize[0] = width
linesize[1] = width / 2
linesize[2] = width / 2
```

原因：

- Y plane 每个像素一个 Y。
- U/V plane 宽度是原图一半。

错误 linesize 会导致：

- 花屏。
- 绿屏。
- 色彩异常。
- encoder 读取错误。
- SDL 显示错位。

在当前转换代码里，输入使用：

```text
src_frame->linesize[0]
```

这样即使 capture 每行 stride 不完全等于 `width * 2`，converter 也按输入 frame 描述来找每一行的起点。

输出是 converter 自己分配的紧凑 buffer，所以 linesize 固定为：

```text
Y: width
U: width / 2
V: width / 2
```

## 13. 内存布局图

### YUYV422

```text
data[0]
  |
  Y0 U0 Y1 V0  Y2 U2 Y3 V2  Y4 U4 Y5 V4 ...
```

### YUV420P

```text
data[0]
  |
  Y Y Y Y Y Y Y Y ...

data[1]
  |
  U U U U ...

data[2]
  |
  V V V V ...
```

## 14. 常见问题分析

### 14.1 绿屏

常见原因：

- Y/U/V plane 顺序错误。
- U/V 大小计算错误。
- U/V 写入位置错误。
- `pixfmt` 写错，例如实际不是 `PIX_FMT_YUV420P` 却按 YUV420P 送给 encoder 或 SDL。
- 摄像头实际输出格式和 `MediaFrame.pixfmt` 不一致。

### 14.2 黑屏

常见原因：

- `data[0]` 指针无效。
- buffer 生命周期结束。
- Y plane 没有正确填充。
- converter 输入 frame 已经被释放。
- capture buffer 没有被正确 copy 到 `FrameQueue`。

### 14.3 花屏

常见原因：

- `linesize` 错误。
- stride 计算错误。
- 按 `width` 读取 YUYV422 输入，实际应该按 `width * 2` 或 `src_frame->linesize[0]`。
- U/V plane 起始地址计算错误。
- 输出 buffer size 不够。

## 15. converter 和其他模块的边界

converter 只负责格式转换。

它不负责：

- 编码：由 `encoder/h264_ffmpeg_encoder.c` 完成。
- 封装：由 `muxer/mp4_muxer.c` 或 `muxer/rtsp_muxer.c` 完成。
- 推流：由 RTSP muxer 完成。
- 显示：由 `viewer/sdl_display_sdl.c` 完成。
- OSD：由 `frame_processor/osd_processor.c` 完成。

这种边界让 pipeline 更清晰：

```text
capture_thread
  -> MediaFrame(YUYV422)

process_thread
  -> ConverterManager_Convert
  -> MediaFrame(YUV420P)

encode_thread
  -> EncoderManager_Encode
  -> MediaPacket(H264)
```

面试时可以这样概括：

> converter 是原始帧格式适配层。V4L2 采集到的是 `MediaFrame(YUYV422)`，但 H264 encoder 和 SDL viewer 更适合使用 `YUV420P`，所以我在 `process_thread` 中通过 `ConverterManager` 调用 `yuyv_to_yuv420_converter`，用纯 C 把 packed 的 YUYV422 拆成 planar 的 YUV420P，并保持 `pts` 不变，保证后续编码、显示和时间轴都稳定。
