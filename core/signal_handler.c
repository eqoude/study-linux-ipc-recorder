#include "signal_handler.h"

#include "ipc_error.h"

#include <pthread.h>
#include <signal.h>
#include <string.h>

static sigset_t g_signal_set;

int Signal_Init(void)
{
    memset(&g_signal_set, 0, sizeof(g_signal_set));

    if (sigemptyset(&g_signal_set) != 0) {
        return IPC_ERROR;
    }
    if (sigaddset(&g_signal_set, SIGINT) != 0) {
        return IPC_ERROR;
    }
    if (sigaddset(&g_signal_set, SIGTERM) != 0) {
        return IPC_ERROR;
    }

    if (pthread_sigmask(SIG_BLOCK, &g_signal_set, NULL) != 0) {
        return IPC_ERROR;
    }

    return IPC_OK;
}

int Signal_Wait(void)
{
    int signo;

    if (sigwait(&g_signal_set, &signo) != 0) {
        return IPC_ERROR;
    }

    if (signo != SIGINT && signo != SIGTERM) {
        return IPC_ERROR;
    }

    return IPC_OK;
}
