#include "encoder_manager.h"

#include <stdio.h>

static unsigned char g_fake_h264_packet[] = { 0x00, 0x00, 0x00, 0x01, 0x65 };

static int fake_encoder_init(EncoderManager *manager)
{
    (void)manager;
    printf("[fake_encoder] init\n");
    return 0;
}

static void fake_encoder_deinit(EncoderManager *manager)
{
    (void)manager;
    printf("[fake_encoder] deinit\n");
}

static int fake_encoder_encode(EncoderManager *manager,
                               const MediaFrame *src_frame,
                               MediaPacket *out_packet)
{
    (void)manager;
    if (src_frame == NULL || out_packet == NULL) {
        return -1;
    }

    printf("[fake_encoder] encode\n");

    out_packet->data = g_fake_h264_packet;
    out_packet->size = (int)sizeof(g_fake_h264_packet);
    out_packet->pts = src_frame->pts;
    out_packet->dts = src_frame->pts;
    out_packet->time_base = (AVRational){ 1, 30 };
    out_packet->codec = CODEC_H264;

    return 0;
}

static int fake_encoder_flush(EncoderManager *manager, MediaPacket *out_packet)
{
    (void)manager;
    (void)out_packet;
    printf("[fake_encoder] flush\n");
    return -1;
}

const EncoderOps g_fake_encoder_ops = {
    .name = "fake_encoder",
    .init = fake_encoder_init,
    .deinit = fake_encoder_deinit,
    .encode = fake_encoder_encode,
    .flush = fake_encoder_flush,
};
