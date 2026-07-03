# YUYV422 到 YUV420P 转换代码思路

## 1. converter 模块作用

converter 位于 capture 后、processor / viewer / encoder 前：

```text
MediaFrame(YUYV422)
  ↓
MediaFrame(YUV420P)
```

相关代码：

```text
converter/converter_manager.c
converter/converter_manager.h
converter/yuyv_to_yuv420_converter.c
core/media_frame.h
```

## 2. YUYV422 是什么

YUYV422 是 packed format。

每两个像素排列为：

```text
Y0 U0 Y1 V0
```

含义：

- `Y0`：第 0 个像素亮度
- `Y1`：第 1 个像素亮度
- `U0 / V0`：这两个像素共享的色度

大小：

```text
每 2 个像素 4 字节
平均每像素 2 字节
linesize = width * 2
```

在 `MediaFrame` 中：

```text
data[0] = packed YUYV buffer
data[1] = NULL
data[2] = NULL
```

## 3. YUV420P 是什么

YUV420P 是 planar format。

三平面：

```text
data[0] = Y plane
data[1] = U plane
data[2] = V plane
```

大小：

```text
Y: width * height
U: width * height / 4
V: width * height / 4
total = width * height * 3 / 2
```

linesize：

```text
linesize[0] = width
linesize[1] = width / 2
linesize[2] = width / 2
```

## 4. 为什么 encoder / SDL 更适合 YUV420P

H264 encoder 常用输入是 `AV_PIX_FMT_YUV420P`。

SDL 也可以用 `SDL_PIXELFORMAT_IYUV` 直接显示 YUV420P：

```text
Y plane + U plane + V plane
```

所以项目把 V4L2 输出的 YUYV422 先转成 YUV420P，再送给：

- frame_processor
- viewer
- encoder

## 5. 当前 converter 怎么转换

插件：

```text
g_yuyv_to_yuv420_ops
name = "yuyv_to_yuv420"
```

初始化：

```text
yuyv_to_yuv420_init()
```

根据 `dst_width / dst_height` 分配一块 YUV420P buffer。

转换：

```text
yuyv_to_yuv420_convert()
```

### 5.1 提取 Y

YUYV422 中每两个像素：

```text
Y0 U0 Y1 V0
```

Y plane 需要每个像素一个 Y：

```text
Y0, Y1
```

所以第一层循环逐行提取所有 Y。

### 5.2 生成 U/V

YUV420P 的 U/V 分辨率是原图一半宽、一半高。

也就是说：

```text
2x2 像素共用一个 U 和一个 V
```

YUYV422 已经在水平方向 2 像素共享 U/V，但垂直方向没有降采样。

所以当前转换会每两行处理一次 U/V，并对上下两行做平均：

```text
U = (U_top + U_bottom) / 2
V = (V_top + V_bottom) / 2
```

这样从 4:2:2 转成 4:2:0。

## 6. 输出 MediaFrame 如何填写

输出：

```text
out->width = dst_width
out->height = dst_height
out->pixfmt = PIX_FMT_YUV420P
out->data[0] = Y plane
out->data[1] = U plane
out->data[2] = V plane
out->linesize[0] = width
out->linesize[1] = width / 2
out->linesize[2] = width / 2
out->size = width * height * 3 / 2
out->pts = src->pts
```

重点：

converter 不生成新时间戳，只透传 `src_frame->pts`。

## 7. linesize 是什么

`linesize` 表示每一行占多少字节。

不要简单假设所有格式都是 `width`：

- YUYV422: `linesize[0] = width * 2`
- YUV420P Y plane: `linesize[0] = width`
- YUV420P U/V plane: `linesize[1/2] = width / 2`

SDL 和 encoder 都依赖正确 linesize。

## 8. 常见错误

### 绿屏

常见原因：

- 摄像头实际输出不是 YUYV422
- U/V plane 填错
- U/V 顺序反了
- linesize 错误

### 黑屏

常见原因：

- Y plane 没填正确
- data 指针为空
- buffer 生命周期已失效
- SDL texture 更新失败

### linesize 错误

如果把 YUYV422 当成 `linesize = width`，会导致每行只读一半数据。

如果 YUV420P 的 U/V linesize 写成 `width`，会导致色度平面错位。

## 9. converter 不负责什么

converter 不负责：

- OSD
- zoom
- merge
- H264 编码
- MP4 封装
- RTSP 推流
- SDL 显示

这些由后续模块完成。
