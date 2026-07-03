# IPC Recorder 后续路线图

当前项目已经是可运行的 engineering prototype。下面是后续建议，不代表当前已经完成。

## 1. V4L2 Capture 增强

- 增加 `VIDIOC_QUERYCAP`
- 增加 `VIDIOC_G_FMT`
- 增加 `VIDIOC_S_PARM`
- 回读实际 width / height / pixfmt / fps
- 完善错误路径资源释放

## 2. 时间戳与同步

- 梳理 capture pts、encoder pts、muxer pts 的长期策略
- 为未来 audio stream 预留统一 clock
- 增加 packet duration 字段
- 增加音视频同步测试

## 3. 性能优化

- 评估 `FrameQueue` deep copy 成本
- 增加丢帧模式
- 增加 queue latency 统计
- 减少逐帧日志
- 增加 fps 统计

## 4. Encoder 扩展

- 增加硬件编码器插件，例如 RKMpp / V4L2 M2M
- 支持码率、GOP、profile 命令行配置
- 支持动态码率调整

## 5. Muxer / Streaming 扩展

当前已有：

- `mp4_muxer.c`
- `rtsp_muxer.c`
- `fake_muxer.c`

后续可考虑：

- RTSP 断线重连
- RTSP 鉴权
- 音频 stream
- HLS / FLV / WebRTC 输出

当前没有 `raw_h264_muxer.c`，不要在文档或 Makefile 中引用不存在的 raw_h264 模块。

## 6. Preview 增强

- 窗口尺寸动态调整
- 显示 fps
- 显示 OSD 调试信息
- 支持无 GUI 环境禁用 viewer

## 7. 工程化

- 引入日志等级
- 引入统一错误码
- 增加自动化测试脚本
- 增加 valgrind 检查流程
- 增加 CI 编译检查

## 8. 当前不建议马上做

- 一次性重构全部模块
- 在当前进程内实现完整 RTSP Server
- 未完成 V4L2 能力检测前盲目支持多摄像头格式
- 在没有性能数据前过早优化 queue
