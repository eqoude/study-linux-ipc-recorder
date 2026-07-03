#include "muxer_manager.h"

#include <stdio.h>

static int fake_muxer_init(MuxerManager *manager)
{
    (void)manager;
    printf("[fake_muxer] init\n");
    return 0;
}

static void fake_muxer_deinit(MuxerManager *manager)
{
    (void)manager;
    printf("[fake_muxer] deinit\n");
}

static int fake_muxer_open(MuxerManager *manager)
{
    (void)manager;
    printf("[fake_muxer] open\n");
    return 0;
}

static void fake_muxer_close(MuxerManager *manager)
{
    (void)manager;
    printf("[fake_muxer] close\n");
}

static int fake_muxer_write_packet(MuxerManager *manager, const MediaPacket *packet)
{
    (void)manager;
    if (packet == NULL) {
        return -1;
    }

    printf("[fake_muxer] write_packet size=%d codec=%d\n", packet->size, packet->codec);
    return 0;
}

const MuxerOps g_fake_muxer_ops = {
    .name = "fake_muxer",
    .init = fake_muxer_init,
    .deinit = fake_muxer_deinit,
    .open = fake_muxer_open,
    .close = fake_muxer_close,
    .write_packet = fake_muxer_write_packet,
};
