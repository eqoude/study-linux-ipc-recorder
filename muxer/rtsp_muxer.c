#include "muxer_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>

typedef struct {
    AVFormatContext *format_ctx;
    AVStream *video_stream;
    MuxerConfig config;
    int header_requested;
    int header_written;
    int trailer_written;
    int64_t frame_index;
} RtspMuxerContext;

static int rtsp_muxer_map_open_error(int errnum)
{
    int posix_errno = AVUNERROR(errnum);

    if (posix_errno == ECONNREFUSED || posix_errno == ETIMEDOUT ||
        posix_errno == EHOSTUNREACH || posix_errno == ENETUNREACH ||
        posix_errno == ECONNRESET || posix_errno == ENOENT) {
        return IPC_EOPEN;
    }

    return IPC_EMUXER;
}

static void rtsp_muxer_print_error(const char *operation, int errnum)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    int mapped_error = rtsp_muxer_map_open_error(errnum);

    if (av_strerror(errnum, errbuf, sizeof(errbuf)) < 0) {
        snprintf(errbuf, sizeof(errbuf), "unknown error");
    }

    fprintf(stderr, "[rtsp] %s failed: %d (%s)\n", operation, errnum, errbuf);
    if (mapped_error == IPC_EOPEN) {
        fprintf(stderr,
                "[rtsp] RTSP server not available: %s (%d). "
                "Please start mediamtx or another RTSP server first.\n",
                IpcError_ToString(mapped_error),
                mapped_error);
    }
}

static int rtsp_muxer_copy_extradata(AVCodecParameters *codecpar,
                                     const MediaPacket *packet)
{
    uint8_t *extradata;

    if (codecpar == NULL || packet == NULL ||
        packet->extradata == NULL || packet->extradata_size <= 0) {
        return IPC_ESTATE;
    }

    extradata = (uint8_t *)av_mallocz((size_t)packet->extradata_size +
                                      AV_INPUT_BUFFER_PADDING_SIZE);
    if (extradata == NULL) {
        return IPC_ENOMEM;
    }

    memcpy(extradata, packet->extradata, (size_t)packet->extradata_size);
    av_freep(&codecpar->extradata);
    codecpar->extradata = extradata;
    codecpar->extradata_size = packet->extradata_size;

    return IPC_OK;
}

static int rtsp_muxer_write_header_with_packet(RtspMuxerContext *ctx,
                                               const MediaPacket *packet)
{
    AVDictionary *options = NULL;
    int ret;

    if (ctx == NULL || ctx->format_ctx == NULL ||
        ctx->video_stream == NULL || packet == NULL) {
        return IPC_ESTATE;
    }
    if (ctx->header_written) {
        return IPC_OK;
    }

    ret = rtsp_muxer_copy_extradata(ctx->video_stream->codecpar, packet);
    if (ret != IPC_OK) {
        fprintf(stderr, "[rtsp] missing H264 SPS/PPS extradata\n");
        return ret;
    }

    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    ret = avformat_write_header(ctx->format_ctx, &options);
    av_dict_free(&options);
    if (ret < 0) {
        rtsp_muxer_print_error("avformat_write_header", ret);
        return rtsp_muxer_map_open_error(ret);
    }

    ctx->header_written = 1;
    printf("[rtsp] write_header\n");
    return IPC_OK;
}

static int rtsp_muxer_init(MuxerManager *manager)
{
    RtspMuxerContext *ctx;

    if (manager == NULL || manager->config.output_path[0] == '\0' ||
        manager->config.width <= 0 || manager->config.height <= 0 ||
        manager->config.fps <= 0 || manager->config.codec != CODEC_H264) {
        return IPC_EINVAL;
    }

    ctx = (RtspMuxerContext *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return IPC_ENOMEM;
    }

    ctx->config = manager->config;
    ctx->frame_index = 0;
    manager->priv = ctx;

    printf("[rtsp] init\n");
    return IPC_OK;
}

static void rtsp_muxer_deinit(MuxerManager *manager)
{
    RtspMuxerContext *ctx;

    if (manager == NULL || manager->priv == NULL) {
        return;
    }

    ctx = (RtspMuxerContext *)manager->priv;

    if (ctx->format_ctx != NULL && ctx->header_written && !ctx->trailer_written) {
        av_write_trailer(ctx->format_ctx);
        ctx->trailer_written = 1;
    }

    if (ctx->format_ctx != NULL && ctx->format_ctx->pb != NULL) {
        avio_closep(&ctx->format_ctx->pb);
    }

    if (ctx->format_ctx != NULL) {
        avformat_free_context(ctx->format_ctx);
        ctx->format_ctx = NULL;
    }

    free(ctx);
    manager->priv = NULL;

    printf("[rtsp] deinit\n");
}

