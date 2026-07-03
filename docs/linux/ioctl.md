# ioctl 与 v4l2_ioctl 封装

## 什么是 ioctl

在 Linux 中，普通文件通常通过以下系统调用完成操作：

```c
read();
write();
close();
```

但是对于设备驱动来说，仅仅依靠 `read()` 和 `write()` 无法完成所有控制功能。

例如摄像头设备需要完成：

* 查询设备能力
* 设置图像格式
* 设置分辨率
* 申请缓冲区
* 启动视频流
* 停止视频流
* 获取图像帧

这些控制类操作统一通过 `ioctl()` 完成。

函数原型如下：

```c
int ioctl(int fd, unsigned long request, void *arg);
```

其中：

| 参数      | 含义         |
| ------- | ---------- |
| fd      | 设备文件描述符    |
| request | 控制命令       |
| arg     | 命令对应的参数结构体 |

例如：

```c
struct v4l2_capability cap;

ioctl(fd, VIDIOC_QUERYCAP, &cap);
```

表示向 V4L2 驱动发送：

```text
VIDIOC_QUERYCAP
```

命令，用于查询设备能力。

---

## V4L2 中常见的 ioctl 命令

V4L2 几乎所有控制操作都通过 ioctl 完成。

常见命令如下：

### 查询设备能力

```c
VIDIOC_QUERYCAP
```

查看设备是否支持：

* Video Capture
* Streaming I/O
* Read/Write I/O

等能力。

---

### 设置图像格式

```c
VIDIOC_S_FMT
```

用于设置：

```text
宽度
高度
像素格式
```

例如：

```text
640x480
YUYV422
```

---

### 申请缓冲区

```c
VIDIOC_REQBUFS
```

向驱动申请 Buffer。

通常申请：

```text
4 个 Buffer
```

用于循环采集。

---

### 查询 Buffer 信息

```c
VIDIOC_QUERYBUF
```

获取 Buffer 大小和偏移地址。

后续用于：

```c
mmap()
```

映射到用户空间。

---

### Buffer 入队

```c
VIDIOC_QBUF
```

把 Buffer 放回驱动队列。

表示：

```text
这个 Buffer 可以继续采集下一帧
```

---

### Buffer 出队

```c
VIDIOC_DQBUF
```

从驱动队列取出已经填充好图像数据的 Buffer。

表示：

```text
拿到一帧图像
```

---

### 开启采集

```c
VIDIOC_STREAMON
```

启动视频流。

---

### 停止采集

```c
VIDIOC_STREAMOFF
```

停止视频流。

---

# 为什么封装 v4l2_ioctl

项目中没有直接使用：

```c
ioctl(fd, request, arg);
```

而是封装了一层：

```c
static int v4l2_ioctl(int fd,
                      unsigned long request,
                      void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret < 0 && errno == EINTR);

    return ret;
}
```

主要原因是：

```text
ioctl 可能被信号中断
```

---

## 什么是 EINTR

Linux 系统调用在执行过程中可能收到信号。

例如：

```text
SIGINT
SIGALRM
SIGCHLD
```

此时系统调用可能提前返回：

```c
ret = -1;
errno = EINTR;
```

其中：

```text
EINTR
=
Interrupted System Call
```

含义是：

```text
系统调用被信号打断
```

注意：

```text
这并不代表设备错误
```

也不代表：

```text
驱动出现异常
```

只是当前系统调用尚未完成。

---

## 为什么要自动重试

例如：

```c
ioctl(fd, VIDIOC_DQBUF, &buf);
```

正在等待摄像头产生下一帧。

此时收到：

```text
SIGCHLD
```

系统调用返回：

```c
errno = EINTR;
```

如果直接返回失败：

```c
if (ioctl(...) < 0)
{
    return -1;
}
```

程序会误认为：

```text
摄像头采集失败
```

实际上只是：

```text
系统调用被打断
```

因此正确做法是：

```text
重新执行 ioctl
```

直到：

* 调用成功
* 出现真正错误

---

## v4l2_ioctl 的执行流程

```text
调用 ioctl
      │
      ▼

返回成功？
      │
 ┌────┴────┐
 │         │
是        否
 │         │
 ▼         ▼

返回      errno==EINTR?
               │
          ┌────┴────┐
          │         │
         是        否
          │         │
          ▼         ▼

      再次调用    返回错误
```

---

## 本项目中的作用

项目中的所有 V4L2 控制操作统一通过：

```c
v4l2_ioctl()
```

完成。

例如：

```c
v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap);
v4l2_ioctl(fd, VIDIOC_S_FMT, &fmt);
v4l2_ioctl(fd, VIDIOC_REQBUFS, &req);
v4l2_ioctl(fd, VIDIOC_QBUF, &buf);
v4l2_ioctl(fd, VIDIOC_DQBUF, &buf);
v4l2_ioctl(fd, VIDIOC_STREAMON, &type);
v4l2_ioctl(fd, VIDIOC_STREAMOFF, &type);
```

这样可以：

* 统一错误处理
* 避免重复代码
* 自动处理 EINTR
* 提高程序健壮性

---

# 总结

`v4l2_ioctl()` 本质上是：

```text
带 EINTR 自动重试机制的 ioctl 封装
```

它不是 V4L2 独有设计，而是 Linux 系统编程中常见的工程化写法。

对于 Camera、V4L2、驱动开发等场景，这种封装能够避免把“信号中断”误判成“设备故障”，提高程序稳定性。
