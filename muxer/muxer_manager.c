#include "muxer_manager.h"

#include <stddef.h>
#include <string.h>

#define MUXER_MAX_OPS 16

static const MuxerOps *g_muxer_ops_table[MUXER_MAX_OPS];
static size_t g_muxer_ops_count;

static int muxer_validate_name(const char *name)
{
    return (name != NULL && name[0] != '\0') ? 0 : -1;
}

int MuxerManager_Register(const MuxerOps *ops)
{
    if (ops == NULL || ops->name == NULL || ops->name[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < g_muxer_ops_count; ++i) {
        if (strcmp(g_muxer_ops_table[i]->name, ops->name) == 0) {
            g_muxer_ops_table[i] = ops;
            return 0;
        }
    }

    if (g_muxer_ops_count >= MUXER_MAX_OPS) {
        return -1;
    }

    g_muxer_ops_table[g_muxer_ops_count++] = ops;
    return 0;
}

const MuxerOps *MuxerManager_Find(const char *muxer_name)
{
    if (muxer_validate_name(muxer_name) < 0) {
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
        config->output_path == NULL || config->output_path[0] == '\0' ||
        config->format_name == NULL || config->format_name[0] == '\0') {
        return -1;
    }

    ops = MuxerManager_Find(muxer_name);
    if (ops == NULL) {
        return -1;
    }

    memset(manager, 0, sizeof(*manager));
    manager->ops = ops;
    strncpy(manager->muxer_name, muxer_name, sizeof(manager->muxer_name) - 1U);
    manager->config = *config;

    if (manager->ops->init != NULL) {
        return manager->ops->init(manager);
    }

    return 0;
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
    if (manager == NULL || manager->ops == NULL || manager->ops->open == NULL) {
        return -1;
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
    if (manager == NULL || manager->ops == NULL) {
        return -1;
    }

    if (manager->ops->write_header == NULL) {
        return 0;
    }

    return manager->ops->write_header(manager);
}

int MuxerManager_WritePacket(MuxerManager *manager, const MediaPacket *packet)
{
    if (manager == NULL || packet == NULL || manager->ops == NULL ||
        manager->ops->write_packet == NULL) {
        return -1;
    }

    return manager->ops->write_packet(manager, packet);
}

int MuxerManager_WriteTrailer(MuxerManager *manager)
{
    if (manager == NULL || manager->ops == NULL) {
        return -1;
    }

    if (manager->ops->write_trailer == NULL) {
        return 0;
    }

    return manager->ops->write_trailer(manager);
}