static int rtsp_muxer_open(MuxerManager *manager)
{
    RtspMuxerContext *ctx;
    AVStream *stream;
    int ret;

    if (manager == NULL) {
        return IPC_EINVAL;
    }
    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    ctx = (RtspMuxerContext *)manager->priv;
    ret = avformat_alloc_output_context2(&ctx->format_ctx, NULL, "rtsp",
                                         ctx->config.output_path);
    if (ret < 0 || ctx->format_ctx == NULL) {
        fprintf(stderr, "[rtsp] avformat_alloc_output_context2 failed: %d\n", ret);
        return IPC_EMUXER;
    }

    ctx->frame_index = 0;
    stream = avformat_new_stream(ctx->format_ctx, NULL);
    if (stream == NULL) {
        return IPC_EMUXER;
    }

    stream->id = (int)(ctx->format_ctx->nb_streams - 1);
    stream->time_base = (AVRational){ 1, ctx->config.fps };
    stream->avg_frame_rate = (AVRational){ ctx->config.fps, 1 };
    stream->r_frame_rate = (AVRational){ ctx->config.fps, 1 };

    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id = AV_CODEC_ID_H264;
    stream->codecpar->width = ctx->config.width;
    stream->codecpar->height = ctx->config.height;
    stream->codecpar->format = AV_PIX_FMT_YUV420P;

    ctx->video_stream = stream;

    printf("[rtsp] open %s\n", ctx->config.output_path);
    return IPC_OK;
}

static void rtsp_muxer_close(MuxerManager *manager)
{
    RtspMuxerContext *ctx;

    if (manager == NULL || manager->priv == NULL) {
        return;
    }

    ctx = (RtspMuxerContext *)manager->priv;
    if (ctx->format_ctx != NULL && ctx->format_ctx->pb != NULL) {
        avio_closep(&ctx->format_ctx->pb);
    }

    printf("[rtsp] close\n");
}

static int rtsp_muxer_write_header(MuxerManager *manager)
{
    RtspMuxerContext *ctx;

    if (manager == NULL) {
        return IPC_EINVAL;
    }
    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    ctx = (RtspMuxerContext *)manager->priv;
    if (ctx->format_ctx == NULL || ctx->video_stream == NULL) {
        return IPC_ESTATE;
    }
    if (ctx->header_written) {
        return IPC_OK;
    }

    ctx->header_requested = 1;
    printf("[rtsp] write_header pending\n");
    return IPC_OK;
}

static int rtsp_muxer_write_packet(MuxerManager *manager,
                                   const MediaPacket *packet)
{
    RtspMuxerContext *ctx;
    AVPacket av_packet = {0};
    AVRational src_time_base;
    AVRational dst_time_base;
    uint8_t *packet_data;
    int64_t duration;
    int ret;

    if (manager == NULL || packet == NULL ||
        packet->codec != CODEC_H264 || packet->data == NULL ||
        packet->size <= 0) {
        return IPC_EINVAL;
    }
    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    ctx = (RtspMuxerContext *)manager->priv;
    if (ctx->format_ctx == NULL || ctx->video_stream == NULL) {
        return IPC_ESTATE;
    }
    if (!ctx->header_written) {
        if (!ctx->header_requested) {
            fprintf(stderr, "[rtsp] write_packet before write_header\n");
            return IPC_ESTATE;
        }
        ret = rtsp_muxer_write_header_with_packet(ctx, packet);
        if (ret != IPC_OK) {
            return ret;
        }
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
        return IPC_ENOMEM;
    }

    memcpy(packet_data, packet->data, (size_t)packet->size);
    memset(packet_data + packet->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    ret = av_packet_from_data(&av_packet, packet_data, packet->size);
    if (ret < 0) {
        av_free(packet_data);
        return IPC_EMUXER;
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
        fprintf(stderr, "[rtsp] av_interleaved_write_frame failed: %d\n", ret);
        av_packet_unref(&av_packet);
        return IPC_EMUXER;
    }

    ctx->frame_index++;
    return IPC_OK;
}

static int rtsp_muxer_write_trailer(MuxerManager *manager)
{
    RtspMuxerContext *ctx;
    int ret;

    if (manager == NULL) {
        return IPC_EINVAL;
    }
    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    ctx = (RtspMuxerContext *)manager->priv;
    if (ctx->format_ctx == NULL || !ctx->header_written || ctx->trailer_written) {
        return IPC_OK;
    }

    ret = av_write_trailer(ctx->format_ctx);
    if (ret < 0) {
        fprintf(stderr, "[rtsp] av_write_trailer failed: %d\n", ret);
        return IPC_EMUXER;
    }

    ctx->trailer_written = 1;
    printf("[rtsp] write_trailer\n");
    return IPC_OK;
}

const MuxerOps g_rtsp_muxer_ops = {
    .name = "rtsp",
    .init = rtsp_muxer_init,
    .deinit = rtsp_muxer_deinit,
    .open = rtsp_muxer_open,
    .close = rtsp_muxer_close,
    .write_header = rtsp_muxer_write_header,
    .write_packet = rtsp_muxer_write_packet,
    .write_trailer = rtsp_muxer_write_trailer,
};
