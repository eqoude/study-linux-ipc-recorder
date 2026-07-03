#include "encoder_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>

#define H264_PENDING_PACKET_COUNT 16

typedef struct {
    AVCodecContext *codec_ctx;
    AVPacket *packet;
    AVPacket *pending_packets[H264_PENDING_PACKET_COUNT];
    uint8_t *extradata;
    int extradata_size;
    int pending_count;
    int flushing;
} H264FFmpegEncoderContext;

static int h264_ffmpeg_fill_packet(MediaPacket *out_packet,
                                   const AVPacket *av_packet,
                                   const H264FFmpegEncoderContext *ctx,
                                   AVRational time_base)
{
    int ret;

    ret = MediaPacket_CopyFromAVPacket(out_packet,
                                       av_packet,
                                       CODEC_H264,
                                       time_base);
    if (ret < 0) {
        return ret;
    }

    out_packet->keyframe = (av_packet->flags & AV_PKT_FLAG_KEY) != 0;
    if (ctx != NULL && ctx->extradata != NULL && ctx->extradata_size > 0) {
        out_packet->extradata = ctx->extradata;
        out_packet->extradata_size = ctx->extradata_size;
    }

    return 0;
}

static int h264_ffmpeg_pop_packet(H264FFmpegEncoderContext *ctx,
                                  MediaPacket *out_packet)
{
    AVPacket *packet;

    if (ctx == NULL || out_packet == NULL || ctx->pending_count <= 0) {
        return 1;
    }

    av_packet_unref(ctx->packet);
    packet = ctx->pending_packets[0];
    av_packet_move_ref(ctx->packet, packet);
    av_packet_free(&packet);

    for (int i = 1; i < ctx->pending_count; ++i) {
        ctx->pending_packets[i - 1] = ctx->pending_packets[i];
    }
    ctx->pending_packets[ctx->pending_count - 1] = NULL;
    ctx->pending_count--;

    return h264_ffmpeg_fill_packet(out_packet,
                                   ctx->packet,
                                   ctx,
                                   ctx->codec_ctx->time_base);
}

static int h264_ffmpeg_queue_packet(H264FFmpegEncoderContext *ctx,
                                    const AVPacket *src_packet)
{
    AVPacket *packet;

    if (ctx == NULL || src_packet == NULL ||
        ctx->pending_count >= H264_PENDING_PACKET_COUNT) {
        return -1;
    }

    packet = av_packet_alloc();
    if (packet == NULL) {
        return -1;
    }

    if (av_packet_ref(packet, src_packet) < 0) {
        av_packet_free(&packet);
        return -1;
    }

    ctx->pending_packets[ctx->pending_count++] = packet;
    return 0;
}

static int h264_ffmpeg_receive_packets(H264FFmpegEncoderContext *ctx)
{
    AVPacket *packet;
    int ret;

    if (ctx == NULL) {
        return -1;
    }

    packet = av_packet_alloc();
    if (packet == NULL) {
        return -1;
    }

    while (1) {
        av_packet_unref(packet);
        ret = avcodec_receive_packet(ctx->codec_ctx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_free(&packet);
            return 0;
        }
        if (ret < 0) {
            fprintf(stderr, "[h264_ffmpeg] avcodec_receive_packet failed: %d\n", ret);
            av_packet_free(&packet);
            return -1;
        }

        if (h264_ffmpeg_queue_packet(ctx, packet) < 0) {
            av_packet_free(&packet);
            return -1;
        }
    }
}

static int h264_ffmpeg_init(EncoderManager *manager)
{
    const AVCodec *codec;
    H264FFmpegEncoderContext *ctx;

    if (manager == NULL || manager->config.width <= 0 ||
        manager->config.height <= 0 || manager->config.fps <= 0 ||
        manager->config.bitrate <= 0 || manager->config.gop <= 0 ||
        manager->config.codec != CODEC_H264) {
        return -1;
    }

    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (codec == NULL) {
        fprintf(stderr, "[h264_ffmpeg] avcodec_find_encoder failed\n");
        return -1;
    }

    ctx = (H264FFmpegEncoderContext *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return -1;
    }

    ctx->codec_ctx = avcodec_alloc_context3(codec);
    if (ctx->codec_ctx == NULL) {
        free(ctx);
        return -1;
    }

    ctx->packet = av_packet_alloc();
    if (ctx->packet == NULL) {
        avcodec_free_context(&ctx->codec_ctx);
        free(ctx);
        return -1;
    }

    ctx->codec_ctx->width = manager->config.width;
    ctx->codec_ctx->height = manager->config.height;
    ctx->codec_ctx->time_base = (AVRational){ 1, manager->config.fps };
    ctx->codec_ctx->framerate = (AVRational){ manager->config.fps, 1 };
    ctx->codec_ctx->bit_rate = manager->config.bitrate;
    ctx->codec_ctx->gop_size = manager->config.fps * 2;
    ctx->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->codec_ctx->max_b_frames = 0;
    ctx->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (ctx->codec_ctx->priv_data != NULL) {
        (void)av_opt_set(ctx->codec_ctx->priv_data, "preset", "ultrafast", 0);
        (void)av_opt_set(ctx->codec_ctx->priv_data, "tune", "zerolatency", 0);
        (void)av_opt_set(ctx->codec_ctx->priv_data,
                         "x264-params",
                         "repeat-headers=1",
                         0);
    }

    if (avcodec_open2(ctx->codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "[h264_ffmpeg] avcodec_open2 failed\n");
        av_packet_free(&ctx->packet);
        avcodec_free_context(&ctx->codec_ctx);
        free(ctx);
        return -1;
    }

    if (ctx->codec_ctx->extradata == NULL ||
        ctx->codec_ctx->extradata_size <= 0) {
        fprintf(stderr, "[h264_ffmpeg] missing H264 SPS/PPS extradata\n");
        av_packet_free(&ctx->packet);
        avcodec_free_context(&ctx->codec_ctx);
        free(ctx);
        return -1;
    }

    ctx->extradata = (uint8_t *)av_mallocz((size_t)ctx->codec_ctx->extradata_size +
                                           AV_INPUT_BUFFER_PADDING_SIZE);
    if (ctx->extradata == NULL) {
        av_packet_free(&ctx->packet);
        avcodec_free_context(&ctx->codec_ctx);
        free(ctx);
        return -1;
    }

    memcpy(ctx->extradata,
           ctx->codec_ctx->extradata,
           (size_t)ctx->codec_ctx->extradata_size);
    ctx->extradata_size = ctx->codec_ctx->extradata_size;

    manager->priv = ctx;
    printf("[h264_ffmpeg] init\n");

    return 0;
}

