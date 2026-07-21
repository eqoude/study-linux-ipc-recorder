#include "device_scanner.h"

#include "ipc_error.h"
#include "ipc_log.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int device_scanner_is_camera(unsigned int caps)
{
    if ((caps & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        return 0;
    }
    if ((caps & V4L2_CAP_STREAMING) == 0) {
        return 0;
    }
#ifdef V4L2_CAP_META_CAPTURE
    if ((caps & V4L2_CAP_META_CAPTURE) != 0 &&
        (caps & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        return 0;
    }
#endif
    return 1;
}

int DeviceScanner_FindFirstCamera(CameraDeviceInfo *info)
{
    if (info == NULL) {
        return IPC_EINVAL;
    }

    memset(info, 0, sizeof(*info));

    for (int index = 0; index < 100; ++index) {
        char path[128];
        struct v4l2_capability cap;
        unsigned int caps;
        int fd;

        snprintf(path, sizeof(path), "/dev/video%d", index);
        fd = open(path, O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }

        memset(&cap, 0, sizeof(cap));
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
            close(fd);
            continue;
        }

        caps = cap.capabilities;
#ifdef V4L2_CAP_DEVICE_CAPS
        if ((cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0) {
            caps = cap.device_caps;
        }
#endif

        if (device_scanner_is_camera(caps)) {
            snprintf(info->path, sizeof(info->path), "%s", path);
            snprintf(info->driver, sizeof(info->driver), "%s", cap.driver);
            snprintf(info->card, sizeof(info->card), "%s", cap.card);

            IPC_LOGI("[device_scanner] Found camera:");
            IPC_LOGI("[device_scanner] path: %s", info->path);
            IPC_LOGI("[device_scanner] driver: %s", info->driver);
            IPC_LOGI("[device_scanner] card: %s", info->card);

            close(fd);
            return IPC_OK;
        }

        close(fd);
    }

    return IPC_EOPEN;
}
