#ifndef IPC_RECORDER_APP_CONFIG_H
#define IPC_RECORDER_APP_CONFIG_H

#include "ipc_error.h"
#include "media_frame.h"

typedef struct {
    char device_path[128];

    int width;
    int height;
    PixelFormat pixel_format;
    int fps;

    int enable_preview;
    int enable_record;
    int enable_rtsp;
    int enable_processor;
    int enable_segment;

    char output_path[256];
    char rtsp_url[256];

    char capture_name[64];
    char converter_name[64];
    char processor_name[64];
    char viewer_name[64];
    char encoder_name[64];
    char muxer_name[64];

    int max_frames;
    int segment_time;
} AppConfig;

void AppConfig_SetDefault(AppConfig *config);
int AppConfig_ParseArgs(AppConfig *config, int argc, char **argv);
void AppConfig_Print(const AppConfig *config);

#endif
