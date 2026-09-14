#ifndef IPC_RECORDER_RKNN_YOLO_WRAPPER_H
#define IPC_RECORDER_RKNN_YOLO_WRAPPER_H

#include "yolov5.h"

#ifdef __cplusplus
extern "C" {
#endif

int rknn_yolo_init(const char *model_path);
int rknn_yolo_infer(image_buffer_t *image,
                    object_detect_result_list *results);
int rknn_yolo_release(void);

#ifdef __cplusplus
}
#endif

#endif
