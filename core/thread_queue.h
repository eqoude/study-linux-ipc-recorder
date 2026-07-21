#ifndef IPC_RECORDER_THREAD_QUEUE_H
#define IPC_RECORDER_THREAD_QUEUE_H

#include <stddef.h>

#include "ipc_error.h"
#include "media_frame.h"
#include "media_packet.h"

typedef struct {
    MediaFrame *items;
    size_t capacity;
    size_t read_index;
    size_t write_index;
    size_t count;
    int closed;
    void *mutex;
    void *not_empty;
    void *not_full;
} FrameQueue;

typedef struct {
    MediaPacket *items;
    size_t capacity;
    size_t read_index;
    size_t write_index;
    size_t count;
    int closed;
    void *mutex;
    void *not_empty;
    void *not_full;
} PacketQueue;

int FrameQueue_Init(FrameQueue *queue, size_t max_size);
void FrameQueue_Deinit(FrameQueue *queue);
int FrameQueue_Push(FrameQueue *queue, const MediaFrame *frame);
int FrameQueue_Pop(FrameQueue *queue, MediaFrame *frame);
void FrameQueue_Close(FrameQueue *queue);
void FrameQueue_UnrefFrame(MediaFrame *frame);

int PacketQueue_Init(PacketQueue *queue, size_t max_size);
void PacketQueue_Deinit(PacketQueue *queue);
int PacketQueue_Push(PacketQueue *queue, const MediaPacket *packet);
int PacketQueue_Pop(PacketQueue *queue, MediaPacket *packet);
void PacketQueue_Close(PacketQueue *queue);

#endif
