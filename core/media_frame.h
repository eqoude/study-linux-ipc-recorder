#ifndef MEDIA_FRAME_H
#define MEDIA_FRAME_H

#include <stdint.h>

typedef enum {
    PIX_FMT_UNKNOWN = 0,
    PIX_FMT_YUYV422,
    PIX_FMT_YUV420P,
} PixelFormat;

typedef struct {
    int width;
    int height;
    PixelFormat pixfmt;

    uint8_t *data[3];
    int linesize[3];

    int size;

    /*
     * Frame timestamp in pipeline time_base.
     * Current normalized video pipeline uses 1/fps, so pts is frame index.
     */
    int64_t pts;
} MediaFrame;

#endif
