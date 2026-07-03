#ifndef IPC_RECORDER_ENCODER_MANAGER_H
#define IPC_RECORDER_ENCODER_MANAGER_H

#include "../core/media_frame.h"
#include "../core/media_packet.h"

typedef struct EncoderManager EncoderManager;
typedef struct EncoderOps EncoderOps;

typedef struct {
    int width;
    int height;
    int fps;
    int bitrate;
    int gop;
    CodecType codec;
} EncoderConfig;

struct EncoderOps {
    const char *name;
    int (*init)(EncoderManager *manager);
    void (*deinit)(EncoderManager *manager);
    int (*encode)(EncoderManager *manager,
                  const MediaFrame *src_frame,
                  MediaPacket *out_packet);
    int (*flush)(EncoderManager *manager, MediaPacket *out_packet);
};

struct EncoderManager {
    const EncoderOps *ops;
    void *priv;

    char encoder_name[64];
    EncoderConfig config;
};

int EncoderManager_Register(const EncoderOps *ops);
const EncoderOps *EncoderManager_Find(const char *encoder_name);

int EncoderManager_Init(EncoderManager *manager,
                        const char *encoder_name,
                        const EncoderConfig *config);
void EncoderManager_Deinit(EncoderManager *manager);
int EncoderManager_Encode(EncoderManager *manager,
                          const MediaFrame *src_frame,
                          MediaPacket *out_packet);
int EncoderManager_Flush(EncoderManager *manager, MediaPacket *out_packet);
#endif
