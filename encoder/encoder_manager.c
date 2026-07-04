#include "encoder_manager.h"

#include <stddef.h>
#include <string.h>

#define ENCODER_MAX_OPS 16

static const EncoderOps *g_encoder_ops_table[ENCODER_MAX_OPS];
static size_t g_encoder_ops_count;

static int encoder_validate_name(const char *name)
{
    return (name != NULL && name[0] != '\0') ? IPC_OK : IPC_EINVAL;
}

int EncoderManager_Register(const EncoderOps *ops)
{
    if (ops == NULL || ops->name == NULL || ops->name[0] == '\0') {
        return IPC_EINVAL;
    }

    for (size_t i = 0; i < g_encoder_ops_count; ++i) {
        if (strcmp(g_encoder_ops_table[i]->name, ops->name) == 0) {
            g_encoder_ops_table[i] = ops;
            return IPC_OK;
        }
    }

    if (g_encoder_ops_count >= ENCODER_MAX_OPS) {
        return IPC_ENOMEM;
    }

    g_encoder_ops_table[g_encoder_ops_count++] = ops;
    return IPC_OK;
}

const EncoderOps *EncoderManager_Find(const char *encoder_name)
{
    if (encoder_validate_name(encoder_name) != IPC_OK) {
        return NULL;
    }

    for (size_t i = 0; i < g_encoder_ops_count; ++i) {
        if (strcmp(g_encoder_ops_table[i]->name, encoder_name) == 0) {
            return g_encoder_ops_table[i];
        }
    }

    return NULL;
}

int EncoderManager_Init(EncoderManager *manager,
                        const char *encoder_name,
                        const EncoderConfig *config)
{
    const EncoderOps *ops;

    if (manager == NULL || config == NULL || encoder_validate_name(encoder_name) < 0) {
        return IPC_EINVAL;
    }

    ops = EncoderManager_Find(encoder_name);
    if (ops == NULL) {
        return IPC_ESTATE;
    }

    memset(manager, 0, sizeof(*manager));
    manager->ops = ops;
    strncpy(manager->encoder_name, encoder_name, sizeof(manager->encoder_name) - 1U);
    manager->config = *config;

    if (manager->ops->init != NULL) {
        return manager->ops->init(manager);
    }

    return IPC_OK;
}

void EncoderManager_Deinit(EncoderManager *manager)
{
    if (manager == NULL) {
        return;
    }

    if (manager->ops != NULL && manager->ops->deinit != NULL) {
        manager->ops->deinit(manager);
    }
}

int EncoderManager_Encode(EncoderManager *manager,
                          const MediaFrame *src_frame,
                          MediaPacket *out_packet)
{
    if (manager == NULL || src_frame == NULL || out_packet == NULL) {
        return IPC_EINVAL;
    }
    if (manager->ops == NULL || manager->ops->encode == NULL) {
        return IPC_ESTATE;
    }

    return manager->ops->encode(manager, src_frame, out_packet);
}

int EncoderManager_Flush(EncoderManager *manager, MediaPacket *out_packet)
{
    if (manager == NULL || out_packet == NULL) {
        return IPC_EINVAL;
    }
    if (manager->ops == NULL || manager->ops->flush == NULL) {
        return IPC_ESTATE;
    }

    return manager->ops->flush(manager, out_packet);
}
