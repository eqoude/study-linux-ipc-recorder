#include "encoder_manager.h"

#include <stddef.h>
#include <string.h>

#define ENCODER_MAX_OPS 16

static const EncoderOps *g_encoder_ops_table[ENCODER_MAX_OPS];
static size_t g_encoder_ops_count;

static int encoder_validate_name(const char *name)
{
    return (name != NULL && name[0] != '\0') ? 0 : -1;
}

int EncoderManager_Register(const EncoderOps *ops)
{
    if (ops == NULL || ops->name == NULL || ops->name[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < g_encoder_ops_count; ++i) {
        if (strcmp(g_encoder_ops_table[i]->name, ops->name) == 0) {
            g_encoder_ops_table[i] = ops;
            return 0;
        }
    }

    if (g_encoder_ops_count >= ENCODER_MAX_OPS) {
        return -1;
    }

    g_encoder_ops_table[g_encoder_ops_count++] = ops;
    return 0;
}

const EncoderOps *EncoderManager_Find(const char *encoder_name)
{
    if (encoder_validate_name(encoder_name) < 0) {
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
        return -1;
    }

    ops = EncoderManager_Find(encoder_name);
    if (ops == NULL) {
        return -1;
    }

    memset(manager, 0, sizeof(*manager));
    manager->ops = ops;
    strncpy(manager->encoder_name, encoder_name, sizeof(manager->encoder_name) - 1U);
    manager->config = *config;

    if (manager->ops->init != NULL) {
        return manager->ops->init(manager);
    }

    return 0;
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
    if (manager == NULL || src_frame == NULL || out_packet == NULL ||
        manager->ops == NULL || manager->ops->encode == NULL) {
        return -1;
    }

    return manager->ops->encode(manager, src_frame, out_packet);
}

int EncoderManager_Flush(EncoderManager *manager, MediaPacket *out_packet)
{
    if (manager == NULL || out_packet == NULL || manager->ops == NULL ||
        manager->ops->flush == NULL) {
        return -1;
    }

    return manager->ops->flush(manager, out_packet);
}
