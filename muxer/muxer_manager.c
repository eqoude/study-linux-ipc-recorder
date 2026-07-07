#include "muxer_manager.h"

#include <stddef.h>
#include <string.h>

#define MUXER_MAX_OPS 16

static const MuxerOps *g_muxer_ops_table[MUXER_MAX_OPS];
static size_t g_muxer_ops_count;

static int muxer_validate_name(const char *name)
{
    return (name != NULL && name[0] != '\0') ? IPC_OK : IPC_EINVAL;
}

int MuxerManager_Register(const MuxerOps *ops)
{
    if (ops == NULL || ops->name == NULL || ops->name[0] == '\0') {
        return IPC_EINVAL;
    }

    for (size_t i = 0; i < g_muxer_ops_count; ++i) {
        if (strcmp(g_muxer_ops_table[i]->name, ops->name) == 0) {
            g_muxer_ops_table[i] = ops;
            return IPC_OK;
        }
    }

    if (g_muxer_ops_count >= MUXER_MAX_OPS) {
        return IPC_ENOMEM;
    }

    g_muxer_ops_table[g_muxer_ops_count++] = ops;
    return IPC_OK;
}

const MuxerOps *MuxerManager_Find(const char *muxer_name)
{
    if (muxer_validate_name(muxer_name) != IPC_OK) {
        return NULL;
    }

    for (size_t i = 0; i < g_muxer_ops_count; ++i) {
        if (strcmp(g_muxer_ops_table[i]->name, muxer_name) == 0) {
            return g_muxer_ops_table[i];
        }
    }

    return NULL;
}

int MuxerManager_Init(MuxerManager *manager,
                      const char *muxer_name,
                      const MuxerConfig *config)
{
    const MuxerOps *ops;

    if (manager == NULL || config == NULL || muxer_validate_name(muxer_name) < 0 ||
        config->output_path[0] == '\0' || config->format_name[0] == '\0') {
        return IPC_EINVAL;
    }

    ops = MuxerManager_Find(muxer_name);
    if (ops == NULL) {
        return IPC_ESTATE;
    }

    memset(manager, 0, sizeof(*manager));
    manager->ops = ops;
    strncpy(manager->muxer_name, muxer_name, sizeof(manager->muxer_name) - 1U);
    manager->config = *config;

    if (manager->ops->init != NULL) {
        return manager->ops->init(manager);
    }

    return IPC_OK;
}

void MuxerManager_Deinit(MuxerManager *manager)
{
    if (manager == NULL) {
        return;
    }

    if (manager->ops != NULL && manager->ops->deinit != NULL) {
        manager->ops->deinit(manager);
    }
}

int MuxerManager_Open(MuxerManager *manager)
{
    if (manager == NULL) {
        return IPC_EINVAL;
    }
    if (manager->ops == NULL || manager->ops->open == NULL) {
        return IPC_ESTATE;
    }

    return manager->ops->open(manager);
}

void MuxerManager_Close(MuxerManager *manager)
{
    if (manager == NULL || manager->ops == NULL || manager->ops->close == NULL) {
        return;
    }

    manager->ops->close(manager);
}

int MuxerManager_WriteHeader(MuxerManager *manager)
{
    if (manager == NULL) {
        return IPC_EINVAL;
    }
    if (manager->ops == NULL) {
        return IPC_ESTATE;
    }

    if (manager->ops->write_header == NULL) {
        return IPC_OK;
    }

    return manager->ops->write_header(manager);
}

int MuxerManager_WritePacket(MuxerManager *manager, const MediaPacket *packet)
{
    if (manager == NULL || packet == NULL) {
        return IPC_EINVAL;
    }
    if (manager->ops == NULL || manager->ops->write_packet == NULL) {
        return IPC_ESTATE;
    }

    return manager->ops->write_packet(manager, packet);
}

int MuxerManager_WriteTrailer(MuxerManager *manager)
{
    if (manager == NULL) {
        return IPC_EINVAL;
    }
    if (manager->ops == NULL) {
        return IPC_ESTATE;
    }

    if (manager->ops->write_trailer == NULL) {
        return IPC_OK;
    }

    return manager->ops->write_trailer(manager);
}
