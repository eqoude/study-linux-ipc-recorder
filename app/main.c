#include <stdio.h>

#include "app_config.h"
#include "app_pipeline.h"
#include "signal_handler.h"

int main(int argc, char *argv[])
{
    AppConfig app_config;
    AppPipeline pipeline;
    int parse_ret;
    int ret;

    AppConfig_SetDefault(&app_config);
    parse_ret = AppConfig_ParseArgs(&app_config, argc, argv);
    if (parse_ret == IPC_EOF) {
        return 0;
    }
    if (parse_ret != IPC_OK) {
        fprintf(stderr,
                "[main] AppConfig_ParseArgs failed: %s (%d)\n",
                IpcError_ToString(parse_ret),
                parse_ret);
        return 1;
    }

    if (!app_config.enable_preview &&
        !app_config.enable_record &&
        !app_config.enable_rtsp) {
        fprintf(stderr, "no output enabled, use --preview, --record <path>, and/or --rtsp <url>\n");
        return 1;
    }

    AppConfig_Print(&app_config);

    if (Signal_Init() != 0) {
        fprintf(stderr, "[main] Signal_Init failed\n");
        return 1;
    }

    ret = AppPipeline_Init(&pipeline, &app_config);
    if (ret != IPC_OK) {
        AppPipeline_Deinit(&pipeline);
        return ret < 0 ? 1 : 0;
    }

    ret = AppPipeline_Start(&pipeline);
    if (ret != IPC_OK) {
        AppPipeline_Deinit(&pipeline);
        return ret < 0 ? 1 : 0;
    }

    ret = Signal_Wait();
    if (ret != IPC_OK) {
        fprintf(stderr,
                "[main] Signal_Wait failed: %s (%d)\n",
                IpcError_ToString(ret),
                ret);
    }

    fprintf(stderr, "[main] stop signal received, stopping pipeline...\n");
    AppPipeline_Stop(&pipeline);

    ret = AppPipeline_Wait(&pipeline);

    AppPipeline_Deinit(&pipeline);

    return ret < 0 ? 1 : 0;
}
