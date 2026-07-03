# thread_queue 代码思路

## 1. 为什么需要 thread_queue

IPC Recorder 的各阶段速度不一样：

- 摄像头按固定 fps 产生 frame
- converter 消耗 CPU
- encoder 可能因为内部缓存、B 帧、码率控制产生波动
- muxer 受磁盘或网络影响
- viewer 受窗口刷新影响

如果所有模块都在一个 while-loop 里同步执行，一个慢模块会直接卡住前面的采集。

`thread_queue` 的目标是把不同速度的模块解耦。

## 2. 单线程 pipeline 的问题

单线程模型：

```text
get_frame
convert
process
display
encode
mux
release / next frame
```

问题：

- muxer 写文件慢，会影响 capture。
- RTSP 网络慢，会影响 capture。
- encoder 短时间阻塞，会影响 preview。
- 无法形成背压边界。

## 3. 多线程 pipeline 的目标

当前 AppPipeline 的目标：

```text
capture thread
   ↓ FrameQueue
process thread
   ↓ FrameQueue
encode thread
   ↓ PacketQueue
mux thread
```

核心思想：

```text
队列连接不同线程
队列满时阻塞上游
队列空时阻塞下游
退出时 close queue 唤醒等待线程
```

thread_queue 的本质是“解耦不同速度的模块”，不是为了炫技。

## 4. queue 在 capture / encode / mux 之间的作用

当前有三个队列：

```text
raw_queue:    capture_thread -> process_thread
encode_queue: process_thread -> encode_thread
packet_queue: encode_thread -> mux_thread
```

图：

```text
capture_thread
  CaptureManager_GetFrame
  FrameQueue_Push(raw_queue)
  CaptureManager_ReleaseFrame
      ↓
process_thread
  FrameQueue_Pop(raw_queue)
  ConverterManager_Convert
  FrameProcessorManager_Process(optional)
  ViewerManager_Display(optional)
  FrameQueue_Push(encode_queue)
      ↓
encode_thread
  FrameQueue_Pop(encode_queue)
  EncoderManager_Encode
  PacketQueue_Push(packet_queue)
      ↓
mux_thread
  PacketQueue_Pop(packet_queue)
  MuxerManager_WritePacket(mp4 / rtsp)
```

## 5. FrameQueue 是什么

定义在：

```text
core/thread_queue.h
```

用途：

```text
MediaFrame queue
```

关键函数：

- `FrameQueue_Init()`
- `FrameQueue_Push()`
- `FrameQueue_Pop()`
- `FrameQueue_Close()`
- `FrameQueue_Deinit()`
- `FrameQueue_UnrefFrame()`

当前 `FrameQueue_Push()` 会 deep copy frame 数据。

原因：

V4L2 `MediaFrame.data[0]` 可能指向 mmap buffer。capture thread push 完后会 `CaptureManager_ReleaseFrame()` 归还 buffer，如果 queue 不拷贝，下游就会读到已归还的 buffer。

## 6. PacketQueue 是什么

定义在：

```text
core/thread_queue.h
```

用途：

```text
MediaPacket queue
```

关键函数：

- `PacketQueue_Init()`
- `PacketQueue_Push()`
- `PacketQueue_Pop()`
- `PacketQueue_Close()`
- `PacketQueue_Deinit()`

当前 `PacketQueue_Push()` 会 deep copy H264 packet 数据。

注意：

`PacketQueue` 当前 deep copy `packet->data`，但 `extradata` 是只读引用，不在 `PacketQueue` 中再次 deep copy。它依赖 encoder 生命周期覆盖 muxer 生命周期。

## 7. push / pop / blocking pop 是什么

### push

`FrameQueue_Push()` / `PacketQueue_Push()`：

- 先 clone 数据
- 加锁
- 如果 queue 满，等待 `not_full`
- 如果 queue 已 close，释放 clone 并返回失败
- 写入 ring buffer
- signal `not_empty`

### pop

`FrameQueue_Pop()` / `PacketQueue_Pop()`：

- 加锁
- 如果 queue 空且未关闭，等待 `not_empty`
- 如果 queue 空且已关闭，返回 `1` 表示结束
- 取出一个 item
- signal `not_full`

## 8. queue 满了怎么办

当前实现：

```text
while (!closed && count >= capacity)
    pthread_cond_wait(not_full)
```

也就是阻塞上游线程，形成背压。

这可以防止内存无限增长，但也意味着慢速 muxer / encoder 会逐步影响前面的线程。

## 9. queue 空了怎么办

当前实现：

```text
while (!closed && count == 0)
    pthread_cond_wait(not_empty)
```

下游线程睡眠等待，不 busy loop。

## 10. stop / shutdown 怎么做

`AppPipeline` 中的 `app_pipeline_request_stop()` 会：

```text
pipeline->stop = 1
FrameQueue_Close(raw_queue)
FrameQueue_Close(encode_queue)
PacketQueue_Close(packet_queue)
```

`Close()` 会 broadcast 条件变量，让正在 push / pop 阻塞的线程醒来。

`AppPipeline_Deinit()` 再 join 线程，并释放 queue 和各模块。

## 11. 当前 thread_queue.c 实际实现了什么

已经实现：

- 固定容量 ring buffer
- mutex + condition variable
- blocking push
- blocking pop
- close 唤醒
- FrameQueue deep copy
- PacketQueue deep copy packet data
- Deinit 清理残留 item

没有实现：

- 丢帧模式
- 超时 push / pop
- 统计队列延迟
- 动态扩容
- lock-free queue

## 12. 当前项目是否真正拆成线程

是的，当前 `app/app_pipeline.c` 已经创建线程：

- `pthread_create(... mux_thread_main ...)`
- `pthread_create(... encode_thread_main ...)`
- `pthread_create(... process_thread_main ...)`
- `pthread_create(... capture_thread_main ...)`

但这仍是工程原型：

- viewer 自己内部还有 SDL 线程
- 没有独立音频线程
- 没有线程优先级设置
- 没有性能监控
