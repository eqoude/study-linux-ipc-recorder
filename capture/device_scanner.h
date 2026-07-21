#ifndef IPC_RECORDER_DEVICE_SCANNER_H
#define IPC_RECORDER_DEVICE_SCANNER_H

typedef struct {
    char path[128];
    char driver[64];
    char card[128];
} CameraDeviceInfo;

int DeviceScanner_FindFirstCamera(CameraDeviceInfo *info);

#endif
