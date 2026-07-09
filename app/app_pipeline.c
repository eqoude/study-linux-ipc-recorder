#include "app_pipeline.h"

#include "ipc_log.h"

#include <stdio.h>
#include <string.h>

#include "../modules/module_register.h"
#include "../sink/snapshot_jpeg_sink.h"

#define IPC_QUEUE_MAX_SIZE 8
#define IPC_DROP_WARMUP_FRAMES 10
#define IPC_SNAPSHOT_SINK_NAME "snapshot_jpeg"
#define IPC_SNAPSHOT_PATH "edge_ai_camera_test/snapshot.jpg"
#define IPC_SNAPSHOT_INTERVAL_FRAMES 30

static void app_pipeline_log_error(const char *operation, int ret)
{
    IPC_LOGE("[pipeline] %s failed: %s (%d)",
             operation,
             IpcError_ToString(ret),
             ret);
}

static void app_pipeline_log_warning(const char *operation, int ret)
{
    IPC_LOGW("[pipeline] warning: %s: %s (%d)",
             operation,
             IpcError_ToString(ret),
             ret);
}

static int app_pipeline_need_encode(const AppPipeline *pipeline)
{
    return pipeline != NULL &&
           (pipeline->config.enable_record || pipeline->config.enable_rtsp);
}

static void app_pipeline_request_stop(AppPipeline *pipeline)
{
    if (pipeline == NULL) {
        return;
    }

    pipeline->stop = 1;
    if (pipeline->raw_queue_inited) {
        FrameQueue_Close(&pipeline->raw_queue);
    }
    if (pipeline->encode_queue_inited) {
        FrameQueue_Close(&pipeline->encode_queue);
    }
    if (pipeline->packet_queue_inited) {
        PacketQueue_Close(&pipeline->packet_queue);
    }
}

static void *capture_thread_main(void *arg)
{
    AppPipeline *pipeline = (AppPipeline *)arg;
    MediaFrame frame;
    int ret;
    int frame_count = 0;

    memset(&frame, 0, sizeof(frame));

    for (int i = 0; i < IPC_DROP_WARMUP_FRAMES && !pipeline->stop; ++i) {
        ret = CaptureManager_GetFrame(&pipeline->capture, &frame);
        if (ret == IPC_EAGAIN) {
            continue;
        }
        if (ret == IPC_EOF) {
            FrameQueue_Close(&pipeline->raw_queue);
            return NULL;
        }
        if (ret != IPC_OK) {
            app_pipeline_log_error("Drop warmup frame", ret);
            pipeline->error = ret;
            app_pipeline_request_stop(pipeline);
            return NULL;
        }
        ret = CaptureManager_ReleaseFrame(&pipeline->capture, &frame);
        if (ret != IPC_OK) {
            app_pipeline_log_error("CaptureManager_ReleaseFrame(warmup)", ret);
            pipeline->error = ret;
            app_pipeline_request_stop(pipeline);
            return NULL;
        }
    }

    while (!pipeline->stop) {
        ret = CaptureManager_GetFrame(&pipeline->capture, &frame);
        if (ret == IPC_EAGAIN) {
            continue;
        }
        if (ret == IPC_EOF) {
            break;
        }
        if (ret != IPC_OK) {
            app_pipeline_log_error("CaptureManager_GetFrame", ret);
            pipeline->error = ret;
            app_pipeline_request_stop(pipeline);
            break;
        }

        ret = FrameQueue_Push(&pipeline->raw_queue, &frame);
        int release_ret = CaptureManager_ReleaseFrame(&pipeline->capture, &frame);
        if (release_ret != IPC_OK) {
            app_pipeline_log_error("CaptureManager_ReleaseFrame", release_ret);
            pipeline->error = release_ret;
            app_pipeline_request_stop(pipeline);
            break;
        }
        if (ret == IPC_EOF) {
            break;
        }
        if (ret != IPC_OK) {
            app_pipeline_log_error("FrameQueue_Push(raw_queue)", ret);
            pipeline->error = ret;
            app_pipeline_request_stop(pipeline);
            break;
        }

        frame_count++;
        if (pipeline->config.max_frames > 0 &&
            frame_count >= pipeline->config.max_frames) {
            break;
        }
    }

    FrameQueue_Close(&pipeline->raw_queue);
    return NULL;
}

