#ifndef IPC_RECORDER_IPC_ERROR_H
#define IPC_RECORDER_IPC_ERROR_H

typedef enum {
    IPC_OK = 0,

    IPC_EAGAIN = 1,
    IPC_EOF = 2,

    IPC_ERROR = -1,
    IPC_EINVAL = -2,
    IPC_ENOMEM = -3,
    IPC_EOPEN = -4,
    IPC_EIO = -5,
    IPC_ESTATE = -6,
    IPC_EUNSUPPORTED = -7,
    IPC_ECODEC = -8,
    IPC_EMUXER = -9,
    IPC_EQUEUE = -10,
    IPC_ETHREAD = -11
} IpcResult;

const char *IpcError_ToString(int err);

#endif
