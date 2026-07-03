#include "capture_manager.h"

#include <stdio.h>

static int fake_capture_init(CaptureManager *manager)
{
    (void)manager;
    printf("[fake_capture] init\n");
    return 0;
}

static void fake_capture_deinit(CaptureManager *manager)
{
    (void)manager;
    printf("[fake_capture] deinit\n");
}

static int fake_capture_open(CaptureManager *manager)
{
    (void)manager;
    printf("[fake_capture] open\n");
    return 0;
}

static void fake_capture_close(CaptureManager *manager)
{
    (void)manager;
    printf("[fake_capture] close\n");
}

static int fake_capture_start(CaptureManager *manager)
{
    (void)manager;
    printf("[fake_capture] start\n");
    return 0;
}

static void fake_capture_stop(CaptureManager *manager)
{
    (void)manager;
    printf("[fake_capture] stop\n");
}

static int fake_capture_get_frame(CaptureManager *manager, MediaFrame *frame)
{
    (void)manager;
    if (frame == NULL) {
        return -1;
    }

    printf("[fake_capture] get_frame\n");

    frame->width = 640;
    frame->height = 480;
    frame->pixfmt = PIX_FMT_YUYV422;
    frame->data[0] = NULL;
    frame->data[1] = NULL;
    frame->data[2] = NULL;
    frame->linesize[0] = 0;
    frame->linesize[1] = 0;
    frame->linesize[2] = 0;
    frame->size = 640 * 480 * 2;
    frame->pts = 0;

    return 0;
}

static int fake_capture_release_frame(CaptureManager *manager, MediaFrame *frame)
{
    (void)manager;
    (void)frame;
    printf("[fake_capture] release_frame\n");
    return 0;
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
