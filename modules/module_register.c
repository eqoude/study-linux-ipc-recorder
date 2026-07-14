#include "module_register.h"

#include "ipc_log.h"

#include <stdio.h>

#include "../capture/capture_manager.h"
#include "../converter/converter_manager.h"
#include "../encoder/encoder_manager.h"
#include "../frame_processor/frame_processor_manager.h"
#include "../muxer/muxer_manager.h"
#include "../sink/frame_sink_manager.h"
#include "../viewer/sdl_display_manager.h"

extern const CaptureOps g_fake_capture_ops;
extern const ConverterOps g_fake_converter_ops;
extern const EncoderOps g_fake_encoder_ops;
extern const MuxerOps g_fake_muxer_ops;

extern const CaptureOps g_v4l2_capture_ops;
extern const ConverterOps g_yuyv_to_yuv420_ops;
extern const EncoderOps g_h264_ffmpeg_encoder_ops;
extern const FrameProcessorOps g_osd_processor_ops;
extern const MuxerOps g_mp4_muxer_ops;
extern const MuxerOps g_rtsp_muxer_ops;
extern const FrameSinkOps g_snapshot_jpeg_sink_ops;
#ifdef ENABLE_VIEWER
extern const ViewerOps g_sdl_display_ops;
#endif

static int module_register_check(const char *module_name, int ret)
{
    if (ret != IPC_OK) {
        IPC_LOGE("[module_register] register %s failed: %s (%d)",
                 module_name,
                 IpcError_ToString(ret),
                 ret);
    }

    return ret;
}

int RegisterAllModules(void)
{
    int ret;

    ret = CaptureManager_Register(&g_fake_capture_ops);
    if (module_register_check("fake_capture", ret) != IPC_OK) {
        return ret;
    }

    ret = CaptureManager_Register(&g_v4l2_capture_ops);
    if (module_register_check("v4l2_capture", ret) != IPC_OK) {
        return ret;
    }

    ret = ConverterManager_Register(&g_fake_converter_ops);
    if (module_register_check("fake_converter", ret) != IPC_OK) {
        return ret;
    }

    ret = ConverterManager_Register(&g_yuyv_to_yuv420_ops);
    if (module_register_check("yuyv_to_yuv420", ret) != IPC_OK) {
        return ret;
    }

    ret = EncoderManager_Register(&g_fake_encoder_ops);
    if (module_register_check("fake_encoder", ret) != IPC_OK) {
        return ret;
    }

    ret = EncoderManager_Register(&g_h264_ffmpeg_encoder_ops);
    if (module_register_check("h264_ffmpeg_encoder", ret) != IPC_OK) {
        return ret;
    }

    ret = FrameProcessorManager_Register(&g_osd_processor_ops);
    if (module_register_check("osd_processor", ret) != IPC_OK) {
        return ret;
    }

    ret = MuxerManager_Register(&g_fake_muxer_ops);
    if (module_register_check("fake_muxer", ret) != IPC_OK) {
        return ret;
    }

    ret = MuxerManager_Register(&g_mp4_muxer_ops);
    if (module_register_check("mp4_muxer", ret) != IPC_OK) {
        return ret;
    }

    ret = MuxerManager_Register(&g_rtsp_muxer_ops);
    if (module_register_check("rtsp_muxer", ret) != IPC_OK) {
        return ret;
    }

#ifdef ENABLE_VIEWER
    ret = RegisterViewer("sdl", &g_sdl_display_ops);
    if (module_register_check("sdl_display", ret) != IPC_OK) {
        return ret;
    }
#endif

    ret = FrameSink_Register(&g_snapshot_jpeg_sink_ops);
    if (module_register_check("snapshot_jpeg_sink", ret) != IPC_OK) {
        return ret;
    }

    return IPC_OK;
}
