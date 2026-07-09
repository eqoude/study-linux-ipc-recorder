#include "converter_manager.h"

#include "ipc_log.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    unsigned char *buffer;
    int width;
    int height;
    int size;
} YUYVToYUV420Context;

static int yuyv_to_yuv420_init(ConverterManager *manager)
{
    YUYVToYUV420Context *ctx;
    int width;
    int height;
    int size;

    if (manager == NULL ||
        manager->config.src_width <= 0 ||
        manager->config.src_height <= 0 ||
        manager->config.dst_width <= 0 ||
        manager->config.dst_height <= 0 ||
        manager->config.src_width != manager->config.dst_width ||
        manager->config.src_height != manager->config.dst_height ||
        (manager->config.dst_width % 2) != 0 ||
        (manager->config.dst_height % 2) != 0) {
        return IPC_EINVAL;
    }

    if (manager->config.src_format != PIX_FMT_YUYV422 ||
        manager->config.dst_format != PIX_FMT_YUV420P) {
        return IPC_EUNSUPPORTED;
    }

    width = manager->config.dst_width;
    height = manager->config.dst_height;
    size = width * height * 3 / 2;

    ctx = (YUYVToYUV420Context *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return IPC_ENOMEM;
    }

    ctx->buffer = (unsigned char *)malloc((size_t)size);
    if (ctx->buffer == NULL) {
        free(ctx);
        return IPC_ENOMEM;
    }

    ctx->width = width;
    ctx->height = height;
    ctx->size = size;
    manager->priv = ctx;

    IPC_LOGI("[yuyv_to_yuv420] init");
    return IPC_OK;
}

static void yuyv_to_yuv420_deinit(ConverterManager *manager)
{
    YUYVToYUV420Context *ctx;

    if (manager == NULL || manager->priv == NULL) {
        return;
    }

    ctx = (YUYVToYUV420Context *)manager->priv;
    free(ctx->buffer);
    free(ctx);
    manager->priv = NULL;

    IPC_LOGI("[yuyv_to_yuv420] deinit");
}

static int yuyv_to_yuv420_convert(ConverterManager *manager,
                                  const MediaFrame *src_frame,
                                  MediaFrame *dst_frame)
{
    YUYVToYUV420Context *ctx;
    const unsigned char *src;
    unsigned char *y_plane;
    unsigned char *u_plane;
    unsigned char *v_plane;
    int width;
    int height;

    if (manager == NULL || src_frame == NULL || dst_frame == NULL ||
        src_frame->data[0] == NULL || src_frame->linesize[0] <= 0) {
        return IPC_EINVAL;
    }

    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    if (src_frame->pixfmt != PIX_FMT_YUYV422) {
        return IPC_EUNSUPPORTED;
    }

    ctx = (YUYVToYUV420Context *)manager->priv;
    width = ctx->width;
    height = ctx->height;

    if (src_frame->width != width || src_frame->height != height ||
        src_frame->linesize[0] < width * 2) {
        return IPC_EINVAL;
    }

    src = src_frame->data[0];
    y_plane = ctx->buffer;
    u_plane = y_plane + width * height;
    v_plane = u_plane + width * height / 4;

    for (int row = 0; row < height; ++row) {
        const unsigned char *src_row = src + row * src_frame->linesize[0];
        unsigned char *dst_y = y_plane + row * width;

        for (int col = 0; col < width; col += 2) {
            int offset = col * 2;
            dst_y[col] = src_row[offset];
            dst_y[col + 1] = src_row[offset + 2];
        }
    }

    for (int row = 0; row < height; row += 2) {
        const unsigned char *src_row0 = src + row * src_frame->linesize[0];
        const unsigned char *src_row1 = src + (row + 1) * src_frame->linesize[0];
        unsigned char *dst_u = u_plane + (row / 2) * (width / 2);
        unsigned char *dst_v = v_plane + (row / 2) * (width / 2);

        for (int col = 0; col < width; col += 2) {
            int offset = col * 2;
            int u = ((int)src_row0[offset + 1] + (int)src_row1[offset + 1]) / 2;
            int v = ((int)src_row0[offset + 3] + (int)src_row1[offset + 3]) / 2;

            dst_u[col / 2] = (unsigned char)u;
            dst_v[col / 2] = (unsigned char)v;
        }
    }

    dst_frame->width = width;
    dst_frame->height = height;
    dst_frame->pixfmt = PIX_FMT_YUV420P;
    dst_frame->data[0] = y_plane;
    dst_frame->data[1] = u_plane;
    dst_frame->data[2] = v_plane;
    dst_frame->linesize[0] = width;
    dst_frame->linesize[1] = width / 2;
    dst_frame->linesize[2] = width / 2;
    dst_frame->size = ctx->size;
    dst_frame->pts = src_frame->pts;

    IPC_LOGD("[yuyv_to_yuv420] convert");
    return IPC_OK;
}

const ConverterOps g_yuyv_to_yuv420_ops = {
    .name = "yuyv_to_yuv420",
    .init = yuyv_to_yuv420_init,
    .deinit = yuyv_to_yuv420_deinit,
    .convert = yuyv_to_yuv420_convert,
};
