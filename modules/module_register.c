#include "module_register.h"

#include "../capture/capture_manager.h"
#include "../converter/converter_manager.h"
#include "../encoder/encoder_manager.h"
#include "../frame_processor/frame_processor_manager.h"
#include "../muxer/muxer_manager.h"
#include "../viewer/sdl_display_manager.h"

extern const CaptureOps g_fake_capture_ops;
extern const ConverterOps g_fake_converter_ops;
extern const EncoderOps g_fake_encoder_ops;
extern const FrameProcessorOps g_osd_processor_ops;
extern const MuxerOps g_fake_muxer_ops;
extern const MuxerOps g_rtsp_muxer_ops;
extern const ViewerOps g_sdl_display_ops;

extern const CaptureOps g_v4l2_capture_ops;
extern const ConverterOps g_yuyv_to_yuv420_ops;
extern const EncoderOps g_h264_ffmpeg_encoder_ops;
extern const MuxerOps g_mp4_muxer_ops;

int RegisterAllModules(void)
{
    int ret;

    ret = CaptureManager_Register(&g_fake_capture_ops);
    if (ret < 0) {
        return ret;
    }

    ret = CaptureManager_Register(&g_v4l2_capture_ops);
    if (ret < 0) {
        return ret;
    }

    ret = ConverterManager_Register(&g_fake_converter_ops);
    if (ret < 0) {
        return ret;
    }

    ret = ConverterManager_Register(&g_yuyv_to_yuv420_ops);
    if (ret < 0) {
        return ret;
    }

    ret = EncoderManager_Register(&g_fake_encoder_ops);
    if (ret < 0) {
        return ret;
    }

    ret = EncoderManager_Register(&g_h264_ffmpeg_encoder_ops);
    if (ret < 0) {
        return ret;
    }

    ret = FrameProcessorManager_Register(&g_osd_processor_ops);
    if (ret < 0) {
        return ret;
    }

    ret = MuxerManager_Register(&g_fake_muxer_ops);
    if (ret < 0) {
        return ret;
    }

    ret = MuxerManager_Register(&g_mp4_muxer_ops);
    if (ret < 0) {
        return ret;
    }

    ret = MuxerManager_Register(&g_rtsp_muxer_ops);
    if (ret < 0) {
        return ret;
    }

    ret = RegisterViewer("sdl", &g_sdl_display_ops);
    if (ret < 0) {
        return ret;
    }

    return 0;
}
