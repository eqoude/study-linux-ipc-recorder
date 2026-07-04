#include "frame_processor_manager.h"

#include <stddef.h>
#include <string.h>

#define FRAME_PROCESSOR_MAX_OPS 16

static const FrameProcessorOps *g_frame_processor_ops_table[FRAME_PROCESSOR_MAX_OPS];
static size_t g_frame_processor_ops_count;

static int frame_processor_validate_name(const char *name)
{
    return (name != NULL && name[0] != '\0') ? IPC_OK : IPC_EINVAL;
}

int FrameProcessorManager_Register(const FrameProcessorOps *ops)
{
    if (ops == NULL || ops->name == NULL || ops->name[0] == '\0') {
        return IPC_EINVAL;
    }

    for (size_t i = 0; i < g_frame_processor_ops_count; ++i) {
        if (strcmp(g_frame_processor_ops_table[i]->name, ops->name) == 0) {
            g_frame_processor_ops_table[i] = ops;
            return IPC_OK;
        }
    }

    if (g_frame_processor_ops_count >= FRAME_PROCESSOR_MAX_OPS) {
        return IPC_ENOMEM;
    }

    g_frame_processor_ops_table[g_frame_processor_ops_count++] = ops;
    return IPC_OK;
}

const FrameProcessorOps *FrameProcessorManager_Find(const char *processor_name)
{
    if (frame_processor_validate_name(processor_name) != IPC_OK) {
        return NULL;
    }

    for (size_t i = 0; i < g_frame_processor_ops_count; ++i) {
        if (strcmp(g_frame_processor_ops_table[i]->name, processor_name) == 0) {
            return g_frame_processor_ops_table[i];
        }
    }

    return NULL;
}

int FrameProcessorManager_Init(FrameProcessorManager *manager,
                               const char *processor_name,
                               const FrameProcessorConfig *config)
{
    const FrameProcessorOps *ops;

    if (manager == NULL || frame_processor_validate_name(processor_name) != IPC_OK) {
        return IPC_EINVAL;
    }

    ops = FrameProcessorManager_Find(processor_name);
    if (ops == NULL) {
        return IPC_ESTATE;
    }

    memset(manager, 0, sizeof(*manager));
    manager->ops = ops;
    strncpy(manager->processor_name, processor_name,
            sizeof(manager->processor_name) - 1U);

    if (config != NULL) {
        manager->config = *config;
    }

    if (manager->ops->init != NULL) {
        return manager->ops->init(manager);
    }

    return IPC_OK;
}

void FrameProcessorManager_Deinit(FrameProcessorManager *manager)
{
    if (manager == NULL) {
        return;
    }

    if (manager->ops != NULL && manager->ops->deinit != NULL) {
        manager->ops->deinit(manager);
    }
}

int FrameProcessorManager_Process(FrameProcessorManager *manager,
                                  MediaFrame *in,
                                  MediaFrame *out)
{
    if (manager == NULL || in == NULL || out == NULL) {
        return IPC_EINVAL;
    }
    if (manager->ops == NULL || manager->ops->process == NULL) {
        return IPC_ESTATE;
    }

    return manager->ops->process(manager, in, out);
}
