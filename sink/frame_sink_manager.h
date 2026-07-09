#ifndef IPC_RECORDER_FRAME_SINK_MANAGER_H
#define IPC_RECORDER_FRAME_SINK_MANAGER_H

#include "../core/ipc_error.h"
#include "../core/media_frame.h"

typedef struct FrameSinkOps {
    const char *name;

    int (*init)(void **ctx, const void *config);
    int (*write)(void *ctx, const MediaFrame *frame);
    void (*deinit)(void *ctx);
} FrameSinkOps;

int FrameSink_Register(const FrameSinkOps *ops);
const FrameSinkOps *FrameSink_Find(const char *name);

#endif
