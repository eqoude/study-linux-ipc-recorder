#include "capture_manager.h"

#include <stddef.h>
#include <string.h>

#define CAPTURE_MAX_OPS 16

static const CaptureOps *g_capture_ops_table[CAPTURE_MAX_OPS];
static size_t g_capture_ops_count;

static int capture_validate_name(const char *name)
{
    return (name != NULL && name[0] != '\0') ? 0 : -1;
}

int CaptureManager_Register(const CaptureOps *ops)
{
    if (ops == NULL || ops->name == NULL || ops->name[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < g_capture_ops_count; ++i) {
        if (strcmp(g_capture_ops_table[i]->name, ops->name) == 0) {
            g_capture_ops_table[i] = ops;
            return 0;
        }
    }

    if (g_capture_ops_count >= CAPTURE_MAX_OPS) {
        return -1;
    }

    g_capture_ops_table[g_capture_ops_count++] = ops;
    return 0;
}

const CaptureOps *CaptureManager_Find(const char *capture_name)
{
    if (capture_validate_name(capture_name) < 0) {
        return NULL;
    }

    for (size_t i = 0; i < g_capture_ops_count; ++i) {
        if (strcmp(g_capture_ops_table[i]->name, capture_name) == 0) {
            return g_capture_ops_table[i];
        }
    }

    return NULL;
}

int CaptureManager_Init(CaptureManager *manager,
                        const char *capture_name,
                        const CaptureConfig *config)
{
    const CaptureOps *ops;

    if (manager == NULL || capture_validate_name(capture_name) < 0 ||
        config == NULL || config->device_path == NULL ||
        config->device_path[0] == '\0' || config->width <= 0 ||
        config->height <= 0 || config->fps <= 0) {
        return -1;
    }

    ops = CaptureManager_Find(capture_name);
    if (ops == NULL) {
        return -1;
    }

    memset(manager, 0, sizeof(*manager));
    manager->ops = ops;
    strncpy(manager->capture_name, capture_name, sizeof(manager->capture_name) - 1U);
    manager->config = *config;
    strncpy(manager->device_path, config->device_path, sizeof(manager->device_path) - 1U);
    manager->width = config->width;
    manager->height = config->height;
    manager->pixel_format = config->pixel_format;
    manager->fps = config->fps;
    manager->state = CAPTURE_STATE_IDLE;

    if (manager->ops->init != NULL) {
        return manager->ops->init(manager);
    }

    return 0;
}

void CaptureManager_Deinit(CaptureManager *manager)
{
    if (manager == NULL) {
        return;
    }

    if (manager->ops != NULL && manager->ops->deinit != NULL) {
        manager->ops->deinit(manager);
    }
}

int CaptureManager_Open(CaptureManager *manager)
{
    if (manager == NULL || manager->ops == NULL || manager->ops->open == NULL) {
        return -1;
    }

    return manager->ops->open(manager);
}

void CaptureManager_Close(CaptureManager *manager)
{
    if (manager == NULL || manager->ops == NULL || manager->ops->close == NULL) {
        return;
    }

    manager->ops->close(manager);
}

int CaptureManager_Start(CaptureManager *manager)
{
    if (manager == NULL || manager->ops == NULL || manager->ops->start == NULL) {
        return -1;
    }

    return manager->ops->start(manager);
}

void CaptureManager_Stop(CaptureManager *manager)
{
    if (manager == NULL || manager->ops == NULL || manager->ops->stop == NULL) {
        return;
    }

    manager->ops->stop(manager);
}

int CaptureManager_GetFrame(CaptureManager *manager, MediaFrame *frame)
{
    if (manager == NULL || frame == NULL || manager->ops == NULL ||
        manager->ops->get_frame == NULL) {
        return -1;
    }

    return manager->ops->get_frame(manager, frame);
}

int CaptureManager_ReleaseFrame(CaptureManager *manager, MediaFrame *frame)
{
    if (manager == NULL || frame == NULL || manager->ops == NULL ||
        manager->ops->release_frame == NULL) {
        return 0;
    }

    return manager->ops->release_frame(manager, frame);
}
