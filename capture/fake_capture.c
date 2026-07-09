#include "capture_manager.h"

#include "ipc_log.h"

#include <stdio.h>

static int fake_capture_init(CaptureManager *manager)
{
    if (manager == NULL) {
        return IPC_EINVAL;
    }

    IPC_LOGI("[fake_capture] init");
    return IPC_OK;
}

static void fake_capture_deinit(CaptureManager *manager)
{
    (void)manager;
    IPC_LOGI("[fake_capture] deinit");
}

static int fake_capture_open(CaptureManager *manager)
{
    if (manager == NULL) {
        return IPC_EINVAL;
    }

    IPC_LOGI("[fake_capture] open");
    return IPC_OK;
}

static void fake_capture_close(CaptureManager *manager)
{
    (void)manager;
    IPC_LOGI("[fake_capture] close");
}

static int fake_capture_start(CaptureManager *manager)
{
    if (manager == NULL) {
        return IPC_EINVAL;
    }

    IPC_LOGI("[fake_capture] start");
    return IPC_OK;
}

static void fake_capture_stop(CaptureManager *manager)
{
    (void)manager;
    IPC_LOGI("[fake_capture] stop");
}

static int fake_capture_get_frame(CaptureManager *manager, MediaFrame *frame)
{
    if (manager == NULL || frame == NULL) {
        return IPC_EINVAL;
    }

    IPC_LOGD("[fake_capture] get_frame");

    frame->width = manager->config.width;
    frame->height = manager->config.height;
    frame->pixfmt = manager->config.pixel_format;
    frame->data[0] = NULL;
    frame->data[1] = NULL;
    frame->data[2] = NULL;
    frame->linesize[0] = 0;
    frame->linesize[1] = 0;
    frame->linesize[2] = 0;
    frame->size = manager->config.width * manager->config.height * 2;
    frame->pts = 0;

    return IPC_OK;
}

static int fake_capture_release_frame(CaptureManager *manager, MediaFrame *frame)
{
    if (manager == NULL || frame == NULL) {
        return IPC_EINVAL;
    }

    IPC_LOGD("[fake_capture] release_frame");
    return IPC_OK;
}

const CaptureOps g_fake_capture_ops = {
    .name = "fake_capture",
    .init = fake_capture_init,
    .deinit = fake_capture_deinit,
    .open = fake_capture_open,
    .close = fake_capture_close,
    .start = fake_capture_start,
    .stop = fake_capture_stop,
    .get_frame = fake_capture_get_frame,
    .release_frame = fake_capture_release_frame,
};
