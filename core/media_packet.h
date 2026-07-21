#ifndef MEDIA_PACKET_H
#define MEDIA_PACKET_H

#include <stdint.h>

#include <libavcodec/packet.h>
#include <libavutil/rational.h>

#include "ipc_error.h"

typedef enum {
    CODEC_UNKNOWN = 0,
    CODEC_H264,
    CODEC_H265,
} CodecType;

typedef struct {
    uint8_t *data;
    int size;
    int owns_data;
    int keyframe;
    uint8_t *extradata;
    int extradata_size;

    int64_t pts;
    int64_t dts;

    /*
     * Packet timestamp time base.
     * Current normalized video pipeline requires 1/fps.
     */
    AVRational time_base;

    CodecType codec;
} MediaPacket;

int MediaPacket_Alloc(MediaPacket *packet, int size);
void MediaPacket_Unref(MediaPacket *packet);
int MediaPacket_CopyFromAVPacket(MediaPacket *dst,
                                 const AVPacket *src,
                                 CodecType codec,
                                 AVRational time_base);

#endif