static void *process_thread_main(void *arg)
{
    AppPipeline *pipeline = (AppPipeline *)arg;
    MediaFrame raw_frame;
    MediaFrame yuv420_frame;
    MediaFrame processed_frame;
    MediaFrame *output_frame;
    int ret;

    memset(&raw_frame, 0, sizeof(raw_frame));
    memset(&yuv420_frame, 0, sizeof(yuv420_frame));
    memset(&processed_frame, 0, sizeof(processed_frame));

    while (!pipeline->stop) {
        ret = FrameQueue_Pop(&pipeline->raw_queue, &raw_frame);
        if (ret == IPC_EOF) {
            break;
        }
        if (ret == IPC_EAGAIN) {
            continue;
        }
        if (ret != IPC_OK) {
            app_pipeline_log_error("FrameQueue_Pop(raw_queue)", ret);
            pipeline->error = ret;
            app_pipeline_request_stop(pipeline);
            break;
        }

        ret = ConverterManager_Convert(&pipeline->converter,
                                       &raw_frame,
                                       &yuv420_frame);
        FrameQueue_UnrefFrame(&raw_frame);
        if (ret == IPC_EAGAIN) {
            continue;
        }
        if (ret == IPC_EOF) {
            break;
        }
        if (ret != IPC_OK) {
            app_pipeline_log_error("ConverterManager_Convert", ret);
            pipeline->error = ret;
            app_pipeline_request_stop(pipeline);
            break;
        }

        output_frame = &yuv420_frame;
        if (pipeline->config.enable_processor) {
            ret = FrameProcessorManager_Process(&pipeline->processor,
                                                &yuv420_frame,
                                                &processed_frame);
            if (ret < 0) {
                app_pipeline_log_error("FrameProcessorManager_Process", ret);
                pipeline->error = 1;
                app_pipeline_request_stop(pipeline);
                break;
            }
            output_frame = &processed_frame;
        }

        if (pipeline->snapshot_sink_inited &&
            pipeline->snapshot_sink_ops != NULL &&
            pipeline->snapshot_sink_ops->write != NULL) {
            ret = pipeline->snapshot_sink_ops->write(pipeline->snapshot_sink_ctx,
                                                     output_frame);
            if (ret != IPC_OK) {
                app_pipeline_log_warning("FrameSink(snapshot_jpeg)->write", ret);
            }
        }

        if (pipeline->config.enable_preview) {
            ret = ViewerManager_Display(&pipeline->viewer, output_frame);
            if (ret == IPC_EOF) {
                IPC_LOGI("[pipeline] preview closed by user: %s (%d)",
                         IpcError_ToString(ret),
                         ret);
                app_pipeline_request_stop(pipeline);
                break;
            }
            if (ret != IPC_OK) {
                app_pipeline_log_error("ViewerManager_Display", ret);
                pipeline->error = ret;
                app_pipeline_request_stop(pipeline);
                break;
            }
        }

        if (app_pipeline_need_encode(pipeline)) {
            ret = FrameQueue_Push(&pipeline->encode_queue, output_frame);
            if (ret == IPC_EOF) {
                break;
            }
            if (ret != IPC_OK) {
                app_pipeline_log_error("FrameQueue_Push(encode_queue)", ret);
                pipeline->error = ret;
                app_pipeline_request_stop(pipeline);
                break;
            }
        }
    }

    if (app_pipeline_need_encode(pipeline)) {
        FrameQueue_Close(&pipeline->encode_queue);
    }

    return NULL;
}

