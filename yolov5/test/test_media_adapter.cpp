#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media_frame.h"
#include "rknn_yolo_media_adapter.h"
#include "rknn_yolo_wrapper.h"

static const char *pixel_format_to_string(PixelFormat pixfmt)
{
    switch (pixfmt) {
    case PIX_FMT_YUYV422:
        return "PIX_FMT_YUYV422";
    case PIX_FMT_YUV420P:
        return "PIX_FMT_YUV420P";
    case PIX_FMT_UNKNOWN:
    default:
        return "PIX_FMT_UNKNOWN";
    }
}

static void fill_test_yuv420p(MediaFrame *frame)
{
    for (int row = 0; row < frame->height; ++row) {
        unsigned char *dst_y = frame->data[0] + row * frame->linesize[0];
        memset(dst_y, 128, (size_t)frame->width);
    }

    for (int row = 0; row < frame->height / 2; ++row) {
        unsigned char *dst_u = frame->data[1] + row * frame->linesize[1];
        unsigned char *dst_v = frame->data[2] + row * frame->linesize[2];
        memset(dst_u, 128, (size_t)(frame->width / 2));
        memset(dst_v, 128, (size_t)(frame->width / 2));
    }
}

int main(int argc, char **argv)
{
    const int width = 640;
    const int height = 480;
    MediaFrame frame;
    object_detect_result_list results;
    int y_size = width * height;
    int uv_size = width * height / 4;
    int nv12_size = width * height * 3 / 2;
    int ret;

    if (argc != 2) {
        printf("Usage: %s <model_path>\n", argv[0]);
        return 1;
    }

    memset(&frame, 0, sizeof(frame));
    memset(&results, 0, sizeof(results));

    frame.width = width;
    frame.height = height;
    frame.pixfmt = PIX_FMT_YUV420P;
    frame.linesize[0] = width;
    frame.linesize[1] = width / 2;
    frame.linesize[2] = width / 2;
    frame.size = width * height * 3 / 2;

    frame.data[0] = (uint8_t *)malloc((size_t)y_size);
    frame.data[1] = (uint8_t *)malloc((size_t)uv_size);
    frame.data[2] = (uint8_t *)malloc((size_t)uv_size);
    if (frame.data[0] == NULL || frame.data[1] == NULL ||
        frame.data[2] == NULL) {
        printf("malloc MediaFrame planes failed\n");
        free(frame.data[0]);
        free(frame.data[1]);
        free(frame.data[2]);
        return 1;
    }

    fill_test_yuv420p(&frame);

    printf("[test_media_adapter] model_path=%s\n", argv[1]);
    printf("[test_media_adapter] input width=%d height=%d pixfmt=%s(%d)\n",
           frame.width,
           frame.height,
           pixel_format_to_string(frame.pixfmt),
           frame.pixfmt);
    printf("[test_media_adapter] linesize Y=%d U=%d V=%d\n",
           frame.linesize[0],
           frame.linesize[1],
           frame.linesize[2]);
    printf("[test_media_adapter] expected NV12 buffer size=%d\n", nv12_size);

    ret = rknn_yolo_init(argv[1]);
    printf("[test_media_adapter] rknn_yolo_init ret=%d\n", ret);
    if (ret != 0) {
        printf("rknn_yolo_init failed: ret=%d\n", ret);
        free(frame.data[0]);
        free(frame.data[1]);
        free(frame.data[2]);
        return 1;
    }

    ret = rknn_yolo_infer_media_frame(&frame, &results);
    printf("[test_media_adapter] rknn_yolo_infer_media_frame ret=%d\n", ret);
    if (ret != 0) {
        printf("rknn_yolo_infer_media_frame failed: ret=%d\n", ret);
        rknn_yolo_release();
        free(frame.data[0]);
        free(frame.data[1]);
        free(frame.data[2]);
        return 1;
    }

    printf("[test_media_adapter] detect count=%d\n", results.count);
    for (int i = 0; i < results.count; ++i) {
        object_detect_result *result = &results.results[i];
        printf("[test_media_adapter] result[%d] class_id=%d bbox=(%d,%d,%d,%d) confidence=%.3f\n",
               i,
               result->cls_id,
               result->box.left,
               result->box.top,
               result->box.right,
               result->box.bottom,
               result->prop);
    }

    rknn_yolo_release();
    free(frame.data[0]);
    free(frame.data[1]);
    free(frame.data[2]);
    return 0;
}
