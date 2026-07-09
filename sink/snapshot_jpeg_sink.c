#include "snapshot_jpeg_sink.h"

#include "ipc_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>

#define SNAPSHOT_JPEG_DEFAULT_PATH "edge_ai_camera_test/snapshot.jpg"
#define SNAPSHOT_JPEG_DEFAULT_INTERVAL_FRAMES 30
#define SNAPSHOT_JPEG_TIME_BASE_FPS 30

typedef struct {
    char output_path[256];
    char tmp_path[256];

    int interval_frames;
    int frame_count;

    int width;
    int height;
} SnapshotJpegSinkContext;

static enum AVPixelFormat snapshot_jpeg_choose_pix_fmt(const AVCodec *codec)
{
    const enum AVPixelFormat *pix_fmt;

    if (codec == NULL || codec->pix_fmts == NULL) {
        return AV_PIX_FMT_YUVJ420P;
    }

    for (pix_fmt = codec->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; ++pix_fmt) {
        if (*pix_fmt == AV_PIX_FMT_YUV420P) {
            return AV_PIX_FMT_YUV420P;
        }
    }

    for (pix_fmt = codec->pix_fmts; *pix_fmt != AV_PIX_FMT_NONE; ++pix_fmt) {
        if (*pix_fmt == AV_PIX_FMT_YUVJ420P) {
            return AV_PIX_FMT_YUVJ420P;
        }
    }

    return codec->pix_fmts[0];
}

static int snapshot_jpeg_ensure_parent_dir(const char *path)
{
    char dir_path[256];
    char *slash;

    if (path == NULL || path[0] == '\0') {
        return IPC_EINVAL;
    }

    snprintf(dir_path, sizeof(dir_path), "%s", path);
    slash = strrchr(dir_path, '/');
    if (slash == NULL) {
        return IPC_OK;
    }

    *slash = '\0';
    if (dir_path[0] == '\0') {
        return IPC_OK;
    }

    if (mkdir(dir_path, 0755) != 0 && errno != EEXIST) {
        IPC_LOGE("[snapshot_jpeg] failed to create directory %s: %s",
                 dir_path,
                 strerror(errno));
        return IPC_EOPEN;
    }

    return IPC_OK;
}

static int snapshot_jpeg_write_file(const char *path,
                                    const uint8_t *data,
                                    int size)
{
    FILE *fp;
    size_t written;

    if (path == NULL || data == NULL || size <= 0) {
        return IPC_EINVAL;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        IPC_LOGE("[snapshot_jpeg] failed to open %s: %s",
                 path,
                 strerror(errno));
        return IPC_EOPEN;
    }

    written = fwrite(data, 1, (size_t)size, fp);
    if (written != (size_t)size) {
        IPC_LOGE("[snapshot_jpeg] failed to write %s: %s",
                 path,
                 ferror(fp) ? strerror(errno) : "short write");
        fclose(fp);
        return IPC_EIO;
    }

    if (fclose(fp) != 0) {
        IPC_LOGE("[snapshot_jpeg] failed to close %s: %s",
                 path,
                 strerror(errno));
        return IPC_EIO;
    }

    return IPC_OK;
}

static int snapshot_jpeg_encode_frame(const SnapshotJpegSinkContext *ctx,
                                      const MediaFrame *frame)
{
    const AVCodec *codec;
    AVCodecContext *codec_ctx = NULL;
    AVFrame *jpeg_frame = NULL;
    AVPacket *packet = NULL;
    struct SwsContext *sws_ctx = NULL;
    const uint8_t *src_data[4] = {0};
    int src_linesize[4] = {0};
    int ret = IPC_OK;

    codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (codec == NULL) {
        IPC_LOGE("[snapshot_jpeg] MJPEG encoder not found");
        return IPC_ECODEC;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    jpeg_frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (codec_ctx == NULL || jpeg_frame == NULL || packet == NULL) {
        ret = IPC_ENOMEM;
        goto cleanup;
    }

    codec_ctx->width = frame->width;
    codec_ctx->height = frame->height;
    codec_ctx->time_base = (AVRational){1, 1};
    codec_ctx->pix_fmt = snapshot_jpeg_choose_pix_fmt(codec);
    codec_ctx->color_range = AVCOL_RANGE_JPEG;

    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        IPC_LOGE("[snapshot_jpeg] avcodec_open2(MJPEG) failed");
        ret = IPC_ECODEC;
        goto cleanup;
    }

    jpeg_frame->format = codec_ctx->pix_fmt;
    jpeg_frame->width = codec_ctx->width;
    jpeg_frame->height = codec_ctx->height;
    jpeg_frame->pts = frame->pts;
    jpeg_frame->color_range = AVCOL_RANGE_JPEG;

    if (av_frame_get_buffer(jpeg_frame, 32) < 0) {
        IPC_LOGE("[snapshot_jpeg] av_frame_get_buffer failed");
        ret = IPC_ENOMEM;
        goto cleanup;
    }

    sws_ctx = sws_getContext(frame->width,
                             frame->height,
                             AV_PIX_FMT_YUV420P,
                             frame->width,
                             frame->height,
                             codec_ctx->pix_fmt,
                             SWS_BILINEAR,
                             NULL,
                             NULL,
                             NULL);
    if (sws_ctx == NULL) {
        IPC_LOGE("[snapshot_jpeg] sws_getContext failed");
        ret = IPC_ERROR;
        goto cleanup;
    }

    src_data[0] = frame->data[0];
    src_data[1] = frame->data[1];
    src_data[2] = frame->data[2];
    src_linesize[0] = frame->linesize[0];
    src_linesize[1] = frame->linesize[1];
    src_linesize[2] = frame->linesize[2];

    sws_scale(sws_ctx,
              src_data,
              src_linesize,
              0,
              frame->height,
              jpeg_frame->data,
              jpeg_frame->linesize);

    if (avcodec_send_frame(codec_ctx, jpeg_frame) < 0) {
        IPC_LOGE("[snapshot_jpeg] avcodec_send_frame failed");
        ret = IPC_ECODEC;
        goto cleanup;
    }

    if (avcodec_receive_packet(codec_ctx, packet) < 0) {
        IPC_LOGE("[snapshot_jpeg] avcodec_receive_packet failed");
        ret = IPC_ECODEC;
        goto cleanup;
    }

    ret = snapshot_jpeg_write_file(ctx->tmp_path, packet->data, packet->size);
    if (ret != IPC_OK) {
        goto cleanup;
    }

    if (rename(ctx->tmp_path, ctx->output_path) != 0) {
        IPC_LOGE("[snapshot_jpeg] failed to rename %s to %s: %s",
                 ctx->tmp_path,
                 ctx->output_path,
                 strerror(errno));
        unlink(ctx->tmp_path);
        ret = IPC_EIO;
        goto cleanup;
    }

    IPC_LOGI("[snapshot_jpeg] wrote %s", ctx->output_path);

cleanup:
    if (ret != IPC_OK) {
        unlink(ctx->tmp_path);
    }
    av_packet_free(&packet);
    av_frame_free(&jpeg_frame);
    avcodec_free_context(&codec_ctx);
    if (sws_ctx != NULL) {
        sws_freeContext(sws_ctx);
    }

    return ret;
}

