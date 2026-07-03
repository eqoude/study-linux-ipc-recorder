#include "muxer_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/codec_id.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>

typedef struct {
    AVFormatContext *format_ctx;
    AVStream *video_stream;
    MuxerConfig config;
    int header_requested;
    int header_written;
    int64_t frame_index;
} Mp4MuxerContext;

static int mp4_muxer_copy_extradata(AVCodecParameters *codecpar,
                                    const MediaPacket *packet)
{
    uint8_t *extradata;

    if (codecpar == NULL || packet == NULL ||
        packet->extradata == NULL || packet->extradata_size <= 0) {
        return -1;
    }

    extradata = (uint8_t *)av_mallocz((size_t)packet->extradata_size +
                                      AV_INPUT_BUFFER_PADDING_SIZE);
    if (extradata == NULL) {
        return -1;
    }

    memcpy(extradata, packet->extradata, (size_t)packet->extradata_size);
    av_freep(&codecpar->extradata);
    codecpar->extradata = extradata;
    codecpar->extradata_size = packet->extradata_size;

    return 0;
}

static int mp4_muxer_write_header_if_needed(Mp4MuxerContext *ctx,
                                            const MediaPacket *packet)
{
    int ret;

    if (ctx == NULL || ctx->format_ctx == NULL || ctx->video_stream == NULL) {
        return -1;
    }

    if (ctx->header_written) {
        return 0;
    }

    if (mp4_muxer_copy_extradata(ctx->video_stream->codecpar, packet) < 0) {
        fprintf(stderr, "[mp4] missing H264 extradata\n");
        return -1;
    }

    ret = avformat_write_header(ctx->format_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "[mp4] avformat_write_header failed: %d\n", ret);
        return -1;
    }

    ctx->header_written = 1;
    printf("[mp4] write_header\n");
    return 0;
}

static int mp4_muxer_init(MuxerManager *manager)
{
    Mp4MuxerContext *ctx;

    if (manager == NULL || manager->config.output_path == NULL ||
        manager->config.output_path[0] == '\0' ||
        manager->config.width <= 0 || manager->config.height <= 0 ||
        manager->config.fps <= 0 || manager->config.codec != CODEC_H264) {
        return -1;
    }

    ctx = (Mp4MuxerContext *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return -1;
    }

    ctx->config = manager->config;
    ctx->frame_index = 0;
    manager->priv = ctx;

    printf("[mp4] init\n");
    return 0;
}

static void mp4_muxer_deinit(MuxerManager *manager)
{
    Mp4MuxerContext *ctx;

    if (manager == NULL || manager->priv == NULL) {
        return;
    }

    ctx = (Mp4MuxerContext *)manager->priv;
    if (ctx->format_ctx != NULL) {
        avformat_free_context(ctx->format_ctx);
        ctx->format_ctx = NULL;
    }

    free(ctx);
    manager->priv = NULL;

    printf("[mp4] deinit\n");
}

static int mp4_muxer_open(MuxerManager *manager)
{
    Mp4MuxerContext *ctx;
    AVStream *stream;
    int ret;

    if (manager == NULL || manager->priv == NULL) {
        return -1;
    }

    ctx = (Mp4MuxerContext *)manager->priv;
    ret = avformat_alloc_output_context2(&ctx->format_ctx, NULL, "mp4",
                                         ctx->config.output_path);
    if (ret < 0 || ctx->format_ctx == NULL) {
        fprintf(stderr, "[mp4] avformat_alloc_output_context2 failed: %d\n", ret);
        return -1;
    }
    ctx->format_ctx->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_ZERO;
    ctx->frame_index = 0;

    stream = avformat_new_stream(ctx->format_ctx, NULL);
    if (stream == NULL) {
        return -1;
    }

    stream->id = (int)(ctx->format_ctx->nb_streams - 1);
    stream->time_base = (AVRational){ 1, ctx->config.fps };
    stream->avg_frame_rate = (AVRational){ ctx->config.fps, 1 };
    stream->r_frame_rate = (AVRational){ ctx->config.fps, 1 };

    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id = AV_CODEC_ID_H264;
    stream->codecpar->width = ctx->config.width;
    stream->codecpar->height = ctx->config.height;
    stream->codecpar->format = AV_PIX_FMT_YUV420P;  // 编码前的像素格式信息

    ctx->video_stream = stream;

    if ((ctx->format_ctx->oformat->flags & AVFMT_NOFILE) == 0) {
        ret = avio_open(&ctx->format_ctx->pb, ctx->config.output_path, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "[mp4] avio_open failed: %d\n", ret);
            return -1;
        }
    }

    printf("[mp4] open %s\n", ctx->config.output_path);
    return 0;
}

