# SDL Preview 代码思路

## 1. Preview 是什么

Preview 是本地实时预览功能：

```text
MediaFrame(YUV420P)
  ↓
SDL_Texture
  ↓
SDL_Window
```

它用于在本机窗口中看到摄像头画面。

## 2. SDL viewer 的作用

相关代码：

```text
viewer/sdl_display_manager.c
viewer/sdl_display_manager.h
viewer/sdl_display_sdl.c
```

插件：

```text
g_sdl_display_ops
name = "sdl"
```

AppPipeline 中：

```text
if (enable_preview)
    ViewerManager_Display(&viewer, output_frame)
```

## 3. 输入数据

输入必须是：

```text
MediaFrame(YUV420P)
```

字段：

```text
data[0] = Y plane
data[1] = U plane
data[2] = V plane
linesize[0] = Y 每行字节数
linesize[1] = U 每行字节数
linesize[2] = V 每行字节数
width / height
```

## 4. SDL 核心对象

### SDL_Window

显示窗口。

当前在 SDL 显示线程中创建。

### SDL_Renderer

渲染器，用于把 texture 渲染到 window。

当前优先创建 accelerated renderer，失败后 fallback 到 software renderer。

### SDL_Texture

保存当前帧图像。

当前格式：

```text
SDL_PIXELFORMAT_IYUV
```

对应 YUV420P。

## 5. SDL_UpdateYUVTexture 怎么用

当前显示线程中：

```text
SDL_UpdateYUVTexture(texture,
                     NULL,
                     y_plane, width,
                     u_plane, width / 2,
                     v_plane, width / 2)
```

然后：

```text
SDL_RenderClear
SDL_RenderCopy
SDL_RenderPresent
```

## 6. display 函数为什么要 copy frame

`ViewerManager_Display()` 被 process thread 调用。

外部传入的 `MediaFrame` 生命周期不属于 SDL 线程。

所以 `sdl_display_display()` 会：

- `SDL_TryLockMutex`
- copy Y/U/V plane 到 SDL viewer 内部 buffer
- 设置 dirty
- 立即返回

SDL 显示线程再读取内部 buffer 更新 texture。

## 7. SDL_QUIT 如何处理

SDL 线程中会 `SDL_PollEvent()`。

如果收到：

```text
SDL_QUIT
```

会设置：

```text
ctx->running = 0
```

之后 `ViewerManager_Display()` 返回退出信号，AppPipeline 请求停止。

## 8. Preview 不负责什么

Preview 不负责：

- H264 编码
- MP4 保存
- RTSP 推流
- 摄像头采集
- 像素格式转换

它只显示已经转换好的 YUV420P frame。

## 9. Preview 和 RTSP 的区别

Preview：

```text
YUV420P -> SDL Window
```

RTSP：

```text
H264 -> RTSP Server -> Client
```

Preview 是本地调试和监控窗口；RTSP 是网络预览。

## 10. 当前限制

- 需要本地图形环境
- 没有窗口尺寸动态调整
- 没有多窗口
- 没有硬件 overlay
- 当前是初步可用的本地预览模块
