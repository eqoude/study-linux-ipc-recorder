#ifndef IPC_RECORDER_RKNN_YOLO_MEDIA_ADAPTER_H
#define IPC_RECORDER_RKNN_YOLO_MEDIA_ADAPTER_H

#include "media_frame.h"
#include "yolov5.h"

#ifdef __cplusplus
extern "C" {
#endif

int rknn_yolo_infer_media_frame(const MediaFrame *frame,
                                object_detect_result_list *results);

#ifdef __cplusplus
}
#endif

#endif
