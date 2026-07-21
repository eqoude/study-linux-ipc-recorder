#include "thread_queue.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t *queue_mutex(void *mutex)
{
    return (pthread_mutex_t *)mutex;
}

static pthread_cond_t *queue_cond(void *cond)
{
    return (pthread_cond_t *)cond;
}

static int queue_alloc_sync(void **mutex, void **not_empty, void **not_full)
{
    pthread_mutex_t *mtx;
    pthread_cond_t *empty_cond;
    pthread_cond_t *full_cond;

    mtx = (pthread_mutex_t *)malloc(sizeof(*mtx));
    empty_cond = (pthread_cond_t *)malloc(sizeof(*empty_cond));
    full_cond = (pthread_cond_t *)malloc(sizeof(*full_cond));
    if (mtx == NULL || empty_cond == NULL || full_cond == NULL) {
        free(mtx);
        free(empty_cond);
        free(full_cond);
        return IPC_ENOMEM;
    }

    if (pthread_mutex_init(mtx, NULL) != 0) {
        free(mtx);
        free(empty_cond);
        free(full_cond);
        return IPC_ETHREAD;
    }

    if (pthread_cond_init(empty_cond, NULL) != 0) {
        pthread_mutex_destroy(mtx);
        free(mtx);
        free(empty_cond);
        free(full_cond);
        return IPC_ETHREAD;
    }

    if (pthread_cond_init(full_cond, NULL) != 0) {
        pthread_cond_destroy(empty_cond);
        pthread_mutex_destroy(mtx);
        free(mtx);
        free(empty_cond);
        free(full_cond);
        return IPC_ETHREAD;
    }

    *mutex = mtx;
    *not_empty = empty_cond;
    *not_full = full_cond;
    return IPC_OK;
}

static void queue_free_sync(void *mutex, void *not_empty, void *not_full)
{
    if (mutex != NULL) {
        pthread_mutex_destroy(queue_mutex(mutex));
        free(mutex);
    }
    if (not_empty != NULL) {
        pthread_cond_destroy(queue_cond(not_empty));
        free(not_empty);
    }
    if (not_full != NULL) {
        pthread_cond_destroy(queue_cond(not_full));
        free(not_full);
    }
}

static int frame_data_size(const MediaFrame *frame)
{
    if (frame == NULL || frame->width <= 0 || frame->height <= 0) {
        return IPC_EINVAL;
    }

    if (frame->pixfmt == PIX_FMT_YUYV422) {
        if (frame->linesize[0] <= 0 || frame->data[0] == NULL) {
            return IPC_EINVAL;
        }
        return frame->linesize[0] * frame->height;
    }

    if (frame->pixfmt == PIX_FMT_YUV420P) {
        if (frame->data[0] == NULL || frame->data[1] == NULL ||
            frame->data[2] == NULL || frame->linesize[0] < frame->width ||
            frame->linesize[1] < frame->width / 2 ||
            frame->linesize[2] < frame->width / 2) {
            return IPC_EINVAL;
        }
        return frame->width * frame->height * 3 / 2;
    }

    return IPC_EUNSUPPORTED;
}

static int frame_clone(MediaFrame *dst, const MediaFrame *src)
{
    uint8_t *buffer;
    uint8_t *y_plane;
    uint8_t *u_plane;
    uint8_t *v_plane;
    int size;

    if (dst == NULL || src == NULL) {
        return IPC_EINVAL;
    }

    memset(dst, 0, sizeof(*dst));
    size = frame_data_size(src);
    if (size < 0) {
        return size;
    }

    buffer = (uint8_t *)malloc((size_t)size);
    if (buffer == NULL) {
        return IPC_ENOMEM;
    }

    *dst = *src;
    dst->data[0] = buffer;
    dst->data[1] = NULL;
    dst->data[2] = NULL;

    if (src->pixfmt == PIX_FMT_YUYV422) {
        for (int row = 0; row < src->height; ++row) {
            memcpy(buffer + row * src->linesize[0],
                   src->data[0] + row * src->linesize[0],
                   (size_t)src->linesize[0]);
        }
        dst->size = size;
        return IPC_OK;
    }

    y_plane = buffer;
    u_plane = y_plane + src->width * src->height;
    v_plane = u_plane + src->width * src->height / 4;

    for (int row = 0; row < src->height; ++row) {
        memcpy(y_plane + row * src->width,
               src->data[0] + row * src->linesize[0],
               (size_t)src->width);
    }

    for (int row = 0; row < src->height / 2; ++row) {
        memcpy(u_plane + row * (src->width / 2),
               src->data[1] + row * src->linesize[1],
               (size_t)(src->width / 2));
        memcpy(v_plane + row * (src->width / 2),
               src->data[2] + row * src->linesize[2],
               (size_t)(src->width / 2));
    }

    dst->data[0] = y_plane;
    dst->data[1] = u_plane;
    dst->data[2] = v_plane;
    dst->linesize[0] = src->width;
    dst->linesize[1] = src->width / 2;
    dst->linesize[2] = src->width / 2;
    dst->size = size;
    return IPC_OK;
}

