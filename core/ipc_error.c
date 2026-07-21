#include "ipc_error.h"

const char *IpcError_ToString(int err)
{
    switch (err) {
    case IPC_OK:
        return "OK";
    case IPC_EAGAIN:
        return "Temporary unavailable";
    case IPC_EOF:
        return "End of stream";
    case IPC_ERROR:
        return "General error";
    case IPC_EINVAL:
        return "Invalid argument";
    case IPC_ENOMEM:
        return "Out of memory";
    case IPC_EOPEN:
        return "Open failed";
    case IPC_EIO:
        return "I/O error";
    case IPC_ESTATE:
        return "Invalid state";
    case IPC_EUNSUPPORTED:
        return "Unsupported";
    case IPC_ECODEC:
        return "Codec error";
    case IPC_EMUXER:
        return "Muxer error";
    case IPC_EQUEUE:
        return "Queue error";
    case IPC_ETHREAD:
        return "Thread error";
    default:
        return "Unknown error";
    }
}
