#include "converter_manager.h"

#include <stdio.h>

static int fake_converter_init(ConverterManager *manager)
{
    if (manager == NULL) {
        return IPC_EINVAL;
    }

    printf("[fake_converter] init\n");
    return IPC_OK;
}

static void fake_converter_deinit(ConverterManager *manager)
{
    (void)manager;
    printf("[fake_converter] deinit\n");
}

static int fake_converter_convert(ConverterManager *manager,
                                  const MediaFrame *src_frame,
                                  MediaFrame *dst_frame)
{
    if (manager == NULL || src_frame == NULL || dst_frame == NULL) {
        return IPC_EINVAL;
    }

    printf("[fake_converter] convert\n");

    *dst_frame = *src_frame;
    dst_frame->pixfmt = PIX_FMT_YUV420P;
    dst_frame->size = src_frame->width * src_frame->height * 3 / 2;

    return IPC_OK;
}

const ConverterOps g_fake_converter_ops = {
    .name = "fake_converter",
    .init = fake_converter_init,
    .deinit = fake_converter_deinit,
    .convert = fake_converter_convert,
};
