#include "converter_manager.h"

#include <stddef.h>
#include <string.h>

#define CONVERTER_MAX_OPS 16

static const ConverterOps *g_converter_ops_table[CONVERTER_MAX_OPS];
static size_t g_converter_ops_count;

static int converter_validate_name(const char *name)
{
    return (name != NULL && name[0] != '\0') ? 0 : -1;
}

int ConverterManager_Register(const ConverterOps *ops)
{
    if (ops == NULL || ops->name == NULL || ops->name[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < g_converter_ops_count; ++i) {
        if (strcmp(g_converter_ops_table[i]->name, ops->name) == 0) {
            g_converter_ops_table[i] = ops;
            return 0;
        }
    }

    if (g_converter_ops_count >= CONVERTER_MAX_OPS) {
        return -1;
    }

    g_converter_ops_table[g_converter_ops_count++] = ops;
    return 0;
}

const ConverterOps *ConverterManager_Find(const char *converter_name)
{
    if (converter_validate_name(converter_name) < 0) {
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
        return -1;
    }

    ops = ConverterManager_Find(converter_name);
    if (ops == NULL) {
        return -1;
    }

    memset(manager, 0, sizeof(*manager));
    manager->ops = ops;
    strncpy(manager->converter_name, converter_name, sizeof(manager->converter_name) - 1U);
    manager->config = *config;

    if (manager->ops->init != NULL) {
        return manager->ops->init(manager);
    }

    return 0;
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
    if (manager == NULL || src_frame == NULL || dst_frame == NULL ||
        manager->ops == NULL || manager->ops->convert == NULL) {
        return -1;
    }

    return manager->ops->convert(manager, src_frame, dst_frame);
}