static int snapshot_jpeg_init(void **ctx, const void *config)
{
    const SnapshotJpegSinkConfig *snapshot_config =
        (const SnapshotJpegSinkConfig *)config;
    SnapshotJpegSinkContext *snapshot_ctx;
    const char *output_path = SNAPSHOT_JPEG_DEFAULT_PATH;
    int interval_frames = SNAPSHOT_JPEG_DEFAULT_INTERVAL_FRAMES;
    int written;
    int ret;

    if (ctx == NULL) {
        return IPC_EINVAL;
    }

    if (snapshot_config != NULL) {
        if (snapshot_config->output_path != NULL &&
            snapshot_config->output_path[0] != '\0') {
            output_path = snapshot_config->output_path;
        }

        if (snapshot_config->interval_frames > 0) {
            interval_frames = snapshot_config->interval_frames;
        }
    }

    snapshot_ctx = (SnapshotJpegSinkContext *)calloc(1, sizeof(*snapshot_ctx));
    if (snapshot_ctx == NULL) {
        return IPC_ENOMEM;
    }

    written = snprintf(snapshot_ctx->output_path,
                       sizeof(snapshot_ctx->output_path),
                       "%s",
                       output_path);
    if (written < 0 || written >= (int)sizeof(snapshot_ctx->output_path)) {
        free(snapshot_ctx);
        return IPC_EINVAL;
    }

    written = snprintf(snapshot_ctx->tmp_path,
                       sizeof(snapshot_ctx->tmp_path),
                       "%s.tmp",
                       output_path);
    if (written < 0 || written >= (int)sizeof(snapshot_ctx->tmp_path)) {
        free(snapshot_ctx);
        return IPC_EINVAL;
    }

    ret = snapshot_jpeg_ensure_parent_dir(snapshot_ctx->output_path);
    if (ret != IPC_OK) {
        free(snapshot_ctx);
        return ret;
    }

    snapshot_ctx->interval_frames = interval_frames;
    *ctx = snapshot_ctx;

    return IPC_OK;
}

static int snapshot_jpeg_write(void *ctx, const MediaFrame *frame)
{
    SnapshotJpegSinkContext *snapshot_ctx = (SnapshotJpegSinkContext *)ctx;

    if (snapshot_ctx == NULL || frame == NULL) {
        return IPC_EINVAL;
    }

    snapshot_ctx->frame_count++;

    if (snapshot_ctx->interval_frames <= 0) {
        snapshot_ctx->interval_frames = SNAPSHOT_JPEG_DEFAULT_INTERVAL_FRAMES;
    }

    if (frame->pixfmt != PIX_FMT_YUV420P) {
        IPC_LOGW("[snapshot_jpeg] unsupported frame pixfmt: %d", frame->pixfmt);
        return IPC_OK;
    }

    if (frame->width <= 0 || frame->height <= 0 ||
        frame->data[0] == NULL || frame->data[1] == NULL ||
        frame->data[2] == NULL ||
        frame->linesize[0] < frame->width ||
        frame->linesize[1] < frame->width / 2 ||
        frame->linesize[2] < frame->width / 2) {
        return IPC_EINVAL;
    }

    snapshot_ctx->width = frame->width;
    snapshot_ctx->height = frame->height;

    if ((snapshot_ctx->frame_count % snapshot_ctx->interval_frames) != 0) {
        return IPC_OK;
    }

    return snapshot_jpeg_encode_frame(snapshot_ctx, frame);
}

static void snapshot_jpeg_deinit(void *ctx)
{
    free(ctx);
}

const FrameSinkOps g_snapshot_jpeg_sink_ops = {
    .name = "snapshot_jpeg",
    .init = snapshot_jpeg_init,
    .write = snapshot_jpeg_write,
    .deinit = snapshot_jpeg_deinit,
};
