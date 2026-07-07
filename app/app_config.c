#include "app_config.h"

#include "ipc_error.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void app_config_copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0 || src == NULL) {
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static int app_config_parse_int(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    if (text == NULL || value == NULL || text[0] == '\0') {
        return IPC_EINVAL;
    }

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < 0 || parsed > 2147483647L) {
        return IPC_EINVAL;
    }

    *value = (int)parsed;
    return IPC_OK;
}

static int app_config_require_value(int index, int argc, const char *option)
{
    if (index + 1 >= argc) {
        fprintf(stderr, "%s requires a value\n", option);
        return IPC_EINVAL;
    }

    return IPC_OK;
}

static void app_config_print_help(const char *program)
{
    printf("Usage: %s [options]\n", program != NULL ? program : "ipc_recorder");
    printf("\n");
    printf("Options:\n");
    printf("  --device /dev/videoX       Camera device path\n");
    printf("  --width 640                Capture width\n");
    printf("  --height 480               Capture height\n");
    printf("  --fps 30                   Capture frame rate\n");
    printf("  --preview                  Enable SDL realtime preview\n");
    printf("  --record output/file.mp4   Enable MP4 recording\n");
    printf("  --rtsp rtsp://host/live    Enable RTSP streaming\n");
    printf("  --processor osd            Enable frame processor plugin\n");
    printf("  --no-processor             Disable frame processor\n");
    printf("  --frames 300               Max frames to process, 0 means unlimited\n");
    printf("  --help                     Show this help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s --device /dev/video0 --preview\n", program);
    printf("  %s --device /dev/video0 --record output/record.mp4\n", program);
    printf("  %s --device /dev/video0 --rtsp rtsp://127.0.0.1:8554/live\n", program);
    printf("  %s --device /dev/video2 --preview --record output/test.mp4\n", program);
    printf("  %s --device /dev/video0 --width 1280 --height 720 --fps 30 --preview\n", program);
}

void AppConfig_SetDefault(AppConfig *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));

    app_config_copy_string(config->device_path,
                           sizeof(config->device_path),
                           "/dev/video0");
    config->width = 640;
    config->height = 480;
    config->pixel_format = PIX_FMT_YUYV422;
    config->fps = 30;

    config->enable_preview = 0;
    config->enable_record = 0;
    config->enable_rtsp = 0;
    config->enable_processor = 0;

    app_config_copy_string(config->output_path,
                           sizeof(config->output_path),
                           "output/record.mp4");
    app_config_copy_string(config->rtsp_url,
                           sizeof(config->rtsp_url),
                           "rtsp://127.0.0.1:8554/live");   // 

    app_config_copy_string(config->capture_name,
                           sizeof(config->capture_name),
                           "v4l2");
    app_config_copy_string(config->converter_name,
                           sizeof(config->converter_name),
                           "yuyv_to_yuv420");
    app_config_copy_string(config->processor_name,
                           sizeof(config->processor_name),
                           "osd");
    app_config_copy_string(config->viewer_name,
                           sizeof(config->viewer_name),
                           "sdl");
    app_config_copy_string(config->encoder_name,
                           sizeof(config->encoder_name),
                           "h264_ffmpeg");
    app_config_copy_string(config->muxer_name,
                           sizeof(config->muxer_name),
                           "mp4");

    config->max_frames = 0;
}

int AppConfig_ParseArgs(AppConfig *config, int argc, char **argv)
{
    if (config == NULL) {
        return IPC_EINVAL;
    }

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0) {
            app_config_print_help(argv[0]);
            return IPC_EOF;
        } else if (strcmp(arg, "--device") == 0) {
            if (app_config_require_value(i, argc, arg) != IPC_OK) {
                return IPC_EINVAL;
            }
            app_config_copy_string(config->device_path,
                                   sizeof(config->device_path),
                                   argv[++i]);
        } else if (strcmp(arg, "--width") == 0) {
            if (app_config_require_value(i, argc, arg) != IPC_OK ||
                app_config_parse_int(argv[++i], &config->width) != IPC_OK ||
                config->width <= 0) {
                fprintf(stderr, "invalid --width\n");
                return IPC_EINVAL;
            }
        } else if (strcmp(arg, "--height") == 0) {
            if (app_config_require_value(i, argc, arg) != IPC_OK ||
                app_config_parse_int(argv[++i], &config->height) != IPC_OK ||
                config->height <= 0) {
                fprintf(stderr, "invalid --height\n");
                return IPC_EINVAL;
            }
        } else if (strcmp(arg, "--fps") == 0) {
            if (app_config_require_value(i, argc, arg) != IPC_OK ||
                app_config_parse_int(argv[++i], &config->fps) != IPC_OK ||
                config->fps <= 0) {
                fprintf(stderr, "invalid --fps\n");
                return IPC_EINVAL;
            }
        } else if (strcmp(arg, "--preview") == 0) {
            config->enable_preview = 1;
        } else if (strcmp(arg, "--record") == 0) {
            if (app_config_require_value(i, argc, arg) != IPC_OK) {
                return IPC_EINVAL;
            }
            config->enable_record = 1;
            app_config_copy_string(config->output_path,
                                   sizeof(config->output_path),
                                   argv[++i]);
        } else if (strcmp(arg, "--rtsp") == 0) {
            if (app_config_require_value(i, argc, arg) != IPC_OK) {
                return IPC_EINVAL;
            }
            config->enable_rtsp = 1;
            app_config_copy_string(config->rtsp_url,
                                   sizeof(config->rtsp_url),
                                   argv[++i]);
        } else if (strcmp(arg, "--processor") == 0) {
            if (app_config_require_value(i, argc, arg) != IPC_OK) {
                return IPC_EINVAL;
            }
            config->enable_processor = 1;
            app_config_copy_string(config->processor_name,
                                   sizeof(config->processor_name),
                                   argv[++i]);
        } else if (strcmp(arg, "--no-processor") == 0) {
            config->enable_processor = 0;
        } else if (strcmp(arg, "--frames") == 0) {
            if (app_config_require_value(i, argc, arg) != IPC_OK ||
                app_config_parse_int(argv[++i], &config->max_frames) != IPC_OK) {
                fprintf(stderr, "invalid --frames\n");
                return IPC_EINVAL;
            }
        } else {
            fprintf(stderr, "unknown option: %s\n", arg);
            app_config_print_help(argv[0]);
            return IPC_EINVAL;
        }
    }

    return IPC_OK;
}

void AppConfig_Print(const AppConfig *config)
{
    if (config == NULL) {
        return;
    }

    printf("AppConfig:\n");
    printf("  device_path      : %s\n", config->device_path);
    printf("  width            : %d\n", config->width);
    printf("  height           : %d\n", config->height);
    printf("  pixel_format     : %d\n", config->pixel_format);
    printf("  fps              : %d\n", config->fps);
    printf("  enable_preview   : %d\n", config->enable_preview);
    printf("  enable_record    : %d\n", config->enable_record);
    printf("  enable_rtsp      : %d\n", config->enable_rtsp);
    printf("  enable_processor : %d\n", config->enable_processor);
    printf("  output_path      : %s\n", config->output_path);
    printf("  rtsp_url         : %s\n", config->rtsp_url);
    printf("  capture_name     : %s\n", config->capture_name);
    printf("  converter_name   : %s\n", config->converter_name);
    printf("  processor_name   : %s\n", config->processor_name);
    printf("  viewer_name      : %s\n", config->viewer_name);
    printf("  encoder_name     : %s\n", config->encoder_name);
    printf("  muxer_name       : %s\n", config->muxer_name);
    printf("  max_frames       : %d\n", config->max_frames);
}
