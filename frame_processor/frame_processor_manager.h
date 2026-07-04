#ifndef IPC_RECORDER_FRAME_PROCESSOR_MANAGER_H
#define IPC_RECORDER_FRAME_PROCESSOR_MANAGER_H

#include "../core/ipc_error.h"
#include "../core/media_frame.h"

typedef struct FrameProcessorManager FrameProcessorManager;
typedef struct FrameProcessorOps FrameProcessorOps;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} FrameProcessorConfig;

struct FrameProcessorOps {
    const char *name;

    int (*init)(void *manager);
    int (*process)(void *manager,
                   MediaFrame *in,
                   MediaFrame *out);

    void (*deinit)(void *manager);
};

struct FrameProcessorManager {
    const FrameProcessorOps *ops;
    void *priv;

    char processor_name[64];
    FrameProcessorConfig config;
};

int FrameProcessorManager_Register(const FrameProcessorOps *ops);
const FrameProcessorOps *FrameProcessorManager_Find(const char *processor_name);

int FrameProcessorManager_Init(FrameProcessorManager *manager,
                               const char *processor_name,
                               const FrameProcessorConfig *config);
void FrameProcessorManager_Deinit(FrameProcessorManager *manager);
int FrameProcessorManager_Process(FrameProcessorManager *manager,
                                  MediaFrame *in,
                                  MediaFrame *out);

#endif
