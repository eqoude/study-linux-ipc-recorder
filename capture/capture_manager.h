#ifndef IPC_RECORDER_CAPTURE_MANAGER_H
#define IPC_RECORDER_CAPTURE_MANAGER_H

#include "../core/ipc_error.h"
#include "../core/media_frame.h"

typedef struct CaptureManager CaptureManager;
typedef struct CaptureOps CaptureOps;

typedef enum {
    CAPTURE_STATE_IDLE = 0,
    CAPTURE_STATE_READY,
    CAPTURE_STATE_RUNNING,
    CAPTURE_STATE_STOPPED,
} CaptureState;

typedef struct {
    const char *device_path;
    int width;
    int height;
    PixelFormat pixel_format;
    int fps;
} CaptureConfig;

struct CaptureOps {
    const char *name;
    int (*init)(CaptureManager *manager);
    void (*deinit)(CaptureManager *manager);
    int (*open)(CaptureManager *manager);
    void (*close)(CaptureManager *manager);
    int (*start)(CaptureManager *manager);
    void (*stop)(CaptureManager *manager);
    int (*get_frame)(CaptureManager *manager, MediaFrame *frame);
    int (*release_frame)(CaptureManager *manager, MediaFrame *frame);
};

struct CaptureManager {
    const CaptureOps *ops;
    void *priv;

    char capture_name[64];
    CaptureConfig config;
    char device_path[128];
    int width;
    int height;
    PixelFormat pixel_format;
    int fps;
    CaptureState state;
};

int CaptureManager_Register(const CaptureOps *ops);
const CaptureOps *CaptureManager_Find(const char *capture_name);

int CaptureManager_Init(CaptureManager *manager,
                        const char *capture_name,
                        const CaptureConfig *config);
void CaptureManager_Deinit(CaptureManager *manager);
int CaptureManager_Open(CaptureManager *manager);
void CaptureManager_Close(CaptureManager *manager);
int CaptureManager_Start(CaptureManager *manager);
void CaptureManager_Stop(CaptureManager *manager);
int CaptureManager_GetFrame(CaptureManager *manager, MediaFrame *frame);
int CaptureManager_ReleaseFrame(CaptureManager *manager, MediaFrame *frame);

#endif
