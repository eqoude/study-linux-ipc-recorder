#include "sdl_display_manager.h"

#include <stddef.h>
#include <string.h>

#define VIEWER_MAX_OPS 16

static const ViewerOps *g_viewer_ops_table[VIEWER_MAX_OPS];
static size_t g_viewer_ops_count;

static int viewer_validate_name(const char *name)
{
    return (name != NULL && name[0] != '\0') ? 0 : -1;
}

int RegisterViewer(const char *name, const ViewerOps *ops)
{
    if (viewer_validate_name(name) < 0 || ops == NULL ||
        ops->name == NULL || strcmp(name, ops->name) != 0) {
        return -1;
    }

    return ViewerManager_Register(ops);
}

int ViewerManager_Register(const ViewerOps *ops)
{
    if (ops == NULL || ops->name == NULL || ops->name[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < g_viewer_ops_count; ++i) {
        if (strcmp(g_viewer_ops_table[i]->name, ops->name) == 0) {
            g_viewer_ops_table[i] = ops;
            return 0;
        }
    }

    if (g_viewer_ops_count >= VIEWER_MAX_OPS) {
        return -1;
    }

    g_viewer_ops_table[g_viewer_ops_count++] = ops;
    return 0;
}

const ViewerOps *ViewerManager_Find(const char *viewer_name)
{
    if (viewer_validate_name(viewer_name) < 0) {
        return NULL;
    }

    for (size_t i = 0; i < g_viewer_ops_count; ++i) {
        if (strcmp(g_viewer_ops_table[i]->name, viewer_name) == 0) {
            return g_viewer_ops_table[i];
        }
    }

    return NULL;
}

int ViewerManager_Init(ViewerManager *manager,
                       const char *viewer_name,
                       const ViewerConfig *config)
{
    const ViewerOps *ops;

    if (manager == NULL || viewer_validate_name(viewer_name) < 0) {
        return -1;
    }

    ops = ViewerManager_Find(viewer_name);
    if (ops == NULL) {
        return -1;
    }

    memset(manager, 0, sizeof(*manager));
    manager->ops = ops;
    strncpy(manager->viewer_name, viewer_name, sizeof(manager->viewer_name) - 1U);

    if (config != NULL) {
        manager->config = *config;
    }

    if (manager->ops->init != NULL) {
        return manager->ops->init(manager);
    }

    return 0;
}

void ViewerManager_Deinit(ViewerManager *manager)
{
    if (manager == NULL) {
        return;
    }

    if (manager->ops != NULL && manager->ops->deinit != NULL) {
        manager->ops->deinit(manager);
    }
}

int ViewerManager_Display(ViewerManager *manager, MediaFrame *frame)
{
    if (manager == NULL || frame == NULL || manager->ops == NULL ||
        manager->ops->display == NULL) {
        return -1;
    }

    return manager->ops->display(manager, frame);
}
