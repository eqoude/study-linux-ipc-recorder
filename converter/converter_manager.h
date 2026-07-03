#ifndef IPC_RECORDER_CONVERTER_MANAGER_H
#define IPC_RECORDER_CONVERTER_MANAGER_H

#include "../core/media_frame.h"

typedef struct ConverterManager ConverterManager;
typedef struct ConverterOps ConverterOps;

typedef struct {
    int src_width;
    int src_height;
    PixelFormat src_format;
    int dst_width;
    int dst_height;
    PixelFormat dst_format;
} ConverterConfig;

struct ConverterOps {
    const char *name;
    int (*init)(ConverterManager *manager);
    void (*deinit)(ConverterManager *manager);
    int (*convert)(ConverterManager *manager,
                   const MediaFrame *src_frame,
                   MediaFrame *dst_frame);
};

struct ConverterManager {
    const ConverterOps *ops;
    void *priv;

    char converter_name[64];
    ConverterConfig config;
};

int ConverterManager_Register(const ConverterOps *ops);
const ConverterOps *ConverterManager_Find(const char *converter_name);

int ConverterManager_Init(ConverterManager *manager,
                          const char *converter_name,
                          const ConverterConfig *config);
void ConverterManager_Deinit(ConverterManager *manager);
int ConverterManager_Convert(ConverterManager *manager,
                             const MediaFrame *src_frame,
                             MediaFrame *dst_frame);

#endif
