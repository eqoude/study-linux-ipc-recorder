#include "frame_sink_manager.h"

#include <stddef.h>
#include <string.h>

#define FRAME_SINK_MAX_OPS 16

static const FrameSinkOps *g_frame_sink_ops_table[FRAME_SINK_MAX_OPS];
static size_t g_frame_sink_ops_count;

static int frame_sink_validate_name(const char *name)
{
    return (name != NULL && name[0] != '\0') ? IPC_OK : IPC_EINVAL;
}

int FrameSink_Register(const FrameSinkOps *ops)
{
    if (ops == NULL || ops->name == NULL || ops->name[0] == '\0' ||
        ops->write == NULL) {
        return IPC_EINVAL;
    }

    for (size_t i = 0; i < g_frame_sink_ops_count; ++i) {
        if (strcmp(g_frame_sink_ops_table[i]->name, ops->name) == 0) {
            g_frame_sink_ops_table[i] = ops;
            return IPC_OK;
        }
    }

    if (g_frame_sink_ops_count >= FRAME_SINK_MAX_OPS) {
        return IPC_ENOMEM;
    }

    g_frame_sink_ops_table[g_frame_sink_ops_count++] = ops;
    return IPC_OK;
}

const FrameSinkOps *FrameSink_Find(const char *name)
{
    if (frame_sink_validate_name(name) != IPC_OK) {
        return NULL;
    }

    for (size_t i = 0; i < g_frame_sink_ops_count; ++i) {
        if (strcmp(g_frame_sink_ops_table[i]->name, name) == 0) {
            return g_frame_sink_ops_table[i];
        }
    }

    return NULL;
}