static void *encode_thread_main(void *arg)
{
    AppPipeline *pipeline = (AppPipeline *)arg;
    MediaFrame frame;
    MediaPacket packet;
    int ret;

    memset(&frame, 0, sizeof(frame));
    memset(&packet, 0, sizeof(packet));

    while (!pipeline->stop) {
        ret = FrameQueue_Pop(&pipeline->encode_queue, &frame);
        if (ret == IPC_EOF) {
            break;
        }
        if (ret == IPC_EAGAIN) {
            continue;
        }
        if (ret != IPC_OK) {
            app_pipeline_log_error("FrameQueue_Pop(encode_queue)", ret);
            pipeline->error = ret;
            app_pipeline_request_stop(pipeline);
            break;
        }

        ret = EncoderManager_Encode(&pipeline->encoder, &frame, &packet);
        FrameQueue_UnrefFrame(&frame);
        if (ret == IPC_OK) {
            ret = PacketQueue_Push(&pipeline->packet_queue, &packet);
            if (ret == IPC_EOF) {
                MediaPacket_Unref(&packet);
                break;
            }
            if (ret != IPC_OK) {
                app_pipeline_log_error("PacketQueue_Push(packet_queue)", ret);
                pipeline->error = ret;
                MediaPacket_Unref(&packet);
                app_pipeline_request_stop(pipeline);
                break;
            }
            MediaPacket_Unref(&packet);
        } else if (ret == IPC_EAGAIN) {
            continue;
        } else if (ret == IPC_EOF) {
            break;
        } else {
            app_pipeline_log_error("EncoderManager_Encode", ret);
            pipeline->error = ret;
            MediaPacket_Unref(&packet);
            app_pipeline_request_stop(pipeline);
            break;
        }
    }

    while (!pipeline->stop) {
        ret = EncoderManager_Flush(&pipeline->encoder, &packet);
        if (ret == IPC_EAGAIN) {
            continue;
        }
        if (ret == IPC_EOF) {
            break;
        }
        if (ret != IPC_OK) {
            app_pipeline_log_error("EncoderManager_Flush", ret);
            pipeline->error = ret;
            MediaPacket_Unref(&packet);
            app_pipeline_request_stop(pipeline);
            break;
        }

        ret = PacketQueue_Push(&pipeline->packet_queue, &packet);
        if (ret == IPC_EOF) {
            MediaPacket_Unref(&packet);
            break;
        }
        if (ret != IPC_OK) {
            app_pipeline_log_error("PacketQueue_Push(packet_queue flush)", ret);
            pipeline->error = ret;
            MediaPacket_Unref(&packet);
            app_pipeline_request_stop(pipeline);
            break;
        }
        MediaPacket_Unref(&packet);
    }

    MediaPacket_Unref(&packet);
    PacketQueue_Close(&pipeline->packet_queue);
    return NULL;
}

static void *mux_thread_main(void *arg)
{
    AppPipeline *pipeline = (AppPipeline *)arg;
    MediaPacket packet;
    int ret;

    memset(&packet, 0, sizeof(packet));

    while (!pipeline->stop) {
        ret = PacketQueue_Pop(&pipeline->packet_queue, &packet);
        if (ret == IPC_EOF) {
            break;
        }
        if (ret == IPC_EAGAIN) {
            continue;
        }
        if (ret != IPC_OK) {
            app_pipeline_log_error("PacketQueue_Pop(packet_queue)", ret);
            pipeline->error = ret;
            app_pipeline_request_stop(pipeline);
            break;
        }

        if (pipeline->config.enable_record) {
            ret = MuxerManager_WritePacket(&pipeline->muxer, &packet);
            if (ret == IPC_EOF) {
                MediaPacket_Unref(&packet);
                break;
            }
            if (ret < 0) {
                app_pipeline_log_error("MuxerManager_WritePacket(mp4)", ret);
                MediaPacket_Unref(&packet);
                pipeline->error = ret;
                app_pipeline_request_stop(pipeline);
                break;
            }
        }

        if (pipeline->config.enable_rtsp) {
            ret = MuxerManager_WritePacket(&pipeline->rtsp_muxer, &packet);
            if (ret == IPC_EOF) {
                MediaPacket_Unref(&packet);
                break;
            }
            if (ret < 0) {
                app_pipeline_log_error("MuxerManager_WritePacket(rtsp)", ret);
                MediaPacket_Unref(&packet);
                pipeline->error = ret;
                app_pipeline_request_stop(pipeline);
                break;
            }
        }

        MediaPacket_Unref(&packet);
        if (ret < 0) {
            app_pipeline_log_error("Mux thread write packet", ret);
            pipeline->error = ret;
            app_pipeline_request_stop(pipeline);
            break;
        }
    }

    MediaPacket_Unref(&packet);
    return NULL;
}

