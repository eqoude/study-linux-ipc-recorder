#ifndef IPC_RECORDER_VIEWER_SDL_DISPLAY_MANAGER_H
#define IPC_RECORDER_VIEWER_SDL_DISPLAY_MANAGER_H

#include "../core/media_frame.h"

typedef struct ViewerManager ViewerManager;
typedef struct ViewerOps ViewerOps;

typedef struct {
    int width;
    int height;
    const char *title;
} ViewerConfig;

struct ViewerOps {
    const char *name;

    int (*init)(void *ctx);
    int (*display)(void *ctx, MediaFrame *frame);
    void (*deinit)(void *ctx);
};

struct ViewerManager {
    const ViewerOps *ops;
    void *priv;

    char viewer_name[64];
    ViewerConfig config;
};

int RegisterViewer(const char *name, const ViewerOps *ops);
int ViewerManager_Register(const ViewerOps *ops);
const ViewerOps *ViewerManager_Find(const char *viewer_name);

int ViewerManager_Init(ViewerManager *manager,
                       const char *viewer_name,
                       const ViewerConfig *config);
void ViewerManager_Deinit(ViewerManager *manager);
int ViewerManager_Display(ViewerManager *manager, MediaFrame *frame);

#endif
