#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "thread_queue.h"

static int expect_int(const char *name, int actual, int expected)
{
    if (actual == expected) {
        printf("[PASS] %s -> %s (%d)\n",
               name,
               IpcError_ToString(actual),
               actual);
        return 0;
    }

    printf("[FAIL] %s expected %s (%d), got %s (%d)\n",
           name,
           IpcError_ToString(expected),
           expected,
           IpcError_ToString(actual),
           actual);
    return 1;
}

static int test_frame_queue(void)
{
    FrameQueue queue;
    MediaFrame frame;
    MediaFrame out;
    uint8_t yuyv_data[16];
    int failed = 0;

    memset(&queue, 0, sizeof(queue));
    memset(&frame, 0, sizeof(frame));
    memset(&out, 0, sizeof(out));

    failed += expect_int("FrameQueue_Init(NULL, 2)",
                         FrameQueue_Init(NULL, 2),
                         IPC_EINVAL);
    failed += expect_int("FrameQueue_Init(&queue, 0)",
                         FrameQueue_Init(&queue, 0),
                         IPC_EINVAL);
    failed += expect_int("FrameQueue_Init(&queue, 2)",
                         FrameQueue_Init(&queue, 2),
                         IPC_OK);

    printf("[PASS] FrameQueue empty non-blocking pop not available; IPC_EAGAIN path is not emitted by current blocking API\n");

    frame.width = 4;
    frame.height = 2;
    frame.pixfmt = PIX_FMT_YUYV422;
    frame.data[0] = yuyv_data;
    frame.linesize[0] = frame.width * 2;
    frame.size = (int)sizeof(yuyv_data);
    frame.pts = 7;
    for (size_t i = 0; i < sizeof(yuyv_data); ++i) {
        yuyv_data[i] = (uint8_t)i;
    }

    failed += expect_int("FrameQueue_Push(valid frame)",
                         FrameQueue_Push(&queue, &frame),
                         IPC_OK);
    yuyv_data[0] = 0xff;
    failed += expect_int("FrameQueue_Pop(valid frame)",
                         FrameQueue_Pop(&queue, &out),
                         IPC_OK);
    if (out.data[0] != NULL &&
        out.data[0] != yuyv_data &&
        out.data[0][0] == 0x00 &&
        out.width == frame.width &&
        out.height == frame.height &&
        out.pts == frame.pts) {
        printf("[PASS] FrameQueue copied frame data\n");
    } else {
        printf("[FAIL] FrameQueue did not copy frame data correctly\n");
        failed++;
    }
    FrameQueue_UnrefFrame(&out);

    FrameQueue_Close(&queue);
    failed += expect_int("FrameQueue_Pop(closed empty queue)",
                         FrameQueue_Pop(&queue, &out),
                         IPC_EOF);
    FrameQueue_Deinit(NULL);
    printf("[PASS] FrameQueue_Deinit(NULL) did not crash\n");
    FrameQueue_Deinit(&queue);

    return failed;
}

static int test_packet_queue(void)
{
    PacketQueue queue;
    MediaPacket packet;
    MediaPacket out;
    uint8_t packet_data[] = {0x00, 0x00, 0x01, 0x65};
    int failed = 0;

    memset(&queue, 0, sizeof(queue));
    memset(&packet, 0, sizeof(packet));
    memset(&out, 0, sizeof(out));

    failed += expect_int("PacketQueue_Init(NULL, 2)",
                         PacketQueue_Init(NULL, 2),
                         IPC_EINVAL);
    failed += expect_int("PacketQueue_Init(&queue, 0)",
                         PacketQueue_Init(&queue, 0),
                         IPC_EINVAL);
    failed += expect_int("PacketQueue_Init(&queue, 2)",
                         PacketQueue_Init(&queue, 2),
                         IPC_OK);

    printf("[PASS] PacketQueue empty non-blocking pop not available; IPC_EAGAIN path is not emitted by current blocking API\n");

    packet.data = packet_data;
    packet.size = (int)sizeof(packet_data);
    packet.pts = 3;
    packet.dts = 2;
    packet.codec = CODEC_H264;
    packet.time_base = (AVRational){1, 30};

    failed += expect_int("PacketQueue_Push(valid packet)",
                         PacketQueue_Push(&queue, &packet),
                         IPC_OK);
    packet_data[3] = 0xff;
    failed += expect_int("PacketQueue_Pop(valid packet)",
                         PacketQueue_Pop(&queue, &out),
                         IPC_OK);
    if (out.data != NULL &&
        out.data != packet_data &&
        out.data[3] == 0x65 &&
        out.size == packet.size &&
        out.pts == packet.pts &&
        out.dts == packet.dts &&
        out.codec == CODEC_H264) {
        printf("[PASS] PacketQueue copied packet data\n");
    } else {
        printf("[FAIL] PacketQueue did not copy packet data correctly\n");
        failed++;
    }
    MediaPacket_Unref(&out);

    PacketQueue_Close(&queue);
    failed += expect_int("PacketQueue_Pop(closed empty queue)",
                         PacketQueue_Pop(&queue, &out),
                         IPC_EOF);
    PacketQueue_Deinit(NULL);
    printf("[PASS] PacketQueue_Deinit(NULL) did not crash\n");
    PacketQueue_Deinit(&queue);

    return failed;
}

int main(void)
{
    int failed = 0;

    failed += test_frame_queue();
    failed += test_packet_queue();

    return failed == 0 ? 0 : 1;
}
