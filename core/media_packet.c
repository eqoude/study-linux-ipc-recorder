#include "media_packet.h"

#include <stdlib.h>
#include <string.h>

int MediaPacket_Alloc(MediaPacket *packet, int size)
{
    uint8_t *data;

    if (packet == NULL || size <= 0) {
        return IPC_EINVAL;
    }

    MediaPacket_Unref(packet);

    data = (uint8_t *)malloc((size_t)size);
    if (data == NULL) {
        return IPC_ENOMEM;
    }

    packet->data = data;
    packet->size = size;
    packet->owns_data = 1;
    return IPC_OK;
}

void MediaPacket_Unref(MediaPacket *packet)
{
    if (packet == NULL) {
        return;
    }

    if (packet->owns_data) {
        free(packet->data);
    }
    memset(packet, 0, sizeof(*packet));
}

int MediaPacket_CopyFromAVPacket(MediaPacket *dst,
                                 const AVPacket *src,
                                 CodecType codec,
                                 AVRational time_base)
{
    if (dst == NULL || src == NULL || src->data == NULL || src->size <= 0) {
        return IPC_EINVAL;
    }

    int ret = MediaPacket_Alloc(dst, src->size);
    if (ret != IPC_OK) {
        return ret;
    }

    memcpy(dst->data, src->data, (size_t)src->size);
    dst->pts = src->pts;
    dst->dts = src->dts;
    dst->time_base = time_base;
    dst->codec = codec;

    return IPC_OK;
}