static void mp4_muxer_close(MuxerManager *manager)
{
    Mp4MuxerContext *ctx;

    if (manager == NULL || manager->priv == NULL) {
        return;
    }

    ctx = (Mp4MuxerContext *)manager->priv;
    if (ctx->format_ctx != NULL && ctx->format_ctx->pb != NULL) {
        avio_closep(&ctx->format_ctx->pb);
    }

    printf("[mp4] close\n");
}

static int mp4_muxer_write_header(MuxerManager *manager)
{
    Mp4MuxerContext *ctx;

    if (manager == NULL || manager->priv == NULL) {
        return -1;
    }

    ctx = (Mp4MuxerContext *)manager->priv;
    if (ctx->format_ctx == NULL || ctx->video_stream == NULL) {
        return -1;
    }

    ctx->header_requested = 1;
    printf("[mp4] write_header pending\n");
    return 0;
}

static int mp4_muxer_write_packet(MuxerManager *manager,
                                  const MediaPacket *packet)
{
    Mp4MuxerContext *ctx;
    AVPacket av_packet = {0};
    AVRational src_time_base;
    AVRational dst_time_base;
    uint8_t *packet_data;
    int64_t duration;
    int ret;

    if (manager == NULL || manager->priv == NULL || packet == NULL ||
        packet->codec != CODEC_H264 || packet->data == NULL ||
        packet->size <= 0) {
        return -1;
    }

    ctx = (Mp4MuxerContext *)manager->priv;
    if (ctx->format_ctx == NULL || ctx->video_stream == NULL) {
        return -1;
    }

    if (mp4_muxer_write_header_if_needed(ctx, packet) < 0) {
        return -1;
    }

    src_time_base = (AVRational){ 1, ctx->config.fps };
    dst_time_base = ctx->video_stream->time_base;

    duration = av_rescale_q(1, src_time_base, dst_time_base);
    if (duration <= 0) {
        duration = 1;
    }


    packet_data = (uint8_t *)av_malloc((size_t)packet->size +
                                       AV_INPUT_BUFFER_PADDING_SIZE);
    if (packet_data == NULL) {
        return -1;
    }

    memcpy(packet_data, packet->data, (size_t)packet->size);
    memset(packet_data + packet->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    ret = av_packet_from_data(&av_packet, packet_data, packet->size);
    if (ret < 0) {
        av_free(packet_data);
        return -1;
    }

    av_packet.stream_index = ctx->video_stream->index;
    av_packet.pts = av_rescale_q(ctx->frame_index, src_time_base, dst_time_base);
    av_packet.dts = av_packet.pts;
    av_packet.duration = duration;
    if (packet->keyframe) {
        av_packet.flags |= AV_PKT_FLAG_KEY;
    }

    ret = av_interleaved_write_frame(ctx->format_ctx, &av_packet);
    if (ret < 0) {
        fprintf(stderr,"[mp4] av_interleaved_write_frame failed: %d\n",ret);
        av_packet_unref(&av_packet);
        return -1;
    }

    /*
     * av_interleaved_write_frame 成功后会接管 AVPacket 内部 buffer。
     * 失败路径已 av_packet_unref，避免 packet_data 泄漏。
     */
    ctx->frame_index++;

    printf("[mp4] write_packet size=%d\n", packet->size);
    return 0;
}

static int mp4_muxer_write_trailer(MuxerManager *manager)
{
    Mp4MuxerContext *ctx;
    int ret;

    if (manager == NULL || manager->priv == NULL) {
        return -1;
    }

    ctx = (Mp4MuxerContext *)manager->priv;
    if (ctx->format_ctx == NULL) {
        return -1;
    }
    if (!ctx->header_written) {
        return 0;
    }

    ret = av_write_trailer(ctx->format_ctx);
    if (ret < 0) {
        fprintf(stderr, "[mp4] av_write_trailer failed: %d\n", ret);
        return -1;
    }

    printf("[mp4] write_trailer\n");
    return 0;
}

const MuxerOps g_mp4_muxer_ops = {
    .name = "mp4",
    .init = mp4_muxer_init,
    .deinit = mp4_muxer_deinit,
    .open = mp4_muxer_open,
    .close = mp4_muxer_close,
    .write_header = mp4_muxer_write_header,
    .write_packet = mp4_muxer_write_packet,
    .write_trailer = mp4_muxer_write_trailer,
};
