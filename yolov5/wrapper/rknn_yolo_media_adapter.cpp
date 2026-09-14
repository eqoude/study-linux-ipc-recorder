#include "rknn_yolo_media_adapter.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "rknn_yolo_wrapper.h"

static int media_frame_yuv420p_to_nv12(const MediaFrame *frame,
                                       unsigned char *nv12_buffer)
{
    const int width = frame->width;
    const int height = frame->height;
    unsigned char *dst_y = nv12_buffer;
    unsigned char *dst_uv = nv12_buffer + width * height;

    for (int row = 0; row < height; ++row) {
        const uint8_t *src_y = frame->data[0] + row * frame->linesize[0];
        memcpy(dst_y + row * width, src_y, (size_t)width);
    }

    for (int row = 0; row < height / 2; ++row) {
        const uint8_t *src_u = frame->data[1] + row * frame->linesize[1];
        const uint8_t *src_v = frame->data[2] + row * frame->linesize[2];
        unsigned char *dst_row = dst_uv + row * width;

        for (int col = 0; col < width / 2; ++col) {
            dst_row[col * 2] = src_u[col];
            dst_row[col * 2 + 1] = src_v[col];
        }
    }

    return 0;
}

int rknn_yolo_infer_media_frame(const MediaFrame *frame,
                                object_detect_result_list *results)
{
    int width;
    int height;
    int nv12_size;
    unsigned char *nv12_buffer;
    image_buffer_t image;
    int ret;

    if (frame == NULL || results == NULL) {
        return -1;
    }

    if (frame->pixfmt != PIX_FMT_YUV420P) {
        return -1;
    }

    if (frame->data[0] == NULL || frame->data[1] == NULL ||
        frame->data[2] == NULL) {
        return -1;
    }

    width = frame->width;
    height = frame->height;
    if (width <= 0 || height <= 0 || (width % 2) != 0 ||
        (height % 2) != 0) {
        return -1;
    }

    if (frame->linesize[0] < width || frame->linesize[1] < width / 2 ||
        frame->linesize[2] < width / 2) {
        return -1;
    }

    nv12_size = width * height * 3 / 2;
    nv12_buffer = (unsigned char *)malloc((size_t)nv12_size);
    if (nv12_buffer == NULL) {
        return -1;
    }

    ret = media_frame_yuv420p_to_nv12(frame, nv12_buffer);
    if (ret != 0) {
        free(nv12_buffer);
        return ret;
    }

    memset(&image, 0, sizeof(image));
    image.width = width;
    image.height = height;
    image.width_stride = width;
    image.height_stride = height;
    image.format = IMAGE_FORMAT_YUV420SP_NV12;
    image.virt_addr = nv12_buffer;
    image.size = nv12_size;
    image.fd = 0;

    ret = rknn_yolo_infer(&image, results);

    free(nv12_buffer);
    return ret;
}
