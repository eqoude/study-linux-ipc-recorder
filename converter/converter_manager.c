#include "converter_manager.h"

#include <stddef.h>
#include <string.h>

#define CONVERTER_MAX_OPS 16

static const ConverterOps *g_converter_ops_table[CONVERTER_MAX_OPS];
static size_t g_converter_ops_count;

static int converter_validate_name(const char *name)
{
    return (name != NULL && name[0] != '\0') ? IPC_OK : IPC_EINVAL;
}

int ConverterManager_Register(const ConverterOps *ops)
{
    if (ops == NULL || ops->name == NULL || ops->name[0] == '\0') {
        return IPC_EINVAL;
    }

    for (size_t i = 0; i < g_converter_ops_count; ++i) {
        if (strcmp(g_converter_ops_table[i]->name, ops->name) == 0) {
            g_converter_ops_table[i] = ops;
            return IPC_OK;
        }
    }

    if (g_converter_ops_count >= CONVERTER_MAX_OPS) {
        return IPC_ENOMEM;
    }

    g_converter_ops_table[g_converter_ops_count++] = ops;
    return IPC_OK;
}

const ConverterOps *ConverterManager_Find(const char *converter_name)
{
    if (converter_validate_name(converter_name) != IPC_OK) {
        return NULL;
    }

    for (size_t i = 0; i < g_converter_ops_count; ++i) {
        if (strcmp(g_converter_ops_table[i]->name, converter_name) == 0) {
            return g_converter_ops_table[i];
        }
    }

    return NULL;
}

int ConverterManager_Init(ConverterManager *manager,
                          const char *converter_name,
                          const ConverterConfig *config)
{
    const ConverterOps *ops;

    if (manager == NULL || config == NULL || converter_validate_name(converter_name) < 0) {
        return IPC_EINVAL;
    }

    ops = ConverterManager_Find(converter_name);
    if (ops == NULL) {
        return IPC_ESTATE;
    }

    memset(manager, 0, sizeof(*manager));
    manager->ops = ops;
    strncpy(manager->converter_name, converter_name, sizeof(manager->converter_name) - 1U);
    manager->config = *config;

    if (manager->ops->init != NULL) {
        return manager->ops->init(manager);
    }

    return IPC_OK;
}

void ConverterManager_Deinit(ConverterManager *manager)
{
    if (manager == NULL) {
        return;
    }

    if (manager->ops != NULL && manager->ops->deinit != NULL) {
        manager->ops->deinit(manager);
    }
}

int ConverterManager_Convert(ConverterManager *manager,
                             const MediaFrame *src_frame,
                             MediaFrame *dst_frame)
{
    if (manager == NULL || src_frame == NULL || dst_frame == NULL) {
        return IPC_EINVAL;
    }
    if (manager->ops == NULL || manager->ops->convert == NULL) {
        return IPC_ESTATE;
    }

    return manager->ops->convert(manager, src_frame, dst_frame);
}