int AppPipeline_Init(AppPipeline *pipeline, const AppConfig *config)
{
    int ret;

    if (pipeline == NULL || config == NULL) {
        return IPC_EINVAL;
    }

    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->config = *config;

    ret = RegisterAllModules();
    if (ret != IPC_OK) {
        app_pipeline_log_error("RegisterAllModules", ret);
        pipeline->error = 1;
        return ret;
    }

    CaptureConfig capture_config;

    memset(&capture_config, 0, sizeof(capture_config));
    strncpy(capture_config.device_path,
            pipeline->config.device_path,
            sizeof(capture_config.device_path) - 1U);
    capture_config.width = pipeline->config.width;
    capture_config.height = pipeline->config.height;
    capture_config.pixel_format = pipeline->config.pixel_format;
    capture_config.fps = pipeline->config.fps;

    ret = CaptureManager_Init(&pipeline->capture,
                              pipeline->config.capture_name,
                              &capture_config);
    if (ret != IPC_OK) {
        app_pipeline_log_error("CaptureManager_Init", ret);
        return ret;
    }
    pipeline->capture_inited = 1;

    ConverterConfig converter_config = {
        .src_width = pipeline->config.width,
        .src_height = pipeline->config.height,
        .src_format = pipeline->config.pixel_format,
        .dst_width = pipeline->config.width,
        .dst_height = pipeline->config.height,
        .dst_format = PIX_FMT_YUV420P,
    };

    ret = ConverterManager_Init(&pipeline->converter,
                                pipeline->config.converter_name,
                                &converter_config);
    if (ret != IPC_OK) {
        app_pipeline_log_error("ConverterManager_Init", ret);
        return ret;
    }
    pipeline->converter_inited = 1;

    SnapshotJpegSinkConfig snapshot_config = {
        .output_path = IPC_SNAPSHOT_PATH,
        .interval_frames = IPC_SNAPSHOT_INTERVAL_FRAMES,
    };

    pipeline->snapshot_sink_ops = FrameSink_Find(IPC_SNAPSHOT_SINK_NAME);
    if (pipeline->snapshot_sink_ops == NULL) {
        app_pipeline_log_warning("FrameSink_Find(snapshot_jpeg)", IPC_ESTATE);
    } else if (pipeline->snapshot_sink_ops->init != NULL) {
        ret = pipeline->snapshot_sink_ops->init(&pipeline->snapshot_sink_ctx,
                                                &snapshot_config);
        if (ret != IPC_OK) {
            app_pipeline_log_warning("FrameSink(snapshot_jpeg)->init", ret);
        } else {
            pipeline->snapshot_sink_inited = 1;
        }
    } else {
        pipeline->snapshot_sink_inited = 1;
    }

    if (pipeline->config.enable_processor) {
        FrameProcessorConfig processor_config = {
            .x = 16,
            .y = 16,
            .width = 96,
            .height = 24,
        };

        ret = FrameProcessorManager_Init(&pipeline->processor,
                                         pipeline->config.processor_name,
                                         &processor_config);
        if (ret < 0) {
            app_pipeline_log_error("FrameProcessorManager_Init", ret);
            return ret;
        }
        pipeline->processor_inited = 1;
    }

    if (pipeline->config.enable_preview) {
        ViewerConfig viewer_config;

        memset(&viewer_config, 0, sizeof(viewer_config));
        viewer_config.width = pipeline->config.width;
        viewer_config.height = pipeline->config.height;
        strncpy(viewer_config.title, "IPC Preview", sizeof(viewer_config.title) - 1U);

        ret = ViewerManager_Init(&pipeline->viewer,
                                 pipeline->config.viewer_name,
                                 &viewer_config);
        if (ret < 0) {
            app_pipeline_log_error("ViewerManager_Init", ret);
            return ret;
        }
        pipeline->viewer_inited = 1;
    }

    if (app_pipeline_need_encode(pipeline)) {
        EncoderConfig encoder_config = {
            .width = pipeline->config.width,
            .height = pipeline->config.height,
            .fps = pipeline->config.fps,
            .bitrate = 800000,
            .gop = pipeline->config.fps,
            .codec = CODEC_H264,
        };

        ret = EncoderManager_Init(&pipeline->encoder,
                                  pipeline->config.encoder_name,
                                  &encoder_config);
        if (ret < 0) {
            app_pipeline_log_error("EncoderManager_Init", ret);
            return ret;
        }
        pipeline->encoder_inited = 1;
    }

    if (pipeline->config.enable_record) {
        MuxerConfig muxer_config;

        memset(&muxer_config, 0, sizeof(muxer_config));
        strncpy(muxer_config.output_path,
                pipeline->config.output_path,
                sizeof(muxer_config.output_path) - 1U);
        strncpy(muxer_config.format_name,
                pipeline->config.muxer_name,
                sizeof(muxer_config.format_name) - 1U);
        muxer_config.width = pipeline->config.width;
        muxer_config.height = pipeline->config.height;
        muxer_config.fps = pipeline->config.fps;
        muxer_config.codec = CODEC_H264;

        ret = MuxerManager_Init(&pipeline->muxer,
                                pipeline->config.muxer_name,
                                &muxer_config);
        if (ret != IPC_OK) {
            app_pipeline_log_error("MuxerManager_Init", ret);
            return ret;
        }
        pipeline->muxer_inited = 1;
    }

    if (pipeline->config.enable_rtsp) {
        MuxerConfig rtsp_muxer_config;

        memset(&rtsp_muxer_config, 0, sizeof(rtsp_muxer_config));
        strncpy(rtsp_muxer_config.output_path,
                pipeline->config.rtsp_url,
                sizeof(rtsp_muxer_config.output_path) - 1U);
        strncpy(rtsp_muxer_config.format_name,
                "rtsp",
                sizeof(rtsp_muxer_config.format_name) - 1U);
        rtsp_muxer_config.width = pipeline->config.width;
        rtsp_muxer_config.height = pipeline->config.height;
        rtsp_muxer_config.fps = pipeline->config.fps;
        rtsp_muxer_config.codec = CODEC_H264;

        ret = MuxerManager_Init(&pipeline->rtsp_muxer,
                                "rtsp",
                                &rtsp_muxer_config);
        if (ret != IPC_OK) {
            app_pipeline_log_error("MuxerManager_Init(rtsp)", ret);
            return ret;
        }
        pipeline->rtsp_muxer_inited = 1;
    }

    ret = FrameQueue_Init(&pipeline->raw_queue, IPC_QUEUE_MAX_SIZE);
    if (ret != IPC_OK) {
        app_pipeline_log_error("FrameQueue_Init(raw_queue)", ret);
        return ret;
    }
    pipeline->raw_queue_inited = 1;

    if (app_pipeline_need_encode(pipeline)) {
        ret = FrameQueue_Init(&pipeline->encode_queue, IPC_QUEUE_MAX_SIZE);
        if (ret != IPC_OK) {
            app_pipeline_log_error("FrameQueue_Init(encode_queue)", ret);
            return ret;
        }
        pipeline->encode_queue_inited = 1;

        ret = PacketQueue_Init(&pipeline->packet_queue, IPC_QUEUE_MAX_SIZE);
        if (ret != IPC_OK) {
            app_pipeline_log_error("PacketQueue_Init(packet_queue)", ret);
            return ret;
        }
        pipeline->packet_queue_inited = 1;
    }

    ret = CaptureManager_Open(&pipeline->capture);
    if (ret != IPC_OK) {
        app_pipeline_log_error("CaptureManager_Open", ret);
        return ret;
    }
    pipeline->capture_opened = 1;

    if (pipeline->config.enable_record) {
        ret = MuxerManager_Open(&pipeline->muxer);
        if (ret != IPC_OK) {
            app_pipeline_log_error("MuxerManager_Open(mp4)", ret);
            return ret;
        }
        pipeline->muxer_opened = 1;

        ret = MuxerManager_WriteHeader(&pipeline->muxer);
        if (ret != IPC_OK) {
            app_pipeline_log_error("MuxerManager_WriteHeader(mp4)", ret);
            return ret;
        }
        pipeline->muxer_header_written = 1;
    }

    if (pipeline->config.enable_rtsp) {
        ret = MuxerManager_Open(&pipeline->rtsp_muxer);
        if (ret != IPC_OK) {
            app_pipeline_log_error("MuxerManager_Open(rtsp)", ret);
            return ret;
        }
        pipeline->rtsp_muxer_opened = 1;

        ret = MuxerManager_WriteHeader(&pipeline->rtsp_muxer);
        if (ret != IPC_OK) {
            app_pipeline_log_error("MuxerManager_WriteHeader(rtsp)", ret);
            return ret;
        }
        pipeline->rtsp_muxer_header_written = 1;
    }

    return IPC_OK;
}

