#define _DEFAULT_SOURCE

#include "capture_manager.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define V4L2_CAPTURE_BUFFER_COUNT 4

typedef struct {
    void *start;
    size_t length;
} V4L2CaptureBuffer;

typedef struct {
    int fd;
    int streaming;
    V4L2CaptureBuffer *buffers;
    unsigned int buffer_count;
    int64_t frame_index;
    int width;
    int height;
    PixelFormat pixel_format;
    unsigned int v4l2_pixelformat;
} V4L2CaptureContext;

static unsigned int PixelFormat_ToV4L2(PixelFormat fmt)
{
    switch (fmt) {
    case PIX_FMT_YUYV422:
        return V4L2_PIX_FMT_YUYV;
    default:
        return 0;
    }
}

static void v4l2_format_to_string(unsigned int pixelformat, char text[5])
{
    text[0] = (char)(pixelformat & 0xFFU);
    text[1] = (char)((pixelformat >> 8) & 0xFFU);
    text[2] = (char)((pixelformat >> 16) & 0xFFU);
    text[3] = (char)((pixelformat >> 24) & 0xFFU);
    text[4] = '\0';
}

static int v4l2_ioctl(int fd, unsigned long request, void *arg)
{
    int ret;

    do {
        ret = ioctl(fd, request, arg);
    } while (ret < 0 && errno == EINTR);

    return ret;
}

