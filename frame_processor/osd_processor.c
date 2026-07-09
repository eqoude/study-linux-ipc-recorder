#include "frame_processor_manager.h"

#include "ipc_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    unsigned char *buffer;
    int buffer_size;
} OsdProcessorContext;

#define OSD_TEXT_X 10
#define OSD_TEXT_Y 10
#define OSD_BG_X 8
#define OSD_BG_Y 8
#define OSD_FONT_WIDTH 5
#define OSD_FONT_HEIGHT 7
#define OSD_FONT_SCALE 2
#define OSD_CHAR_SPACING 1
#define OSD_BG_PADDING 2
#define OSD_Y_WHITE 235
#define OSD_Y_BLACK 16

static const uint8_t *osd_get_font(char ch)
{
    static const uint8_t font_digit[10][OSD_FONT_HEIGHT] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
        {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
        {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
        {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},
    };
    static const uint8_t font_dash[OSD_FONT_HEIGHT] = {
        0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00,
    };
    static const uint8_t font_colon[OSD_FONT_HEIGHT] = {
        0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00,
    };
    static const uint8_t font_space[OSD_FONT_HEIGHT] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    if (ch >= '0' && ch <= '9') {
        return font_digit[ch - '0'];
    }
    if (ch == '-') {
        return font_dash;
    }
    if (ch == ':') {
        return font_colon;
    }
    if (ch == ' ') {
        return font_space;
    }

    return font_space;
}

static void draw_rect_y(MediaFrame *frame,
                        int x,
                        int y,
                        int width,
                        int height,
                        uint8_t value)
{
    int end_x;
    int end_y;

    if (frame == NULL || frame->data[0] == NULL ||
        frame->linesize[0] < frame->width) {
        return;
    }

    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (width <= 0 || height <= 0 || x >= frame->width || y >= frame->height) {
        return;
    }

    end_x = x + width;
    end_y = y + height;
    if (end_x > frame->width) {
        end_x = frame->width;
    }
    if (end_y > frame->height) {
        end_y = frame->height;
    }

    for (int row = y; row < end_y; ++row) {
        uint8_t *dst_y = frame->data[0] + row * frame->linesize[0];
        for (int col = x; col < end_x; ++col) {
            dst_y[col] = value;
        }
    }
}

static void draw_char_y(MediaFrame *frame, int x, int y, char ch, uint8_t value)
{
    const uint8_t *bitmap = osd_get_font(ch);

    for (int row = 0; row < OSD_FONT_HEIGHT; ++row) {
        for (int col = 0; col < OSD_FONT_WIDTH; ++col) {
            if ((bitmap[row] & (uint8_t)(1U << (OSD_FONT_WIDTH - 1 - col))) == 0) {
                continue;
            }

            draw_rect_y(frame,
                        x + col * OSD_FONT_SCALE,
                        y + row * OSD_FONT_SCALE,
                        OSD_FONT_SCALE,
                        OSD_FONT_SCALE,
                        value);
        }
    }
}

static void draw_text_y(MediaFrame *frame, int x, int y, const char *text, uint8_t value)
{
    int cursor_x = x;
    int step = OSD_FONT_WIDTH * OSD_FONT_SCALE + OSD_CHAR_SPACING;

    if (text == NULL) {
        return;
    }

    for (int i = 0; text[i] != '\0'; ++i) {
        draw_char_y(frame, cursor_x, y, text[i], value);
        cursor_x += step;
    }
}

static int overlay_datetime(MediaFrame *frame)
{
    char time_text[32];
    time_t now;
    struct tm tm_now;
    size_t text_len;
    int text_width;
    int text_height;

    if (frame == NULL || frame->pixfmt != PIX_FMT_YUV420P ||
        frame->data[0] == NULL || frame->linesize[0] < frame->width) {
        return IPC_EINVAL;
    }

    now = time(NULL);
    if (localtime_r(&now, &tm_now) == NULL) {
        return IPC_ERROR;
    }
    if (strftime(time_text,
                 sizeof(time_text),
                 "%Y-%m-%d %H:%M:%S",
                 &tm_now) == 0) {
        return IPC_ERROR;
    }

    text_len = strlen(time_text);
    text_width = (int)text_len * (OSD_FONT_WIDTH * OSD_FONT_SCALE +
                                  OSD_CHAR_SPACING) -
                 OSD_CHAR_SPACING;
    text_height = OSD_FONT_HEIGHT * OSD_FONT_SCALE;

    draw_rect_y(frame,
                OSD_BG_X,
                OSD_BG_Y,
                text_width + OSD_BG_PADDING * 2,
                text_height + OSD_BG_PADDING * 2,
                OSD_Y_BLACK);
    draw_text_y(frame, OSD_TEXT_X, OSD_TEXT_Y, time_text, OSD_Y_WHITE);

    return IPC_OK;
}

static int osd_processor_init(void *manager)
{
    FrameProcessorManager *processor = (FrameProcessorManager *)manager;
    OsdProcessorContext *ctx;

    if (processor == NULL) {
        return IPC_EINVAL;
    }

    ctx = (OsdProcessorContext *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return IPC_ENOMEM;
    }

    if (processor->config.width <= 0) {
        processor->config.width = 96;
    }
    if (processor->config.height <= 0) {
        processor->config.height = 24;
    }

    processor->priv = ctx;
    IPC_LOGI("[osd_processor] init");

    return IPC_OK;
}

static void osd_processor_deinit(void *manager)
{
    FrameProcessorManager *processor = (FrameProcessorManager *)manager;
    OsdProcessorContext *ctx;

    if (processor == NULL || processor->priv == NULL) {
        return;
    }

    ctx = (OsdProcessorContext *)processor->priv;
    free(ctx->buffer);
    free(ctx);
    processor->priv = NULL;

    IPC_LOGI("[osd_processor] deinit");
}

static int osd_processor_process(void *manager,
                                 MediaFrame *in,
                                 MediaFrame *out)
{
    FrameProcessorManager *processor = (FrameProcessorManager *)manager;
    OsdProcessorContext *ctx;
    unsigned char *y_plane;
    unsigned char *u_plane;
    unsigned char *v_plane;
    int width;
    int height;
    int size;
    int ret;

    if (processor == NULL || in == NULL || out == NULL) {
        return IPC_EINVAL;
    }
    if (processor->priv == NULL) {
        return IPC_ESTATE;
    }
    if (in->pixfmt != PIX_FMT_YUV420P) {
        return IPC_EUNSUPPORTED;
    }
    if (in->data[0] == NULL || in->data[1] == NULL || in->data[2] == NULL ||
        in->linesize[0] <= 0 || in->linesize[1] <= 0 ||
        in->linesize[2] <= 0 || in->width <= 0 || in->height <= 0 ||
        (in->width % 2) != 0 || (in->height % 2) != 0) {
        return IPC_EINVAL;
    }

    ctx = (OsdProcessorContext *)processor->priv;
    width = in->width;
    height = in->height;
    size = width * height * 3 / 2;

    if (ctx->buffer_size < size) {
        unsigned char *buffer = (unsigned char *)realloc(ctx->buffer, (size_t)size);
        if (buffer == NULL) {
            return IPC_ENOMEM;
        }
        ctx->buffer = buffer;
        ctx->buffer_size = size;
    }

    y_plane = ctx->buffer;
    u_plane = y_plane + width * height;
    v_plane = u_plane + width * height / 4;

    for (int row = 0; row < height; ++row) {
        memcpy(y_plane + row * width,
               in->data[0] + row * in->linesize[0],
               (size_t)width);
    }

    for (int row = 0; row < height / 2; ++row) {
        memcpy(u_plane + row * (width / 2),
               in->data[1] + row * in->linesize[1],
               (size_t)(width / 2));
        memcpy(v_plane + row * (width / 2),
               in->data[2] + row * in->linesize[2],
               (size_t)(width / 2));
    }

    out->width = width;
    out->height = height;
    out->pixfmt = PIX_FMT_YUV420P;
    out->data[0] = y_plane;
    out->data[1] = u_plane;
    out->data[2] = v_plane;
    out->linesize[0] = width;
    out->linesize[1] = width / 2;
    out->linesize[2] = width / 2;
    out->size = size;
    out->pts = in->pts;

    ret = overlay_datetime(out);
    if (ret != IPC_OK) {
        return ret;
    }

    IPC_LOGD("[osd_processor] process");
    return IPC_OK;
}

const FrameProcessorOps g_osd_processor_ops = {
    .name = "osd",
    .init = osd_processor_init,
    .process = osd_processor_process,
    .deinit = osd_processor_deinit,
};
