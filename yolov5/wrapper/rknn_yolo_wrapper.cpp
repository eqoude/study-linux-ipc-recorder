#include "rknn_yolo_wrapper.h"

#include <string.h>

#include "yolov5.h"

static rknn_app_context_t g_yolo_ctx;
static int g_yolo_initialized = 0;

int rknn_yolo_init(const char *model_path)
{
    int ret;

    if (model_path == nullptr || model_path[0] == '\0') {
        return -1;
    }

    if (g_yolo_initialized) {
        return 0;
    }

    memset(&g_yolo_ctx, 0, sizeof(g_yolo_ctx));

    ret = init_post_process();
    if (ret != 0) {
        return ret;
    }

    ret = init_yolov5_model(model_path, &g_yolo_ctx);
    if (ret != 0) {
        deinit_post_process();
        memset(&g_yolo_ctx, 0, sizeof(g_yolo_ctx));
        return ret;
    }

    g_yolo_initialized = 1;
    return 0;
}

int rknn_yolo_infer(image_buffer_t *image,
                    object_detect_result_list *results)
{
    if (!g_yolo_initialized || image == nullptr || results == nullptr) {
        return -1;
    }

    return inference_yolov5_model(&g_yolo_ctx, image, results);
}

int rknn_yolo_release(void)
{
    int ret = 0;

    if (!g_yolo_initialized) {
        return 0;
    }

    ret = release_yolov5_model(&g_yolo_ctx);
    deinit_post_process();
    memset(&g_yolo_ctx, 0, sizeof(g_yolo_ctx));
    g_yolo_initialized = 0;

    return ret;
}