static int v4l2_capture_queue_buffer(V4L2CaptureContext *ctx, unsigned int index)
{
    struct v4l2_buffer buffer;

    if (ctx == NULL || index >= ctx->buffer_count) {
        return IPC_EINVAL;
    }

    memset(&buffer, 0, sizeof(buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;

    if (v4l2_ioctl(ctx->fd, VIDIOC_QBUF, &buffer) < 0) {
        perror("[v4l2] VIDIOC_QBUF");
        return IPC_EIO;
    }

    return IPC_OK;
}

static int v4l2_capture_init(CaptureManager *manager)
{
    V4L2CaptureContext *ctx;
    unsigned int v4l2_pixfmt;

    if (manager == NULL || manager->config.width <= 0 || manager->config.height <= 0) {
        return IPC_EINVAL;
    }

    v4l2_pixfmt = PixelFormat_ToV4L2(manager->config.pixel_format);
    if (v4l2_pixfmt == 0) {
        fprintf(stderr,
                "[v4l2] unsupported pixel format config: %d\n",
                manager->config.pixel_format);
        return IPC_EINVAL;
    }

    ctx = (V4L2CaptureContext *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return IPC_ENOMEM;
    }

    ctx->fd = -1;
    ctx->width = manager->config.width;
    ctx->height = manager->config.height;
    ctx->pixel_format = manager->config.pixel_format;
    ctx->v4l2_pixelformat = v4l2_pixfmt;
    manager->priv = ctx;
    printf("[v4l2] init\n");

    return IPC_OK;
}

static void v4l2_capture_deinit(CaptureManager *manager)
{
    V4L2CaptureContext *ctx;

    if (manager == NULL || manager->priv == NULL) {
        return;
    }

    ctx = (V4L2CaptureContext *)manager->priv;
    if (ctx->buffers != NULL) {
        for (unsigned int i = 0; i < ctx->buffer_count; ++i) {
            if (ctx->buffers[i].start != NULL && ctx->buffers[i].length > 0) {
                munmap(ctx->buffers[i].start, ctx->buffers[i].length);
            }
        }
        free(ctx->buffers);
    }

    if (ctx->fd >= 0) {
        close(ctx->fd);
    }

    free(ctx);
    manager->priv = NULL;
    printf("[v4l2] deinit\n");
}

static int v4l2_capture_open(CaptureManager *manager)
{
    V4L2CaptureContext *ctx;
    struct v4l2_format format;
    struct v4l2_requestbuffers request;
    char requested_fourcc[5];
    char actual_fourcc[5];

    if (manager == NULL) {
        return IPC_EINVAL;
    }
    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    ctx = (V4L2CaptureContext *)manager->priv;
    ctx->fd = open(manager->config.device_path, O_RDWR);
    if (ctx->fd < 0) {
        perror("[v4l2] open");
        return IPC_EOPEN;
    }

    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = (unsigned int)manager->config.width;
    format.fmt.pix.height = (unsigned int)manager->config.height;
    format.fmt.pix.pixelformat = ctx->v4l2_pixelformat;
    format.fmt.pix.field = V4L2_FIELD_NONE;

    if (v4l2_ioctl(ctx->fd, VIDIOC_S_FMT, &format) < 0) {
        perror("[v4l2] VIDIOC_S_FMT");
        return IPC_EIO;
    }

    v4l2_format_to_string(ctx->v4l2_pixelformat, requested_fourcc);
    v4l2_format_to_string(format.fmt.pix.pixelformat, actual_fourcc);

    printf("[v4l2] requested format: width=%d height=%d pixelformat=%s\n",
           manager->config.width,
           manager->config.height,
           requested_fourcc);
    printf("[v4l2] actual format: width=%u height=%u pixelformat=%s\n",
           format.fmt.pix.width,
           format.fmt.pix.height,
           actual_fourcc);

    if (format.fmt.pix.pixelformat != ctx->v4l2_pixelformat) {
        fprintf(stderr,
                "[v4l2] driver changed pixelformat: requested=%s actual=%s\n",
                requested_fourcc,
                actual_fourcc);
        return IPC_EINVAL;
    }

    ctx->width = (int)format.fmt.pix.width;
    ctx->height = (int)format.fmt.pix.height;
    ctx->v4l2_pixelformat = format.fmt.pix.pixelformat;

    memset(&request, 0, sizeof(request));
    request.count = V4L2_CAPTURE_BUFFER_COUNT;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;

    if (v4l2_ioctl(ctx->fd, VIDIOC_REQBUFS, &request) < 0) {
        perror("[v4l2] VIDIOC_REQBUFS");
        return IPC_EIO;
    }

    if (request.count < V4L2_CAPTURE_BUFFER_COUNT) {
        fprintf(stderr, "[v4l2] insufficient buffers: %u\n", request.count);
        return IPC_EIO;
    }

    ctx->buffers = (V4L2CaptureBuffer *)calloc(request.count, sizeof(*ctx->buffers));
    if (ctx->buffers == NULL) {
        return IPC_ENOMEM;
    }
    ctx->buffer_count = request.count;

    for (unsigned int i = 0; i < ctx->buffer_count; ++i) {
        struct v4l2_buffer buffer;

        memset(&buffer, 0, sizeof(buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (v4l2_ioctl(ctx->fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            perror("[v4l2] VIDIOC_QUERYBUF");
            return IPC_EIO;
        }

        ctx->buffers[i].length = buffer.length;
        ctx->buffers[i].start = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE,
                                     MAP_SHARED, ctx->fd, buffer.m.offset);
        if (ctx->buffers[i].start == MAP_FAILED) {
            ctx->buffers[i].start = NULL;
            perror("[v4l2] mmap");
            return IPC_EIO;
        }

        int ret = v4l2_capture_queue_buffer(ctx, i);
        if (ret != IPC_OK) {
            return ret;
        }
    }

    manager->state = CAPTURE_STATE_READY;
    printf("[v4l2] open %s\n", manager->config.device_path);

    return IPC_OK;
}

static void v4l2_capture_close(CaptureManager *manager)
{
    V4L2CaptureContext *ctx;

    if (manager == NULL || manager->priv == NULL) {
        return;
    }

    ctx = (V4L2CaptureContext *)manager->priv;
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }

    manager->state = CAPTURE_STATE_STOPPED;
    printf("[v4l2] close\n");
}

static int v4l2_capture_start(CaptureManager *manager)
{
    V4L2CaptureContext *ctx;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (manager == NULL) {
        return IPC_EINVAL;
    }
    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    ctx = (V4L2CaptureContext *)manager->priv;
    if (ctx->fd < 0) {
        return IPC_ESTATE;
    }

    if (v4l2_ioctl(ctx->fd, VIDIOC_STREAMON, &type) < 0) {
        perror("[v4l2] VIDIOC_STREAMON");
        return IPC_EIO;
    }

    ctx->frame_index = 0;
    ctx->streaming = 1;
    manager->state = CAPTURE_STATE_RUNNING;
    printf("[v4l2] start\n");

    return IPC_OK;
}

static void v4l2_capture_stop(CaptureManager *manager)
{
    V4L2CaptureContext *ctx;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (manager == NULL || manager->priv == NULL) {
        return;
    }

    ctx = (V4L2CaptureContext *)manager->priv;
    if (ctx->fd >= 0 && ctx->streaming) {
        if (v4l2_ioctl(ctx->fd, VIDIOC_STREAMOFF, &type) < 0) {
            perror("[v4l2] VIDIOC_STREAMOFF");
        }
        ctx->streaming = 0;
    }

    manager->state = CAPTURE_STATE_STOPPED;
    printf("[v4l2] stop\n");
}

static int v4l2_capture_get_frame(CaptureManager *manager, MediaFrame *frame)
{
    V4L2CaptureContext *ctx;
    struct v4l2_buffer buffer;

    if (manager == NULL || frame == NULL) {
        return IPC_EINVAL;
    }
    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    ctx = (V4L2CaptureContext *)manager->priv;
    if (ctx->fd < 0 || !ctx->streaming) {
        return IPC_ESTATE;
    }

    memset(&buffer, 0, sizeof(buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    if (v4l2_ioctl(ctx->fd, VIDIOC_DQBUF, &buffer) < 0) {
        if (errno == EAGAIN) {
            return IPC_EAGAIN;
        }
        perror("[v4l2] VIDIOC_DQBUF");
        return IPC_EIO;
    }

    if (buffer.index >= ctx->buffer_count) {
        return IPC_EIO;
    }

    memset(frame, 0, sizeof(*frame));
    frame->width = ctx->width;
    frame->height = ctx->height;
    frame->pixfmt = ctx->pixel_format;
    frame->data[0] = (unsigned char *)ctx->buffers[buffer.index].start;
    frame->linesize[0] = ctx->width * 2;
    frame->size = (int)buffer.bytesused;
    frame->pts = ctx->frame_index++;

    printf("[v4l2] get_frame index=%u size=%d\n", buffer.index, frame->size);

    return IPC_OK;
}

static int v4l2_capture_release_frame(CaptureManager *manager, MediaFrame *frame)
{
    V4L2CaptureContext *ctx;

    if (manager == NULL || frame == NULL || frame->data[0] == NULL) {
        return IPC_EINVAL;
    }
    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    ctx = (V4L2CaptureContext *)manager->priv;
    for (unsigned int i = 0; i < ctx->buffer_count; ++i) {
        if (ctx->buffers[i].start == frame->data[0]) {
            printf("[v4l2] release_frame index=%u\n", i);
            return v4l2_capture_queue_buffer(ctx, i);
        }
    }

    return IPC_EINVAL;
}

const CaptureOps g_v4l2_capture_ops = {
    .name = "v4l2",
    .init = v4l2_capture_init,
    .deinit = v4l2_capture_deinit,
    .open = v4l2_capture_open,
    .close = v4l2_capture_close,
    .start = v4l2_capture_start,
    .stop = v4l2_capture_stop,
    .get_frame = v4l2_capture_get_frame,
    .release_frame = v4l2_capture_release_frame,
};
