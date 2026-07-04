#include <stdio.h>

#include "app_config.h"
#include "app_pipeline.h"

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

    ret = AppPipeline_Init(&pipeline, &app_config);
    if (ret == IPC_OK) {
        ret = AppPipeline_Run(&pipeline);
    }
    AppPipeline_Deinit(&pipeline);

    return ret < 0 ? 1 : 0;
}
