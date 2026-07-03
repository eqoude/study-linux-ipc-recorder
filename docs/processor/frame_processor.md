# Frame Processor 图像处理模块代码思路

## 1. frame_processor 放在哪里

位置：

```text
capture
  ↓
converter
  ↓
frame_processor
  ↓
encoder
  ↓
muxer
```

Preview 也可以使用 processor 后的 frame：

```text
MediaFrame(YUV420P processed)
  ├── viewer
  └── encoder
```

## 2. 输入输出

输入：

```text
MediaFrame(YUV420P)
```

输出：

```text
MediaFrame(YUV420P)
```

processor 不改变像素格式。

## 3. 为什么放在 converter 后、encoder 前

capture 当前输出 YUYV422，是 packed format，不适合直接做 OSD / overlay。

converter 后得到 YUV420P：

- Y plane 表示亮度
- U/V plane 表示颜色
- 更适合做像素级处理

encoder 后已经是 H264 压缩码流，不能直接画字或拼接。

## 4. 当前 osd_processor 做什么

相关代码：

```text
frame_processor/osd_processor.c
```

插件：

```text
g_osd_processor_ops
name = "osd"
```

当前功能是初步 OSD：

- 分配内部 YUV420P buffer
- copy 输入 frame 到内部 buffer
- 在指定区域绘制简单白色矩形
- 输出 processed frame

它不是完整文字渲染系统，没有字体库。

## 5. Manager 调用流程

AppPipeline 中：

```text
if (enable_processor) {
    FrameProcessorManager_Process(&processor,
                                  &yuv420_frame,
                                  &processed_frame)
}
```

Manager 内部：

```text
FrameProcessorManager_Find("osd")
manager->ops = g_osd_processor_ops
manager->ops->process(...)
```

## 6. 为什么 processor 不应该依赖 FFmpeg

processor 处理的是原始图像像素，不是编码码流。

如果 processor 依赖 FFmpeg `AVFrame`：

- capture / converter 会被 FFmpeg 绑死
- 后续替换 encoder 或 muxer 会更困难
- 模块边界变模糊

当前用 `MediaFrame` 保持模块解耦。

## 7. 为什么 processor 不修改其他模块

processor 是独立模块，不应该改：

- capture
- converter
- encoder
- muxer
- viewer

它只在 pipeline 中作为一层可选处理。

## 8. 后续扩展方向

可以新增插件：

- `watermark_processor`
- `zoom_processor`
- `merge_processor`
- `grid_processor`
- `roi_processor`

扩展方式：

1. 新增 `.c` 文件
2. 定义 `FrameProcessorOps`
3. 在 `module_register.c` 注册
4. 通过配置选择插件名

## 9. 和 converter 的区别

converter：

```text
YUYV422 -> YUV420P
```

负责格式转换。

processor：

```text
YUV420P -> YUV420P
```

负责图像内容处理。

## 10. 和 encoder 的区别

processor 处理原始图像。

encoder 处理压缩编码：

```text
MediaFrame(YUV420P) -> MediaPacket(H264)
```

所以 processor 必须在 encoder 前面。
