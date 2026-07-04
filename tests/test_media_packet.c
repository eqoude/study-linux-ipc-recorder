#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "media_packet.h"

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

int main(void)
{
    MediaPacket packet;
    MediaPacket copy;
    AVPacket avpkt;
    uint8_t source_data[] = {0x00, 0x00, 0x01, 0x65, 0x88};
    int failed = 0;
    int ret;

    memset(&packet, 0, sizeof(packet));
    memset(&copy, 0, sizeof(copy));
    memset(&avpkt, 0, sizeof(avpkt));

    failed += expect_int("MediaPacket_Alloc(NULL, 100)",
                         MediaPacket_Alloc(NULL, 100),
                         IPC_EINVAL);
    failed += expect_int("MediaPacket_Alloc(&packet, 0)",
                         MediaPacket_Alloc(&packet, 0),
                         IPC_EINVAL);

    ret = MediaPacket_Alloc(&packet, 16);
    failed += expect_int("MediaPacket_Alloc(&packet, 16)", ret, IPC_OK);
    if (ret == IPC_OK && (packet.data == NULL || packet.size != 16 || !packet.owns_data)) {
        printf("[FAIL] MediaPacket_Alloc did not initialize owned data correctly\n");
        failed++;
    }

    MediaPacket_Unref(NULL);
    printf("[PASS] MediaPacket_Unref(NULL) did not crash\n");

    MediaPacket_Unref(&packet);
    if (packet.data == NULL && packet.size == 0 && packet.owns_data == 0) {
        printf("[PASS] MediaPacket_Unref(&packet) cleared packet\n");
    } else {
        printf("[FAIL] MediaPacket_Unref(&packet) did not clear packet\n");
        failed++;
    }

    failed += expect_int("MediaPacket_CopyFromAVPacket(NULL, &avpkt)",
                         MediaPacket_CopyFromAVPacket(NULL,
                                                      &avpkt,
                                                      CODEC_H264,
                                                      (AVRational){1, 30}),
                         IPC_EINVAL);
    failed += expect_int("MediaPacket_CopyFromAVPacket(&copy, NULL)",
                         MediaPacket_CopyFromAVPacket(&copy,
                                                      NULL,
                                                      CODEC_H264,
                                                      (AVRational){1, 30}),
                         IPC_EINVAL);

    avpkt.data = NULL;
    avpkt.size = 0;
    failed += expect_int("MediaPacket_CopyFromAVPacket(empty packet)",
                         MediaPacket_CopyFromAVPacket(&copy,
                                                      &avpkt,
                                                      CODEC_H264,
                                                      (AVRational){1, 30}),
                         IPC_EINVAL);

    avpkt.data = source_data;
    avpkt.size = (int)sizeof(source_data);
    avpkt.pts = 10;
    avpkt.dts = 8;
    ret = MediaPacket_CopyFromAVPacket(&copy,
                                       &avpkt,
                                       CODEC_H264,
                                       (AVRational){1, 30});
    failed += expect_int("MediaPacket_CopyFromAVPacket(valid packet)", ret, IPC_OK);
    if (ret == IPC_OK) {
        source_data[4] = 0xff;
        if (copy.data != NULL &&
            copy.data != source_data &&
            copy.size == avpkt.size &&
            copy.data[4] == 0x88 &&
            copy.pts == 10 &&
            copy.dts == 8 &&
            copy.codec == CODEC_H264 &&
            copy.time_base.num == 1 &&
            copy.time_base.den == 30) {
            printf("[PASS] MediaPacket_CopyFromAVPacket deep copied data\n");
        } else {
            printf("[FAIL] MediaPacket_CopyFromAVPacket did not deep copy metadata/data correctly\n");
            failed++;
        }
    }

    MediaPacket_Unref(&copy);
    return failed == 0 ? 0 : 1;
}