static void h264_ffmpeg_deinit(EncoderManager *manager)
{
    H264FFmpegEncoderContext *ctx;

    if (manager == NULL || manager->priv == NULL) {
        return;
    }

    ctx = (H264FFmpegEncoderContext *)manager->priv;
    for (int i = 0; i < ctx->pending_count; ++i) {
        av_packet_free(&ctx->pending_packets[i]);
    }
    av_freep(&ctx->extradata);
    av_packet_free(&ctx->packet);
    avcodec_free_context(&ctx->codec_ctx);
    free(ctx);
    manager->priv = NULL;

    printf("[h264_ffmpeg] deinit\n");
}

static int h264_ffmpeg_encode(EncoderManager *manager,
                              const MediaFrame *src_frame,
                              MediaPacket *out_packet)
{
    H264FFmpegEncoderContext *ctx;
    AVFrame av_frame;
    int ret;

    if (manager == NULL || manager->priv == NULL || src_frame == NULL ||
        out_packet == NULL || src_frame->pixfmt != PIX_FMT_YUV420P ||
        src_frame->data[0] == NULL || src_frame->data[1] == NULL ||
        src_frame->data[2] == NULL || src_frame->linesize[0] <= 0 ||
        src_frame->linesize[1] <= 0 || src_frame->linesize[2] <= 0) {
        return -1;
    }

    ctx = (H264FFmpegEncoderContext *)manager->priv;
    if (ctx->flushing) {
        return -1;
    }

    MediaPacket_Unref(out_packet);

    memset(&av_frame, 0, sizeof(av_frame));
    av_frame.format = AV_PIX_FMT_YUV420P;
    av_frame.width = src_frame->width;
    av_frame.height = src_frame->height;
    av_frame.pts = src_frame->pts;
    av_frame.data[0] = src_frame->data[0];
    av_frame.data[1] = src_frame->data[1];
    av_frame.data[2] = src_frame->data[2];
    av_frame.linesize[0] = src_frame->linesize[0];
    av_frame.linesize[1] = src_frame->linesize[1];
    av_frame.linesize[2] = src_frame->linesize[2];

    if (av_frame.width != ctx->codec_ctx->width ||
        av_frame.height != ctx->codec_ctx->height ||
        av_image_check_size((unsigned int)av_frame.width,
                            (unsigned int)av_frame.height, 0, NULL) < 0) {
        return -1;
    }

    ret = avcodec_send_frame(ctx->codec_ctx, &av_frame);
    if (ret == AVERROR(EAGAIN)) {
        if (h264_ffmpeg_receive_packets(ctx) < 0) {
            return -1;
        }
        ret = avcodec_send_frame(ctx->codec_ctx, &av_frame);
    }
    if (ret < 0) {
        fprintf(stderr, "[h264_ffmpeg] avcodec_send_frame failed: %d\n", ret);
        return -1;
    }

    if (h264_ffmpeg_receive_packets(ctx) < 0) {
        return -1;
    }

    ret = h264_ffmpeg_pop_packet(ctx, out_packet);
    if (ret == 0) {
        printf("[h264_ffmpeg] encode\n");
    }
    return ret;
}

static int h264_ffmpeg_flush(EncoderManager *manager, MediaPacket *out_packet)
{
    H264FFmpegEncoderContext *ctx;
    int ret;

    if (manager == NULL || manager->priv == NULL || out_packet == NULL) {
        return -1;
    }

    ctx = (H264FFmpegEncoderContext *)manager->priv;
    MediaPacket_Unref(out_packet);

    if (!ctx->flushing) {
        ret = avcodec_send_frame(ctx->codec_ctx, NULL);
        if (ret < 0 && ret != AVERROR_EOF) {
            fprintf(stderr, "[h264_ffmpeg] flush send failed: %d\n", ret);
            return -1;
        }
        ctx->flushing = 1;
    }

    if (h264_ffmpeg_receive_packets(ctx) < 0) {
        return -1;
    }

    ret = h264_ffmpeg_pop_packet(ctx, out_packet);
    if (ret == 0) {
        printf("[h264_ffmpeg] flush\n");
    }

    return ret == 0 ? 0 : -1;
}

const EncoderOps g_h264_ffmpeg_encoder_ops = {
    .name = "h264_ffmpeg",
    .init = h264_ffmpeg_init,
    .deinit = h264_ffmpeg_deinit,
    .encode = h264_ffmpeg_encode,
    .flush = h264_ffmpeg_flush,
};