int AppPipeline_Run(AppPipeline *pipeline)
{
    int ret;

    if (pipeline == NULL) {
        return IPC_EINVAL;
    }

    ret = CaptureManager_Start(&pipeline->capture);
    if (ret != IPC_OK) {
        app_pipeline_log_error("CaptureManager_Start", ret);
        return ret;
    }
    pipeline->capture_started = 1;

    if (app_pipeline_need_encode(pipeline)) {
        if (pthread_create(&pipeline->mux_thread,
                           NULL,
                           mux_thread_main,
                           pipeline) != 0) {
            app_pipeline_request_stop(pipeline);
            app_pipeline_log_error("pthread_create(mux_thread)", IPC_ETHREAD);
            return IPC_ETHREAD;
        }
        pipeline->mux_thread_started = 1;

        if (pthread_create(&pipeline->encode_thread,
                           NULL,
                           encode_thread_main,
                           pipeline) != 0) {
            app_pipeline_request_stop(pipeline);
            app_pipeline_log_error("pthread_create(encode_thread)", IPC_ETHREAD);
            return IPC_ETHREAD;
        }
        pipeline->encode_thread_started = 1;
    }

    if (pthread_create(&pipeline->process_thread,
                       NULL,
                       process_thread_main,
                       pipeline) != 0) {
        app_pipeline_request_stop(pipeline);
        app_pipeline_log_error("pthread_create(process_thread)", IPC_ETHREAD);
        return IPC_ETHREAD;
    }
    pipeline->process_thread_started = 1;

    if (pthread_create(&pipeline->capture_thread,
                       NULL,
                       capture_thread_main,
                       pipeline) != 0) {
        app_pipeline_request_stop(pipeline);
        app_pipeline_log_error("pthread_create(capture_thread)", IPC_ETHREAD);
        return IPC_ETHREAD;
    }
    pipeline->capture_thread_started = 1;

    if (pipeline->capture_thread_started) {
        pthread_join(pipeline->capture_thread, NULL);
        pipeline->capture_thread_started = 0;
    }
    if (pipeline->process_thread_started) {
        pthread_join(pipeline->process_thread, NULL);
        pipeline->process_thread_started = 0;
    }
    if (pipeline->encode_thread_started) {
        pthread_join(pipeline->encode_thread, NULL);
        pipeline->encode_thread_started = 0;
    }
    if (pipeline->mux_thread_started) {
        pthread_join(pipeline->mux_thread, NULL);
        pipeline->mux_thread_started = 0;
    }

    return pipeline->error < 0 ? pipeline->error : IPC_OK;
}

