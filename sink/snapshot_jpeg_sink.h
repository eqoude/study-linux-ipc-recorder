#ifndef IPC_RECORDER_SNAPSHOT_JPEG_SINK_H
#define IPC_RECORDER_SNAPSHOT_JPEG_SINK_H

#include "frame_sink_manager.h"

typedef struct {
    const char *output_path;
    int interval_frames;
} SnapshotJpegSinkConfig;

extern const FrameSinkOps g_snapshot_jpeg_sink_ops;

#endif