void FrameQueue_UnrefFrame(MediaFrame *frame)
{
    if (frame == NULL) {
        return;
    }

    free(frame->data[0]);
    memset(frame, 0, sizeof(*frame));
}

int FrameQueue_Init(FrameQueue *queue, size_t max_size)
{
    if (queue == NULL || max_size == 0) {
        return IPC_EINVAL;
    }

    memset(queue, 0, sizeof(*queue));
    queue->items = (MediaFrame *)calloc(max_size, sizeof(*queue->items));
    if (queue->items == NULL) {
        return IPC_ENOMEM;
    }

    int ret = queue_alloc_sync(&queue->mutex, &queue->not_empty, &queue->not_full);
    if (ret != IPC_OK) {
        free(queue->items);
        memset(queue, 0, sizeof(*queue));
        return ret;
    }

    queue->capacity = max_size;
    return IPC_OK;
}

void FrameQueue_Deinit(FrameQueue *queue)
{
    if (queue == NULL) {
        return;
    }

    if (queue->items != NULL) {
        for (size_t i = 0; i < queue->capacity; ++i) {
            FrameQueue_UnrefFrame(&queue->items[i]);
        }
        free(queue->items);
    }

    queue_free_sync(queue->mutex, queue->not_empty, queue->not_full);
    memset(queue, 0, sizeof(*queue));
}

int FrameQueue_Push(FrameQueue *queue, const MediaFrame *frame)
{
    MediaFrame clone;
    int ret;

    if (queue == NULL || frame == NULL || queue->items == NULL) {
        return IPC_EINVAL;
    }

    ret = frame_clone(&clone, frame);
    if (ret != IPC_OK) {
        return ret;
    }

    if (pthread_mutex_lock(queue_mutex(queue->mutex)) != 0) {
        FrameQueue_UnrefFrame(&clone);
        return IPC_ETHREAD;
    }
    while (!queue->closed && queue->count >= queue->capacity) {
        if (pthread_cond_wait(queue_cond(queue->not_full), queue_mutex(queue->mutex)) != 0) {
            pthread_mutex_unlock(queue_mutex(queue->mutex));
            FrameQueue_UnrefFrame(&clone);
            return IPC_ETHREAD;
        }
    }

    if (queue->closed) {
        pthread_mutex_unlock(queue_mutex(queue->mutex));
        FrameQueue_UnrefFrame(&clone);
        return IPC_EOF;
    }

    queue->items[queue->write_index] = clone;
    queue->write_index = (queue->write_index + 1) % queue->capacity;
    queue->count++;
    pthread_cond_signal(queue_cond(queue->not_empty));
    pthread_mutex_unlock(queue_mutex(queue->mutex));
    return IPC_OK;
}

int FrameQueue_Pop(FrameQueue *queue, MediaFrame *frame)
{
    if (queue == NULL || frame == NULL || queue->items == NULL) {
        return IPC_EINVAL;
    }

    if (pthread_mutex_lock(queue_mutex(queue->mutex)) != 0) {
        return IPC_ETHREAD;
    }
    while (!queue->closed && queue->count == 0) {
        if (pthread_cond_wait(queue_cond(queue->not_empty), queue_mutex(queue->mutex)) != 0) {
            pthread_mutex_unlock(queue_mutex(queue->mutex));
            return IPC_ETHREAD;
        }
    }

    if (queue->count == 0 && queue->closed) {
        pthread_mutex_unlock(queue_mutex(queue->mutex));
        return IPC_EOF;
    }

    *frame = queue->items[queue->read_index];
    memset(&queue->items[queue->read_index], 0, sizeof(queue->items[queue->read_index]));
    queue->read_index = (queue->read_index + 1) % queue->capacity;
    queue->count--;
    pthread_cond_signal(queue_cond(queue->not_full));
    pthread_mutex_unlock(queue_mutex(queue->mutex));
    return IPC_OK;
}

void FrameQueue_Close(FrameQueue *queue)
{
    if (queue == NULL || queue->mutex == NULL) {
        return;
    }

    pthread_mutex_lock(queue_mutex(queue->mutex));
    queue->closed = 1;
    pthread_cond_broadcast(queue_cond(queue->not_empty));
    pthread_cond_broadcast(queue_cond(queue->not_full));
    pthread_mutex_unlock(queue_mutex(queue->mutex));
}

