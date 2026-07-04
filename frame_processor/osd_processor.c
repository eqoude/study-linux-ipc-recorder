#include "frame_processor_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *buffer;
    int buffer_size;
} OsdProcessorContext;

static void osd_draw_rect_yuv420(MediaFrame *frame,
                                 int x,
                                 int y,
                                 int width,
                                 int height)
{
    int end_x;
    int end_y;

    if (frame == NULL || frame->pixfmt != PIX_FMT_YUV420P ||
        frame->data[0] == NULL || frame->data[1] == NULL ||
        frame->data[2] == NULL) {
        return;
    }

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (width <= 0 || height <= 0 || x >= frame->width || y >= frame->height) {
        return;
    }

    end_x = x + width;
    end_y = y + height;
    if (end_x > frame->width) {
        end_x = frame->width;
    }
    if (end_y > frame->height) {
        end_y = frame->height;
    }

    for (int row = y; row < end_y; ++row) {
        unsigned char *dst_y = frame->data[0] + row * frame->linesize[0];
        for (int col = x; col < end_x; ++col) {
            dst_y[col] = 235;
        }
    }

    for (int row = y / 2; row < (end_y + 1) / 2; ++row) {
        unsigned char *dst_u = frame->data[1] + row * frame->linesize[1];
        unsigned char *dst_v = frame->data[2] + row * frame->linesize[2];

        for (int col = x / 2; col < (end_x + 1) / 2; ++col) {
            dst_u[col] = 128;
            dst_v[col] = 128;
        }
    }
}

static int osd_processor_init(void *manager)
{
    FrameProcessorManager *processor = (FrameProcessorManager *)manager;
    OsdProcessorContext *ctx;

    if (processor == NULL) {
        return IPC_EINVAL;
    }

    ctx = (OsdProcessorContext *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return IPC_ENOMEM;
    }

    if (processor->config.width <= 0) {
        processor->config.width = 96;
    }
    if (processor->config.height <= 0) {
        processor->config.height = 24;
    }

    processor->priv = ctx;
    printf("[osd_processor] init\n");

    return IPC_OK;
}

static void osd_processor_deinit(void *manager)
{
    FrameProcessorManager *processor = (FrameProcessorManager *)manager;
    OsdProcessorContext *ctx;

    if (processor == NULL || processor->priv == NULL) {
        return;
    }

    ctx = (OsdProcessorContext *)processor->priv;
    free(ctx->buffer);
    free(ctx);
    processor->priv = NULL;

    printf("[osd_processor] deinit\n");
}

static int osd_processor_process(void *manager,
                                 MediaFrame *in,
                                 MediaFrame *out)
{
    FrameProcessorManager *processor = (FrameProcessorManager *)manager;
    OsdProcessorContext *ctx;
    unsigned char *y_plane;
    unsigned char *u_plane;
    unsigned char *v_plane;
    int width;
    int height;
    int size;

    if (processor == NULL || in == NULL || out == NULL) {
        return IPC_EINVAL;
    }
    if (processor->priv == NULL) {
        return IPC_ESTATE;
    }
    if (in->pixfmt != PIX_FMT_YUV420P) {
        return IPC_EUNSUPPORTED;
    }
    if (in->data[0] == NULL || in->data[1] == NULL || in->data[2] == NULL ||
        in->linesize[0] <= 0 || in->linesize[1] <= 0 ||
        in->linesize[2] <= 0 || in->width <= 0 || in->height <= 0 ||
        (in->width % 2) != 0 || (in->height % 2) != 0) {
        return IPC_EINVAL;
    }

    ctx = (OsdProcessorContext *)processor->priv;
    width = in->width;
    height = in->height;
    size = width * height * 3 / 2;

    if (ctx->buffer_size < size) {
        unsigned char *buffer = (unsigned char *)realloc(ctx->buffer, (size_t)size);
        if (buffer == NULL) {
            return IPC_ENOMEM;
        }
        ctx->buffer = buffer;
        ctx->buffer_size = size;
    }

    y_plane = ctx->buffer;
    u_plane = y_plane + width * height;
    v_plane = u_plane + width * height / 4;

    for (int row = 0; row < height; ++row) {
        memcpy(y_plane + row * width,
               in->data[0] + row * in->linesize[0],
               (size_t)width);
    }

    for (int row = 0; row < height / 2; ++row) {
        memcpy(u_plane + row * (width / 2),
               in->data[1] + row * in->linesize[1],
               (size_t)(width / 2));
        memcpy(v_plane + row * (width / 2),
               in->data[2] + row * in->linesize[2],
               (size_t)(width / 2));
    }

    out->width = width;
    out->height = height;
    out->pixfmt = PIX_FMT_YUV420P;
    out->data[0] = y_plane;
    out->data[1] = u_plane;
    out->data[2] = v_plane;
    out->linesize[0] = width;
    out->linesize[1] = width / 2;
    out->linesize[2] = width / 2;
    out->size = size;
    out->pts = in->pts;

    osd_draw_rect_yuv420(out,
                         processor->config.x,
                         processor->config.y,
                         processor->config.width,
                         processor->config.height);

    printf("[osd_processor] process\n");
    return IPC_OK;
}

const FrameProcessorOps g_osd_processor_ops = {
    .name = "osd",
    .init = osd_processor_init,
    .process = osd_processor_process,
    .deinit = osd_processor_deinit,
};
