# IPC Recorder 代码阅读顺序

这份文档按“从入口到模块”的顺序组织。目标是重新看懂当前工程，而不是背 API。

## 第一步：看 `app/main.c`

为什么先看：

`main.c` 是程序入口，可以看到程序启动后做了哪些大动作。

负责什么：

- 定义 `AppConfig app_config`
- 定义 `AppPipeline pipeline`
- 调用 `AppConfig_SetDefault()`
- 调用 `AppConfig_ParseArgs()`
- 检查至少启用 `--preview` / `--record` / `--rtsp` 之一
- 调用 `AppPipeline_Init()`
- 调用 `AppPipeline_Run()`
- 调用 `AppPipeline_Deinit()`

看懂后应该知道：

`main.c` 不处理 V4L2、FFmpeg、SDL 细节，只负责配置和 pipeline 生命周期。

## 第二步：看 `app/app_config.c` / `app/app_config.h`

为什么看：

运行模式由命令行决定，不先看配置就不知道 pipeline 会启用哪些模块。

负责什么：

- `AppConfig_SetDefault()` 设置默认值
- `AppConfig_ParseArgs()` 解析参数
- `AppConfig_Print()` 打印当前配置

当前支持参数：

- `--device`
- `--width`
- `--height`
- `--fps`
- `--preview`
- `--record`
- `--rtsp`
- `--processor`
- `--no-processor`
- `--frames`
- `--help`

看懂后应该知道：

`enable_preview` 决定是否初始化 viewer；`enable_record` 决定是否初始化 MP4 muxer；`enable_rtsp` 决定是否初始化 RTSP muxer；只要 record 或 RTSP 启用，就需要 encoder。

## 第三步：看 `app/app_pipeline.c` / `app/app_pipeline.h`

为什么看：

这是当前项目真正的调度中心。

负责什么：

- 保存所有 manager
- 保存 thread queue
- 创建 / join 线程
- 初始化模块
- 打开 capture / muxer
- 调用各模块 manager 接口
- 退出时按顺序释放资源

重点函数：

- `AppPipeline_Init()`
- `AppPipeline_Run()`
- `AppPipeline_Deinit()`
- `capture_thread_main()`
- `process_thread_main()`
- `encode_thread_main()`
- `mux_thread_main()`

看懂后应该知道：

当前 pipeline 已经不是单线程 while-loop，而是通过 `FrameQueue` 和 `PacketQueue` 解耦多个线程。

## 第四步：看 `modules/module_register.c`

为什么看：

AppPipeline 只写插件名，不直接 new 某个具体模块。插件名如何找到实现，要看注册表。

负责什么：

- `extern const xxxOps g_xxx_ops`
- `RegisterAllModules()`
- 调用各 manager 的 register 函数

看懂后应该知道：

`"v4l2"`、`"yuyv_to_yuv420"`、`"h264_ffmpeg"`、`"mp4"`、`"rtsp"`、`"sdl"`、`"osd"` 都是在这里注册进系统的。

## 第五步：看 `core/media_frame.h` 和 `core/media_packet.h`

为什么看：

所有模块之间传递的不是 `AVFrame` / `AVPacket`，而是项目自己的数据结构。

负责什么：

- `MediaFrame` 表示原始图像帧
- `MediaPacket` 表示编码后码流
- `MediaPacket_Alloc()` 分配 packet 数据
- `MediaPacket_CopyFromAVPacket()` 从 FFmpeg `AVPacket` deep copy H264 数据
- `MediaPacket_Unref()` 释放 packet 数据

看懂后应该知道：

capture / converter / processor / viewer 主要处理 `MediaFrame`；encoder / muxer 主要处理 `MediaPacket`。

## 第六步：看 `capture/`

为什么看：

capture 是数据源。

重点文件：

- `capture_manager.c`
- `capture_manager.h`
- `v4l2_capture.c`
- `fake_capture.c`

负责什么：

- `CaptureManager_Init()` 根据插件名找到 capture ops
- `v4l2_capture.c` 使用 V4L2 mmap 采集 YUYV422
- `get_frame` 输出 `MediaFrame(YUYV422)`
- `release_frame` 归还 V4L2 buffer

看懂后应该知道：

`MediaFrame.data[0]` 在 V4L2 capture 中指向 mmap buffer，必须通过 `CaptureManager_ReleaseFrame()` 归还。

## 第七步：看 `converter/`

为什么看：

encoder 和 SDL 更适合处理 YUV420P，而 V4L2 当前输出 YUYV422。

重点文件：

- `converter_manager.c`
- `yuyv_to_yuv420_converter.c`

负责什么：

- 输入 `MediaFrame(YUYV422)`
- 输出 `MediaFrame(YUV420P)`
- 填充 `data[0] = Y`、`data[1] = U`、`data[2] = V`

看懂后应该知道：

converter 只做格式转换，不做 OSD、不编码、不保存文件。

## 第八步：看 `encoder/`

为什么看：

encoder 是从原始图像到 H264 码流的边界。

重点文件：

- `encoder_manager.c`
- `h264_ffmpeg_encoder.c`

负责什么：

- `MediaFrame(YUV420P)` 包装成 `AVFrame`
- `avcodec_send_frame()`
- `avcodec_receive_packet()`
- 输出 `MediaPacket(H264)`
- deep copy packet 数据
- 保存 keyframe / SPS / PPS extradata 信息

看懂后应该知道：

不是每送一帧都一定立即输出 packet；flush 用来取出 encoder 内部剩余 packet。

## 第九步：看 `muxer/`

为什么看：

muxer 是编码数据的最终输出层。

重点文件：

- `muxer_manager.c`
- `mp4_muxer.c`
- `rtsp_muxer.c`
- `fake_muxer.c`

负责什么：

- MP4 muxer 把 `MediaPacket(H264)` 写成 MP4 文件
- RTSP muxer 把 `MediaPacket(H264)` 推到外部 RTSP Server
- muxer 不负责采集、不负责转换、不负责编码

看懂后应该知道：

MP4 和 RTSP 是两个 muxer 插件，共用 `MuxerManager` 接口。

## 第十步：看 `viewer/`

为什么看：

Preview 是一个显示旁路，不参与编码和封装。

重点文件：

- `sdl_display_manager.c`
- `sdl_display_sdl.c`

负责什么：

- 输入 `MediaFrame(YUV420P)`
- 拷贝到 SDL 内部 buffer
- 用 `SDL_UpdateYUVTexture()` 更新画面
- SDL 线程处理窗口事件

看懂后应该知道：

Preview 不等于 RTSP。Preview 是本地窗口显示，RTSP 是网络推流。

## 第十一步：看 `frame_processor/`

为什么看：

这是 converter 和 encoder 中间的图像处理层。

重点文件：

- `frame_processor_manager.c`
- `osd_processor.c`

负责什么：

- 输入 `MediaFrame(YUV420P)`
- 输出 `MediaFrame(YUV420P)`
- 当前 OSD 插件绘制简单白色区域

看懂后应该知道：

processor 处理的是未压缩图像，不应该依赖 FFmpeg，也不应该处理 H264 packet。

## 第十二步：看 `core/thread_queue.c`

为什么最后看：

先理解 pipeline 和数据结构，再看线程队列更容易。

负责什么：

- `FrameQueue` 跨线程传递 `MediaFrame`
- `PacketQueue` 跨线程传递 `MediaPacket`
- push 满时阻塞
- pop 空时阻塞
- close 时唤醒等待线程

看懂后应该知道：

thread_queue 的本质是解耦不同速度的模块，不是为了炫技。
