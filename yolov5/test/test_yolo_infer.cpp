#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_utils.h"
#include "image_utils.h"
#include "rknn_yolo_wrapper.h"
#include "postprocess.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
        printf("Usage: %s <model_path> <image_path>\n", argv[0]);
        return 1;
    }

    const char *model_path = argv[1];
    const char *image_path = argv[2];
    image_buffer_t image;
    object_detect_result_list results;
    int ret;

    memset(&image, 0, sizeof(image));
    memset(&results, 0, sizeof(results));

    ret = read_image(image_path, &image);
    if (ret != 0) {
        printf("read_image failed: %s ret=%d\n", image_path, ret);
        return 1;
    }

    ret = rknn_yolo_init(model_path);
    if (ret != 0) {
        printf("rknn_yolo_init failed: %s ret=%d\n", model_path, ret);
        free(image.virt_addr);
        return 1;
    }

    ret = rknn_yolo_infer(&image, &results);
    if (ret != 0) {
        printf("rknn_yolo_infer failed: ret=%d\n", ret);
        rknn_yolo_release();
        free(image.virt_addr);
        return 1;
    }

    printf("detect count: %d\n", results.count);
    for (int i = 0; i < results.count; ++i) {
        object_detect_result *result = &results.results[i];
        printf("%s bbox=(%d,%d,%d,%d) confidence=%.3f\n",
               coco_cls_to_name(result->cls_id),
               result->box.left,
               result->box.top,
               result->box.right,
               result->box.bottom,
               result->prop);
    }

    rknn_yolo_release();
    free(image.virt_addr);
    return 0;
}