static int packet_clone(MediaPacket *dst, const MediaPacket *src)
{
    if (dst == NULL || src == NULL || src->data == NULL || src->size <= 0) {
        return IPC_EINVAL;
    }

    memset(dst, 0, sizeof(*dst));
    int ret = MediaPacket_Alloc(dst, src->size);
    if (ret != IPC_OK) {
        return ret;
    }

    memcpy(dst->data, src->data, (size_t)src->size);
    dst->keyframe = src->keyframe;
    dst->extradata = src->extradata;
    dst->extradata_size = src->extradata_size;
    dst->pts = src->pts;
    dst->dts = src->dts;
    dst->time_base = src->time_base;
    dst->codec = src->codec;
    return IPC_OK;
}

int PacketQueue_Init(PacketQueue *queue, size_t max_size)
{
    if (queue == NULL || max_size == 0) {
        return IPC_EINVAL;
    }

    memset(queue, 0, sizeof(*queue));
    queue->items = (MediaPacket *)calloc(max_size, sizeof(*queue->items));
    if (queue->items == NULL) {
        return IPC_ENOMEM;
    }

    int ret = queue_alloc_sync(&queue->mutex, &queue->not_empty, &queue->not_full);
    if (ret != IPC_OK) {
        free(queue->items);
        memset(queue, 0, sizeof(*queue));
        return ret;
    }

    queue->capacity = max_size;
    return IPC_OK;
}

void PacketQueue_Deinit(PacketQueue *queue)
{
    if (queue == NULL) {
        return;
    }

    if (queue->items != NULL) {
        for (size_t i = 0; i < queue->capacity; ++i) {
            MediaPacket_Unref(&queue->items[i]);
        }
        free(queue->items);
    }

    queue_free_sync(queue->mutex, queue->not_empty, queue->not_full);
    memset(queue, 0, sizeof(*queue));
}

int PacketQueue_Push(PacketQueue *queue, const MediaPacket *packet)
{
    MediaPacket clone;
    int ret;

    if (queue == NULL || packet == NULL || queue->items == NULL) {
        return IPC_EINVAL;
    }

    ret = packet_clone(&clone, packet);
    if (ret != IPC_OK) {
        return ret;
    }

    if (pthread_mutex_lock(queue_mutex(queue->mutex)) != 0) {
        MediaPacket_Unref(&clone);
        return IPC_ETHREAD;
    }
    while (!queue->closed && queue->count >= queue->capacity) {
        if (pthread_cond_wait(queue_cond(queue->not_full), queue_mutex(queue->mutex)) != 0) {
            pthread_mutex_unlock(queue_mutex(queue->mutex));
            MediaPacket_Unref(&clone);
            return IPC_ETHREAD;
        }
    }

    if (queue->closed) {
        pthread_mutex_unlock(queue_mutex(queue->mutex));
        MediaPacket_Unref(&clone);
        return IPC_EOF;
    }

    queue->items[queue->write_index] = clone;
    queue->write_index = (queue->write_index + 1) % queue->capacity;
    queue->count++;
    pthread_cond_signal(queue_cond(queue->not_empty));
    pthread_mutex_unlock(queue_mutex(queue->mutex));
    return IPC_OK;
}

int PacketQueue_Pop(PacketQueue *queue, MediaPacket *packet)
{
    if (queue == NULL || packet == NULL || queue->items == NULL) {
        return IPC_EINVAL;
    }

    if (pthread_mutex_lock(queue_mutex(queue->mutex)) != 0) {
        return IPC_ETHREAD;
    }
    while (!queue->closed && queue->count == 0) {
        if (pthread_cond_wait(queue_cond(queue->not_empty), queue_mutex(queue->mutex)) != 0) {
            pthread_mutex_unlock(queue_mutex(queue->mutex));
            return IPC_ETHREAD;
        }
    }

    if (queue->count == 0 && queue->closed) {
        pthread_mutex_unlock(queue_mutex(queue->mutex));
        return IPC_EOF;
    }

    *packet = queue->items[queue->read_index];
    memset(&queue->items[queue->read_index], 0, sizeof(queue->items[queue->read_index]));
    queue->read_index = (queue->read_index + 1) % queue->capacity;
    queue->count--;
    pthread_cond_signal(queue_cond(queue->not_full));
    pthread_mutex_unlock(queue_mutex(queue->mutex));
    return IPC_OK;
}

void PacketQueue_Close(PacketQueue *queue)
{
    if (queue == NULL || queue->mutex == NULL) {
        return;
    }

    pthread_mutex_lock(queue_mutex(queue->mutex));
    queue->closed = 1;
    pthread_cond_broadcast(queue_cond(queue->not_empty));
    pthread_cond_broadcast(queue_cond(queue->not_full));
    pthread_mutex_unlock(queue_mutex(queue->mutex));
}
