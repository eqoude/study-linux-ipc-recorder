#include "sdl_display_manager.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Thread *thread;
    SDL_mutex *mutex;

    unsigned char *frame_buffer;
    int frame_size;
    int width;
    int height;
    volatile int running;
    volatile int ready;
    volatile int dirty;
    int init_result;
    volatile int error_result;
    char title[128];
} SdlDisplayContext;

static int sdl_display_copy_frame(SdlDisplayContext *ctx, const MediaFrame *frame)
{
    unsigned char *y_plane;
    unsigned char *u_plane;
    unsigned char *v_plane;
    int size;

    if (ctx == NULL || frame == NULL || frame->pixfmt != PIX_FMT_YUV420P ||
        frame->data[0] == NULL || frame->data[1] == NULL ||
        frame->data[2] == NULL || frame->width != ctx->width ||
        frame->height != ctx->height || frame->linesize[0] < ctx->width ||
        frame->linesize[1] < ctx->width / 2 ||
        frame->linesize[2] < ctx->width / 2) {
        return IPC_EINVAL;
    }

    size = ctx->width * ctx->height * 3 / 2;
    if (ctx->frame_size < size) {
        unsigned char *buffer = (unsigned char *)realloc(ctx->frame_buffer,
                                                         (size_t)size);
        if (buffer == NULL) {
            return IPC_ENOMEM;
        }
        ctx->frame_buffer = buffer;
        ctx->frame_size = size;
    }

    y_plane = ctx->frame_buffer;
    u_plane = y_plane + ctx->width * ctx->height;
    v_plane = u_plane + ctx->width * ctx->height / 4;

    for (int row = 0; row < ctx->height; ++row) {
        memcpy(y_plane + row * ctx->width,
               frame->data[0] + row * frame->linesize[0],
               (size_t)ctx->width);
    }

    for (int row = 0; row < ctx->height / 2; ++row) {
        memcpy(u_plane + row * (ctx->width / 2),
               frame->data[1] + row * frame->linesize[1],
               (size_t)(ctx->width / 2));
        memcpy(v_plane + row * (ctx->width / 2),
               frame->data[2] + row * frame->linesize[2],
               (size_t)(ctx->width / 2));
    }

    ctx->dirty = 1;
    return IPC_OK;
}

static int sdl_display_thread(void *arg)
{
    SdlDisplayContext *ctx = (SdlDisplayContext *)arg;
    SDL_Event event;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "[sdl_display] SDL_Init failed: %s\n", SDL_GetError());
        ctx->init_result = IPC_EIO;
        ctx->ready = 1;
        return IPC_EIO;
    }

    ctx->window = SDL_CreateWindow(ctx->title,
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   ctx->width,
                                   ctx->height,
                                   SDL_WINDOW_SHOWN);
    if (ctx->window == NULL) {
        fprintf(stderr, "[sdl_display] SDL_CreateWindow failed: %s\n", SDL_GetError());
        ctx->init_result = IPC_EIO;
        ctx->ready = 1;
        SDL_Quit();
        return IPC_EIO;
    }

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_ACCELERATED);
    if (ctx->renderer == NULL) {
        ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (ctx->renderer == NULL) {
        fprintf(stderr, "[sdl_display] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        ctx->init_result = IPC_EIO;
        ctx->ready = 1;
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return IPC_EIO;
    }

    ctx->texture = SDL_CreateTexture(ctx->renderer,
                                     SDL_PIXELFORMAT_IYUV,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     ctx->width,
                                     ctx->height);
    if (ctx->texture == NULL) {
        fprintf(stderr, "[sdl_display] SDL_CreateTexture failed: %s\n", SDL_GetError());
        ctx->init_result = IPC_EIO;
        ctx->ready = 1;
        SDL_DestroyRenderer(ctx->renderer);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return IPC_EIO;
    }

    ctx->init_result = IPC_OK;
    ctx->ready = 1;

    while (ctx->running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                ctx->running = 0;
            }
        }

        if (SDL_TryLockMutex(ctx->mutex) == 0) {
            if (ctx->dirty && ctx->frame_buffer != NULL) {
                unsigned char *y_plane = ctx->frame_buffer;
                unsigned char *u_plane = y_plane + ctx->width * ctx->height;
                unsigned char *v_plane = u_plane + ctx->width * ctx->height / 4;

                if (SDL_UpdateYUVTexture(ctx->texture,
                                         NULL,
                                         y_plane,
                                         ctx->width,
                                         u_plane,
                                         ctx->width / 2,
                                         v_plane,
                                         ctx->width / 2) != 0) {
                    fprintf(stderr,
                            "[sdl_display] SDL_UpdateYUVTexture failed: %s\n",
                            SDL_GetError());
                    ctx->error_result = IPC_EIO;
                    ctx->running = 0;
                }
                ctx->dirty = 0;
            }
            SDL_UnlockMutex(ctx->mutex);
        }

        if (SDL_RenderClear(ctx->renderer) != 0) {
            fprintf(stderr, "[sdl_display] SDL_RenderClear failed: %s\n", SDL_GetError());
            ctx->error_result = IPC_EIO;
            ctx->running = 0;
        }
        if (SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL) != 0) {
            fprintf(stderr, "[sdl_display] SDL_RenderCopy failed: %s\n", SDL_GetError());
            ctx->error_result = IPC_EIO;
            ctx->running = 0;
        }
        SDL_RenderPresent(ctx->renderer);
        SDL_Delay(1);
    }

    SDL_DestroyTexture(ctx->texture);
    SDL_DestroyRenderer(ctx->renderer);
    SDL_DestroyWindow(ctx->window);
    SDL_Quit();

    ctx->texture = NULL;
    ctx->renderer = NULL;
    ctx->window = NULL;
    return IPC_OK;
}

