#include "app_pipeline.h"

#include <stdio.h>
#include <string.h>

#include "../modules/module_register.h"

#define IPC_QUEUE_MAX_SIZE 8
#define IPC_DROP_WARMUP_FRAMES 10

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
        if (ret < 0) {
            fprintf(stderr, "Drop frame failed\n");
            pipeline->error = -1;
            app_pipeline_request_stop(pipeline);
            return NULL;
        }
        CaptureManager_ReleaseFrame(&pipeline->capture, &frame);
    }

    while (!pipeline->stop) {
        ret = CaptureManager_GetFrame(&pipeline->capture, &frame);
        if (ret < 0) {
            fprintf(stderr, "CaptureManager_GetFrame failed\n");
            pipeline->error = -1;
            app_pipeline_request_stop(pipeline);
            break;
        }

        ret = FrameQueue_Push(&pipeline->raw_queue, &frame);
        CaptureManager_ReleaseFrame(&pipeline->capture, &frame);
        if (ret < 0) {
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
        if (ret > 0) {
            break;
        }
        if (ret < 0) {
            pipeline->error = -1;
            app_pipeline_request_stop(pipeline);
            break;
        }

        ret = ConverterManager_Convert(&pipeline->converter,
                                       &raw_frame,
                                       &yuv420_frame);
        FrameQueue_UnrefFrame(&raw_frame);
        if (ret < 0) {
            fprintf(stderr, "ConverterManager_Convert failed\n");
            pipeline->error = -1;
            app_pipeline_request_stop(pipeline);
            break;
        }

        output_frame = &yuv420_frame;
        if (pipeline->config.enable_processor) {
            ret = FrameProcessorManager_Process(&pipeline->processor,
                                                &yuv420_frame,
                                                &processed_frame);
            if (ret < 0) {
                fprintf(stderr, "FrameProcessorManager_Process failed\n");
                pipeline->error = -1;
                app_pipeline_request_stop(pipeline);
                break;
            }
            output_frame = &processed_frame;
        }

        if (pipeline->config.enable_preview) {
            ret = ViewerManager_Display(&pipeline->viewer, output_frame);
            if (ret > 0) {
                app_pipeline_request_stop(pipeline);
                break;
            }
            if (ret < 0) {
                fprintf(stderr, "ViewerManager_Display failed\n");
                pipeline->error = -1;
                app_pipeline_request_stop(pipeline);
                break;
            }
        }

        if (app_pipeline_need_encode(pipeline)) {
            ret = FrameQueue_Push(&pipeline->encode_queue, output_frame);
            if (ret < 0) {
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
        if (ret > 0) {
            break;
        }
        if (ret < 0) {
            pipeline->error = -1;
            app_pipeline_request_stop(pipeline);
            break;
        }

        ret = EncoderManager_Encode(&pipeline->encoder, &frame, &packet);
        FrameQueue_UnrefFrame(&frame);
        if (ret == 0) {
            if (PacketQueue_Push(&pipeline->packet_queue, &packet) < 0) {
                MediaPacket_Unref(&packet);
                break;
            }
            MediaPacket_Unref(&packet);
        } else if (ret > 0) {
        } else {
            fprintf(stderr, "EncoderManager_Encode failed\n");
            pipeline->error = -1;
            MediaPacket_Unref(&packet);
            app_pipeline_request_stop(pipeline);
            break;
        }
    }

    while (!pipeline->stop &&
           EncoderManager_Flush(&pipeline->encoder, &packet) == 0) {
        if (PacketQueue_Push(&pipeline->packet_queue, &packet) < 0) {
            MediaPacket_Unref(&packet);
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
        if (ret > 0) {
            break;
        }
        if (ret < 0) {
            pipeline->error = -1;
            app_pipeline_request_stop(pipeline);
            break;
        }

        if (pipeline->config.enable_record) {
            ret = MuxerManager_WritePacket(&pipeline->muxer, &packet);
            if (ret < 0) {
                fprintf(stderr, "MuxerManager_WritePacket failed\n");
                MediaPacket_Unref(&packet);
                pipeline->error = -1;
                app_pipeline_request_stop(pipeline);
                break;
            }
        }

        if (pipeline->config.enable_rtsp) {
            ret = MuxerManager_WritePacket(&pipeline->rtsp_muxer, &packet);
            if (ret < 0) {
                fprintf(stderr, "RtspMuxer WritePacket failed\n");
                MediaPacket_Unref(&packet);
                pipeline->error = -1;
                app_pipeline_request_stop(pipeline);
                break;
            }
        }

        MediaPacket_Unref(&packet);
        if (ret < 0) {
            pipeline->error = -1;
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
        return -1;
    }

    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->config = *config;

    ret = RegisterAllModules();
    if (ret < 0) {
        fprintf(stderr, "RegisterAllModules failed\n");
        return -1;
    }

    CaptureConfig capture_config = {
        .device_path = pipeline->config.device_path,
        .width = pipeline->config.width,
        .height = pipeline->config.height,
        .pixel_format = PIX_FMT_YUYV422,
        .fps = pipeline->config.fps,
    };

    ret = CaptureManager_Init(&pipeline->capture,
                              pipeline->config.capture_name,
                              &capture_config);
    if (ret < 0) {
        fprintf(stderr, "CaptureManager_Init failed\n");
        return -1;
    }
    pipeline->capture_inited = 1;

    ConverterConfig converter_config = {
        .src_width = pipeline->config.width,
        .src_height = pipeline->config.height,
        .src_format = PIX_FMT_YUYV422,
        .dst_width = pipeline->config.width,
        .dst_height = pipeline->config.height,
        .dst_format = PIX_FMT_YUV420P,
    };

    ret = ConverterManager_Init(&pipeline->converter,
                                pipeline->config.converter_name,
                                &converter_config);
    if (ret < 0) {
        fprintf(stderr, "ConverterManager_Init failed\n");
        return -1;
    }
    pipeline->converter_inited = 1;

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
            fprintf(stderr, "FrameProcessorManager_Init failed\n");
            return -1;
        }
        pipeline->processor_inited = 1;
    }

    if (pipeline->config.enable_preview) {
        ViewerConfig viewer_config = {
            .width = pipeline->config.width,
            .height = pipeline->config.height,
            .title = "IPC Preview",
        };

        ret = ViewerManager_Init(&pipeline->viewer,
                                 pipeline->config.viewer_name,
                                 &viewer_config);
        if (ret < 0) {
            fprintf(stderr, "ViewerManager_Init failed\n");
            return -1;
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
            fprintf(stderr, "EncoderManager_Init failed\n");
            return -1;
        }
        pipeline->encoder_inited = 1;
    }

    if (pipeline->config.enable_record) {
        MuxerConfig muxer_config = {
            .output_path = pipeline->config.output_path,
            .format_name = pipeline->config.muxer_name,
            .width = pipeline->config.width,
            .height = pipeline->config.height,
            .fps = pipeline->config.fps,
            .codec = CODEC_H264,
        };

        ret = MuxerManager_Init(&pipeline->muxer,
                                pipeline->config.muxer_name,
                                &muxer_config);
        if (ret < 0) {
            fprintf(stderr, "MuxerManager_Init failed\n");
            return -1;
        }
        pipeline->muxer_inited = 1;
    }

    if (pipeline->config.enable_rtsp) {
        MuxerConfig rtsp_muxer_config = {
            .output_path = pipeline->config.rtsp_url,
            .format_name = "rtsp",
            .width = pipeline->config.width,
            .height = pipeline->config.height,
            .fps = pipeline->config.fps,
            .codec = CODEC_H264,
        };

        ret = MuxerManager_Init(&pipeline->rtsp_muxer,
                                "rtsp",
                                &rtsp_muxer_config);
        if (ret < 0) {
            fprintf(stderr, "RtspMuxer Init failed\n");
            return -1;
        }
        pipeline->rtsp_muxer_inited = 1;
    }

    if (FrameQueue_Init(&pipeline->raw_queue, IPC_QUEUE_MAX_SIZE) < 0) {
        return -1;
    }
    pipeline->raw_queue_inited = 1;

    if (app_pipeline_need_encode(pipeline)) {
        if (FrameQueue_Init(&pipeline->encode_queue, IPC_QUEUE_MAX_SIZE) < 0) {
            return -1;
        }
        pipeline->encode_queue_inited = 1;

        if (PacketQueue_Init(&pipeline->packet_queue, IPC_QUEUE_MAX_SIZE) < 0) {
            return -1;
        }
        pipeline->packet_queue_inited = 1;
    }

    ret = CaptureManager_Open(&pipeline->capture);
    if (ret < 0) {
        fprintf(stderr, "CaptureManager_Open failed\n");
        return -1;
    }
    pipeline->capture_opened = 1;

    if (pipeline->config.enable_record) {
        ret = MuxerManager_Open(&pipeline->muxer);
        if (ret < 0) {
            fprintf(stderr, "MuxerManager_Open failed\n");
            return -1;
        }
        pipeline->muxer_opened = 1;

        ret = MuxerManager_WriteHeader(&pipeline->muxer);
        if (ret < 0) {
            fprintf(stderr, "MuxerManager_WriteHeader failed\n");
            return -1;
        }
        pipeline->muxer_header_written = 1;
    }

    if (pipeline->config.enable_rtsp) {
        ret = MuxerManager_Open(&pipeline->rtsp_muxer);
        if (ret < 0) {
            fprintf(stderr, "RtspMuxer Open failed\n");
            return -1;
        }
        pipeline->rtsp_muxer_opened = 1;

        ret = MuxerManager_WriteHeader(&pipeline->rtsp_muxer);
        if (ret < 0) {
            fprintf(stderr, "RtspMuxer WriteHeader failed\n");
            return -1;
        }
        pipeline->rtsp_muxer_header_written = 1;
    }

    return 0;
}

int AppPipeline_Run(AppPipeline *pipeline)
{
    int ret;

    if (pipeline == NULL) {
        return -1;
    }

    ret = CaptureManager_Start(&pipeline->capture);
    if (ret < 0) {
        fprintf(stderr, "CaptureManager_Start failed\n");
        return -1;
    }
    pipeline->capture_started = 1;

    if (app_pipeline_need_encode(pipeline)) {
        if (pthread_create(&pipeline->mux_thread,
                           NULL,
                           mux_thread_main,
                           pipeline) != 0) {
            app_pipeline_request_stop(pipeline);
            return -1;
        }
        pipeline->mux_thread_started = 1;

        if (pthread_create(&pipeline->encode_thread,
                           NULL,
                           encode_thread_main,
                           pipeline) != 0) {
            app_pipeline_request_stop(pipeline);
            return -1;
        }
        pipeline->encode_thread_started = 1;
    }

    if (pthread_create(&pipeline->process_thread,
                       NULL,
                       process_thread_main,
                       pipeline) != 0) {
        app_pipeline_request_stop(pipeline);
        return -1;
    }
    pipeline->process_thread_started = 1;

    if (pthread_create(&pipeline->capture_thread,
                       NULL,
                       capture_thread_main,
                       pipeline) != 0) {
        app_pipeline_request_stop(pipeline);
        return -1;
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

    return pipeline->error < 0 ? -1 : 0;
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
