#include <stdio.h>
#include <string.h>

#include "ipc_error.h"

typedef struct {
    int code;
    const char *name;
    const char *expected;
} ErrorCase;

static int check_error_string(const ErrorCase *test_case)
{
    const char *actual = IpcError_ToString(test_case->code);

    if (strcmp(actual, test_case->expected) == 0) {
        printf("[PASS] %s -> %s\n", test_case->name, actual);
        return 0;
    }

    printf("[FAIL] %s expected %s, got %s\n",
           test_case->name,
           test_case->expected,
           actual);
    return 1;
}

int main(void)
{
    const ErrorCase cases[] = {
        {IPC_OK, "IPC_OK", "OK"},
        {IPC_EAGAIN, "IPC_EAGAIN", "Temporary unavailable"},
        {IPC_EOF, "IPC_EOF", "End of stream"},
        {IPC_EINVAL, "IPC_EINVAL", "Invalid argument"},
        {IPC_ENOMEM, "IPC_ENOMEM", "Out of memory"},
        {IPC_EOPEN, "IPC_EOPEN", "Open failed"},
        {IPC_EIO, "IPC_EIO", "I/O error"},
        {IPC_ESTATE, "IPC_ESTATE", "Invalid state"},
        {IPC_EUNSUPPORTED, "IPC_EUNSUPPORTED", "Unsupported"},
        {IPC_ECODEC, "IPC_ECODEC", "Codec error"},
        {IPC_EMUXER, "IPC_EMUXER", "Muxer error"},
        {IPC_EQUEUE, "IPC_EQUEUE", "Queue error"},
        {IPC_ETHREAD, "IPC_ETHREAD", "Thread error"},
        {12345, "UNKNOWN", "Unknown error"},
    };
    int failed = 0;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        failed += check_error_string(&cases[i]);
    }

    return failed == 0 ? 0 : 1;
}
