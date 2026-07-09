#include "muxer_manager.h"

#include "ipc_log.h"

#include <stdio.h>

static int fake_muxer_init(MuxerManager *manager)
{
    if (manager == NULL) {
        return IPC_EINVAL;
    }

    IPC_LOGI("[fake_muxer] init");
    return IPC_OK;
}

static void fake_muxer_deinit(MuxerManager *manager)
{
    (void)manager;
    IPC_LOGI("[fake_muxer] deinit");
}

static int fake_muxer_open(MuxerManager *manager)
{
    if (manager == NULL) {
        return IPC_EINVAL;
    }

    IPC_LOGI("[fake_muxer] open");
    return IPC_OK;
}

static void fake_muxer_close(MuxerManager *manager)
{
    (void)manager;
    IPC_LOGI("[fake_muxer] close");
}

static int fake_muxer_write_packet(MuxerManager *manager, const MediaPacket *packet)
{
    if (manager == NULL || packet == NULL) {
        return IPC_EINVAL;
    }

    IPC_LOGD("[fake_muxer] write_packet size=%d codec=%d", packet->size, packet->codec);
    return IPC_OK;
}

const MuxerOps g_fake_muxer_ops = {
    .name = "fake_muxer",
    .init = fake_muxer_init,
    .deinit = fake_muxer_deinit,
    .open = fake_muxer_open,
    .close = fake_muxer_close,
    .write_packet = fake_muxer_write_packet,
};