void AppPipeline_Deinit(AppPipeline *pipeline)
{
    if (pipeline == NULL) {
        return;
    }

    app_pipeline_request_stop(pipeline);

    if (pipeline->capture_thread_started) {
        pthread_join(pipeline->capture_thread, NULL);
        pipeline->capture_thread_started = 0;
    }
    if (pipeline->process_thread_started) {
        pthread_join(pipeline->process_thread, NULL);
        pipeline->process_thread_started = 0;
    }
    if (pipeline->encode_thread_started) {
        pthread_join(pipeline->encode_thread, NULL);
        pipeline->encode_thread_started = 0;
    }
    if (pipeline->mux_thread_started) {
        pthread_join(pipeline->mux_thread, NULL);
        pipeline->mux_thread_started = 0;
    }

    if (pipeline->capture_started) {
        CaptureManager_Stop(&pipeline->capture);
        pipeline->capture_started = 0;
    }

    if (pipeline->muxer_header_written) {
        MuxerManager_WriteTrailer(&pipeline->muxer);
        pipeline->muxer_header_written = 0;
    }

    if (pipeline->rtsp_muxer_header_written) {
        MuxerManager_WriteTrailer(&pipeline->rtsp_muxer);
        pipeline->rtsp_muxer_header_written = 0;
    }

    if (pipeline->muxer_opened) {
        MuxerManager_Close(&pipeline->muxer);
        pipeline->muxer_opened = 0;
    }

    if (pipeline->rtsp_muxer_opened) {
        MuxerManager_Close(&pipeline->rtsp_muxer);
        pipeline->rtsp_muxer_opened = 0;
    }

    if (pipeline->capture_opened) {
        CaptureManager_Close(&pipeline->capture);
        pipeline->capture_opened = 0;
    }

    if (pipeline->packet_queue_inited) {
        PacketQueue_Deinit(&pipeline->packet_queue);
        pipeline->packet_queue_inited = 0;
    }
    if (pipeline->encode_queue_inited) {
        FrameQueue_Deinit(&pipeline->encode_queue);
        pipeline->encode_queue_inited = 0;
    }
    if (pipeline->raw_queue_inited) {
        FrameQueue_Deinit(&pipeline->raw_queue);
        pipeline->raw_queue_inited = 0;
    }

    if (pipeline->muxer_inited) {
        MuxerManager_Deinit(&pipeline->muxer);
        pipeline->muxer_inited = 0;
    }

    if (pipeline->rtsp_muxer_inited) {
        MuxerManager_Deinit(&pipeline->rtsp_muxer);
        pipeline->rtsp_muxer_inited = 0;
    }

    if (pipeline->encoder_inited) {
        EncoderManager_Deinit(&pipeline->encoder);
        pipeline->encoder_inited = 0;
    }

    if (pipeline->viewer_inited) {
        ViewerManager_Deinit(&pipeline->viewer);
        pipeline->viewer_inited = 0;
    }

    if (pipeline->snapshot_sink_inited &&
        pipeline->snapshot_sink_ops != NULL &&
        pipeline->snapshot_sink_ops->deinit != NULL) {
        pipeline->snapshot_sink_ops->deinit(pipeline->snapshot_sink_ctx);
        pipeline->snapshot_sink_ctx = NULL;
        pipeline->snapshot_sink_inited = 0;
    }

    if (pipeline->processor_inited) {
        FrameProcessorManager_Deinit(&pipeline->processor);
        pipeline->processor_inited = 0;
    }

    if (pipeline->converter_inited) {
        ConverterManager_Deinit(&pipeline->converter);
        pipeline->converter_inited = 0;
    }

    if (pipeline->capture_inited) {
        CaptureManager_Deinit(&pipeline->capture);
        pipeline->capture_inited = 0;
    }
}
