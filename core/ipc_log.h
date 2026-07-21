#ifndef IPC_RECORDER_IPC_LOG_H
#define IPC_RECORDER_IPC_LOG_H

#include <stdio.h>

#define IPC_LOG_ERROR 0
#define IPC_LOG_WARN  1
#define IPC_LOG_INFO  2
#define IPC_LOG_DEBUG 3

#ifndef IPC_LOG_LEVEL
#define IPC_LOG_LEVEL IPC_LOG_INFO
#endif

#define IPC_LOGE(fmt, ...)                                                     \
    do {                                                                       \
        if (IPC_LOG_LEVEL >= IPC_LOG_ERROR) {                                  \
            fprintf(stderr, fmt "\n", ##__VA_ARGS__);                         \
        }                                                                      \
    } while (0)

#define IPC_LOGW(fmt, ...)                                                     \
    do {                                                                       \
        if (IPC_LOG_LEVEL >= IPC_LOG_WARN) {                                   \
            fprintf(stderr, fmt "\n", ##__VA_ARGS__);                         \
        }                                                                      \
    } while (0)

#define IPC_LOGI(fmt, ...)                                                     \
    do {                                                                       \
        if (IPC_LOG_LEVEL >= IPC_LOG_INFO) {                                   \
            fprintf(stdout, fmt "\n", ##__VA_ARGS__);                         \
        }                                                                      \
    } while (0)

#define IPC_LOGD(fmt, ...)                                                     \
    do {                                                                       \
        if (IPC_LOG_LEVEL >= IPC_LOG_DEBUG) {                                  \
            fprintf(stdout, fmt "\n", ##__VA_ARGS__);                         \
        }                                                                      \
    } while (0)

#endif