static int sdl_display_init(void *manager)
{
    ViewerManager *viewer = (ViewerManager *)manager;
    SdlDisplayContext *ctx;

    if (viewer == NULL) {
        return IPC_EINVAL;
    }

    ctx = (SdlDisplayContext *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return IPC_ENOMEM;
    }

    ctx->width = viewer->config.width > 0 ? viewer->config.width : 640;
    ctx->height = viewer->config.height > 0 ? viewer->config.height : 480;
    snprintf(ctx->title, sizeof(ctx->title), "%s",
             viewer->config.title[0] != '\0' ? viewer->config.title : "IPC Preview");

    ctx->mutex = SDL_CreateMutex();
    if (ctx->mutex == NULL) {
        free(ctx);
        return IPC_EIO;
    }

    ctx->running = 1;
    ctx->init_result = IPC_ERROR;
    ctx->error_result = IPC_OK;
    ctx->thread = SDL_CreateThread(sdl_display_thread, "sdl_display", ctx);
    if (ctx->thread == NULL) {
        SDL_DestroyMutex(ctx->mutex);
        free(ctx);
        return IPC_ETHREAD;
    }

    while (!ctx->ready) {
        SDL_Delay(1);
    }

    if (ctx->init_result < 0) {
        int init_result = ctx->init_result;
        ctx->running = 0;
        SDL_WaitThread(ctx->thread, NULL);
        SDL_DestroyMutex(ctx->mutex);
        free(ctx);
        return init_result;
    }

    viewer->priv = ctx;
    printf("[sdl_display] init\n");
    return IPC_OK;
}

static int sdl_display_display(void *manager, MediaFrame *frame)
{
    ViewerManager *viewer = (ViewerManager *)manager;
    SdlDisplayContext *ctx;
    int ret;

    if (viewer == NULL || frame == NULL) {
        return IPC_EINVAL;
    }
    if (viewer->priv == NULL) {
        return IPC_ESTATE;
    }

    ctx = (SdlDisplayContext *)viewer->priv;
    if (!ctx->running) {
        if (ctx->error_result < 0) {
            return ctx->error_result;
        }
        return IPC_EOF;
    }

    if (ctx->mutex == NULL) {
        return IPC_ESTATE;
    }

    if (SDL_TryLockMutex(ctx->mutex) != 0) {
        return IPC_OK;
    }

    ret = sdl_display_copy_frame(ctx, frame);
    SDL_UnlockMutex(ctx->mutex);

    return ret;
}

static void sdl_display_deinit(void *manager)
{
    ViewerManager *viewer = (ViewerManager *)manager;
    SdlDisplayContext *ctx;

    if (viewer == NULL || viewer->priv == NULL) {
        return;
    }

    ctx = (SdlDisplayContext *)viewer->priv;
    ctx->running = 0;

    if (ctx->thread != NULL) {
        SDL_WaitThread(ctx->thread, NULL);
    }
    if (ctx->mutex != NULL) {
        SDL_DestroyMutex(ctx->mutex);
    }

    free(ctx->frame_buffer);
    free(ctx);
    viewer->priv = NULL;

    printf("[sdl_display] deinit\n");
}

const ViewerOps g_sdl_display_ops = {
    .name = "sdl",
    .init = sdl_display_init,
    .display = sdl_display_display,
    .deinit = sdl_display_deinit,
};
