#ifndef IPC_RECORDER_MUXER_MANAGER_H
#define IPC_RECORDER_MUXER_MANAGER_H

#include "../core/ipc_error.h"
#include "../core/media_packet.h"

typedef struct MuxerManager MuxerManager;
typedef struct MuxerOps MuxerOps;

typedef struct {
    char output_path[256];
    char format_name[64];
    int width;
    int height;
    int fps;
    CodecType codec;
} MuxerConfig;

struct MuxerOps {
    const char *name;
    int (*init)(MuxerManager *manager);
    void (*deinit)(MuxerManager *manager);
    int (*open)(MuxerManager *manager);
    void (*close)(MuxerManager *manager);
    int (*write_header)(MuxerManager *manager);
    int (*write_packet)(MuxerManager *manager, const MediaPacket *packet);
    int (*write_trailer)(MuxerManager *manager);
};

struct MuxerManager {
    const MuxerOps *ops;
    void *priv;

    char muxer_name[64];
    MuxerConfig config;
};

int MuxerManager_Register(const MuxerOps *ops);
const MuxerOps *MuxerManager_Find(const char *muxer_name);

int MuxerManager_Init(MuxerManager *manager,
                      const char *muxer_name,
                      const MuxerConfig *config);
void MuxerManager_Deinit(MuxerManager *manager);
int MuxerManager_Open(MuxerManager *manager);
void MuxerManager_Close(MuxerManager *manager);
int MuxerManager_WriteHeader(MuxerManager *manager);
int MuxerManager_WritePacket(MuxerManager *manager, const MediaPacket *packet);
int MuxerManager_WriteTrailer(MuxerManager *manager);

#endif
