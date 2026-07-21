#include "segment_muxer.h"

#include "ipc_log.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    AVFormatContext *format_ctx;
    AVStream *video_stream;
    MuxerConfig config;
    int segment_index;
    int frames_in_segment;
    int max_frames_per_segment;
    int header_written;
    char current_path[256];
} SegmentMuxerContext;

static int segment_copy_extradata(AVCodecParameters *codecpar,
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

static void segment_make_path(const char *base_path, int index,
                              char *out_path, size_t out_size)
{
    const char *slash;
    const char *name;
    const char *dot;
    char dir[128] = "output";
    char stem[128] = "record";

    if (base_path != NULL && base_path[0] != '\0') {
        slash = strrchr(base_path, '/');
        name = slash != NULL ? slash + 1 : base_path;
        if (slash != NULL) {
            size_t dir_len = (size_t)(slash - base_path);
            if (dir_len >= sizeof(dir)) {
                dir_len = sizeof(dir) - 1U;
            }
            memcpy(dir, base_path, dir_len);
            dir[dir_len] = '\0';
        }

        dot = strrchr(name, '.');
        if (dot != NULL && dot > name) {
            size_t stem_len = (size_t)(dot - name);
            if (stem_len >= sizeof(stem)) {
                stem_len = sizeof(stem) - 1U;
            }
            memcpy(stem, name, stem_len);
            stem[stem_len] = '\0';
        } else if (name[0] != '\0') {
            snprintf(stem, sizeof(stem), "%s", name);
        }
    }

    snprintf(out_path, out_size, "%s/%s_%06d.mp4", dir, stem, index);
}

static void segment_close_current(SegmentMuxerContext *ctx)
{
    if (ctx == NULL || ctx->format_ctx == NULL) {
        return;
    }

    if (ctx->header_written) {
        int ret = av_write_trailer(ctx->format_ctx);
        if (ret < 0) {
            IPC_LOGE("[segment] av_write_trailer failed: %d", ret);
        } else {
            IPC_LOGI("[segment] write_trailer %s", ctx->current_path);
        }
    }

    if ((ctx->format_ctx->oformat->flags & AVFMT_NOFILE) == 0 &&
        ctx->format_ctx->pb != NULL) {
        avio_closep(&ctx->format_ctx->pb);
    }

    avformat_free_context(ctx->format_ctx);
    ctx->format_ctx = NULL;
    ctx->video_stream = NULL;
    ctx->header_written = 0;
    ctx->frames_in_segment = 0;
    ctx->current_path[0] = '\0';
}

static int segment_open_next(SegmentMuxerContext *ctx, const MediaPacket *packet)
{
    AVStream *stream;
    int ret;

    if (ctx == NULL || packet == NULL) {
        return IPC_EINVAL;
    }

    ctx->segment_index++;
    segment_make_path(ctx->config.output_path, ctx->segment_index,
                      ctx->current_path, sizeof(ctx->current_path));

    ret = avformat_alloc_output_context2(&ctx->format_ctx, NULL, "mp4",
                                         ctx->current_path);
    if (ret < 0 || ctx->format_ctx == NULL) {
        IPC_LOGE("[segment] avformat_alloc_output_context2 failed: %d", ret);
        return IPC_EMUXER;
    }
    ctx->format_ctx->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_ZERO;

    stream = avformat_new_stream(ctx->format_ctx, NULL);
    if (stream == NULL) {
        segment_close_current(ctx);
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

    ret = segment_copy_extradata(stream->codecpar, packet);
    if (ret != IPC_OK) {
        IPC_LOGE("[segment] missing H264 extradata");
        segment_close_current(ctx);
        return ret;
    }

    ctx->video_stream = stream;

    if ((ctx->format_ctx->oformat->flags & AVFMT_NOFILE) == 0) {
        ret = avio_open(&ctx->format_ctx->pb, ctx->current_path, AVIO_FLAG_WRITE);
        if (ret < 0) {
            IPC_LOGE("[segment] avio_open failed: %d", ret);
            segment_close_current(ctx);
            return IPC_EOPEN;
        }
    }

    ret = avformat_write_header(ctx->format_ctx, NULL);
    if (ret < 0) {
        IPC_LOGE("[segment] avformat_write_header failed: %d", ret);
        segment_close_current(ctx);
        return IPC_EMUXER;
    }

    ctx->header_written = 1;
    ctx->frames_in_segment = 0;
    IPC_LOGI("[segment] open %s", ctx->current_path);
    return IPC_OK;
}

static int segment_muxer_init(MuxerManager *manager)
{
    SegmentMuxerContext *ctx;

    if (manager == NULL || manager->config.output_path[0] == '\0' ||
        manager->config.width <= 0 || manager->config.height <= 0 ||
        manager->config.fps <= 0 || manager->config.codec != CODEC_H264) {
        return IPC_EINVAL;
    }

    ctx = (SegmentMuxerContext *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return IPC_ENOMEM;
    }

    ctx->config = manager->config;
    if (ctx->config.segment_time <= 0) {
        ctx->config.segment_time = 60;
    }
    ctx->max_frames_per_segment = ctx->config.segment_time * ctx->config.fps;
    if (ctx->max_frames_per_segment <= 0) {
        free(ctx);
        return IPC_EINVAL;
    }

    manager->priv = ctx;
    IPC_LOGI("[segment] init segment_time=%d", ctx->config.segment_time);
    return IPC_OK;
}

static void segment_muxer_deinit(MuxerManager *manager)
{
    SegmentMuxerContext *ctx;

    if (manager == NULL || manager->priv == NULL) {
        return;
    }

    ctx = (SegmentMuxerContext *)manager->priv;
    segment_close_current(ctx);
    free(ctx);
    manager->priv = NULL;
    IPC_LOGI("[segment] deinit");
}

static int segment_muxer_open(MuxerManager *manager)
{
    if (manager == NULL || manager->priv == NULL) {
        return manager == NULL ? IPC_EINVAL : IPC_ESTATE;
    }

    IPC_LOGI("[segment] open pending");
    return IPC_OK;
}

static void segment_muxer_close(MuxerManager *manager)
{
    if (manager == NULL || manager->priv == NULL) {
        return;
    }

    segment_close_current((SegmentMuxerContext *)manager->priv);
    IPC_LOGI("[segment] close");
}

static int segment_muxer_write_header(MuxerManager *manager)
{
    if (manager == NULL) {
        return IPC_EINVAL;
    }
    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    IPC_LOGI("[segment] write_header pending");
    return IPC_OK;
}

static int segment_muxer_write_packet(MuxerManager *manager,
                                      const MediaPacket *packet)
{
    SegmentMuxerContext *ctx;
    AVPacket av_packet = {0};
    AVRational src_time_base;
    AVRational dst_time_base;
    uint8_t *packet_data;
    int64_t duration;
    int ret;

    if (manager == NULL || packet == NULL || packet->codec != CODEC_H264 ||
        packet->data == NULL || packet->size <= 0) {
        return IPC_EINVAL;
    }
    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    ctx = (SegmentMuxerContext *)manager->priv;

    if (ctx->format_ctx == NULL) {
        ret = segment_open_next(ctx, packet);
        if (ret != IPC_OK) {
            return ret;
        }
    } else if (ctx->frames_in_segment >= ctx->max_frames_per_segment &&
               packet->keyframe) {
        segment_close_current(ctx);
        ret = segment_open_next(ctx, packet);
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
    av_packet.pts = av_rescale_q(ctx->frames_in_segment,
                                 src_time_base,
                                 dst_time_base);
    av_packet.dts = av_packet.pts;
    av_packet.duration = duration;
    if (packet->keyframe) {
        av_packet.flags |= AV_PKT_FLAG_KEY;
    }

    ret = av_interleaved_write_frame(ctx->format_ctx, &av_packet);
    if (ret < 0) {
        IPC_LOGE("[segment] av_interleaved_write_frame failed: %d", ret);
        av_packet_unref(&av_packet);
        return IPC_EMUXER;
    }

    ctx->frames_in_segment++;
    return IPC_OK;
}

static int segment_muxer_write_trailer(MuxerManager *manager)
{
    if (manager == NULL) {
        return IPC_EINVAL;
    }
    if (manager->priv == NULL) {
        return IPC_ESTATE;
    }

    segment_close_current((SegmentMuxerContext *)manager->priv);
    return IPC_OK;
}

const MuxerOps g_segment_muxer_ops = {
    .name = "segment",
    .init = segment_muxer_init,
    .deinit = segment_muxer_deinit,
    .open = segment_muxer_open,
    .close = segment_muxer_close,
    .write_header = segment_muxer_write_header,
    .write_packet = segment_muxer_write_packet,
    .write_trailer = segment_muxer_write_trailer,
};
