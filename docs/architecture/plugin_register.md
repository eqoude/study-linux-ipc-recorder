# 插件注册机制代码思路

## 1. Manager + Ops 是什么

项目用 C 语言模拟面向对象。

Manager 表示“对象实例”：

```text
CaptureManager
ConverterManager
FrameProcessorManager
ViewerManager
EncoderManager
MuxerManager
```

Ops 表示“虚函数表”：

```text
CaptureOps
ConverterOps
FrameProcessorOps
ViewerOps
EncoderOps
MuxerOps
```

典型结构：

```c
struct MuxerOps {
    const char *name;
    int (*init)(MuxerManager *manager);
    void (*deinit)(MuxerManager *manager);
    int (*open)(MuxerManager *manager);
    void (*close)(MuxerManager *manager);
    int (*write_header)(MuxerManager *manager);
    int (*write_packet)(MuxerManager *manager, const MediaPacket *packet);
    int (*write_trailer)(MuxerManager *manager);
};
```

Manager 保存：

- 当前选择的 `ops`
- 插件私有上下文 `priv`
- 配置 `config`
- 插件名

## 2. Plugin Register 是什么

Plugin Register 是一个静态注册表。

每个 manager 内部都有一个数组：

```text
g_capture_ops_table
g_converter_ops_table
g_encoder_ops_table
g_muxer_ops_table
g_viewer_ops_table
g_frame_processor_ops_table
```

注册时把 `Ops*` 放入表中。

查找时按 `ops->name` 匹配。

当前不是 `.so` 动态插件，不会运行时加载共享库。

## 3. `module_register.c` 做什么

文件：

```text
modules/module_register.c
```

它声明所有插件的全局 ops：

```c
extern const CaptureOps g_fake_capture_ops;
extern const CaptureOps g_v4l2_capture_ops;
extern const ConverterOps g_fake_converter_ops;
extern const ConverterOps g_yuyv_to_yuv420_ops;
extern const EncoderOps g_fake_encoder_ops;
extern const EncoderOps g_h264_ffmpeg_encoder_ops;
extern const FrameProcessorOps g_osd_processor_ops;
extern const MuxerOps g_fake_muxer_ops;
extern const MuxerOps g_mp4_muxer_ops;
extern const MuxerOps g_rtsp_muxer_ops;
extern const ViewerOps g_sdl_display_ops;
```

然后在 `RegisterAllModules()` 中统一注册。

## 4. 每个模块如何注册

### capture

```text
CaptureManager_Register(&g_fake_capture_ops)
CaptureManager_Register(&g_v4l2_capture_ops)
```

当前插件名：

```text
fake_capture
v4l2
```

### converter

```text
ConverterManager_Register(&g_fake_converter_ops)
ConverterManager_Register(&g_yuyv_to_yuv420_ops)
```

当前插件名：

```text
fake_converter
yuyv_to_yuv420
```

### encoder

```text
EncoderManager_Register(&g_fake_encoder_ops)
EncoderManager_Register(&g_h264_ffmpeg_encoder_ops)
```

当前插件名：

```text
fake_encoder
h264_ffmpeg
```

### muxer

```text
MuxerManager_Register(&g_fake_muxer_ops)
MuxerManager_Register(&g_mp4_muxer_ops)
MuxerManager_Register(&g_rtsp_muxer_ops)
```

当前插件名：

```text
fake_muxer
mp4
rtsp
```

### viewer

```text
RegisterViewer("sdl", &g_sdl_display_ops)
```

当前插件名：

```text
sdl
```

### processor

```text
FrameProcessorManager_Register(&g_osd_processor_ops)
```

当前插件名：

```text
osd
```

## 5. Init 流程如何找到插件

以 capture 为例：

```text
AppConfig.capture_name = "v4l2"
AppPipeline_Init
  CaptureManager_Init(&capture, "v4l2", &capture_config)
    CaptureManager_Find("v4l2")
      遍历 g_capture_ops_table
      找到 g_v4l2_capture_ops
    manager->ops = &g_v4l2_capture_ops
    manager->ops->init(manager)
```

其他模块流程相同。

## 6. 为什么 AppPipeline 不直接调用 `v4l2_capture.c`

如果 AppPipeline 直接调用具体实现：

```text
v4l2_open
v4l2_get_frame
mp4_write_packet
```

那么替换 fake/real 插件会很麻烦。

现在 AppPipeline 只调用：

```text
CaptureManager_GetFrame
ConverterManager_Convert
EncoderManager_Encode
MuxerManager_WritePacket
ViewerManager_Display
```

具体实现由插件名决定。

好处：

- main / AppPipeline 不依赖具体插件文件
- fake 插件可用于框架测试
- real 插件用于真实功能
- 后续可增加新 encoder / muxer / viewer

## 7. 当前是 static plugin registry

当前插件都是编译进同一个二进制：

```text
bin/ipc_recorder
```

`Makefile` 通过 wildcard 编译各目录 `.c` 文件。

当前没有：

- `dlopen`
- `.so` 插件
- 动态扫描目录
- 插件版本管理

## 8. 新增一个插件需要改哪些文件

以新增 `rkmpp_encoder` 为例：

1. 新增文件：

```text
encoder/rkmpp_encoder.c
```

2. 定义全局 ops：

```c
const EncoderOps g_rkmpp_encoder_ops = {
    .name = "rkmpp",
    ...
};
```

3. 在 `modules/module_register.c` 声明：

```c
extern const EncoderOps g_rkmpp_encoder_ops;
```

4. 在 `RegisterAllModules()` 注册：

```c
EncoderManager_Register(&g_rkmpp_encoder_ops);
```

5. 在配置中选择插件名：

```text
encoder_name = "rkmpp"
```

当前 CLI 还没有 `--encoder` 参数，默认 encoder 是 `h264_ffmpeg`。
