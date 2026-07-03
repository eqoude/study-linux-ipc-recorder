#ifndef IPC_RECORDER_APP_PIPELINE_H
#define IPC_RECORDER_APP_PIPELINE_H

#include <pthread.h>

#include "app_config.h"

#include "../capture/capture_manager.h"
#include "../converter/converter_manager.h"
#include "../core/thread_queue.h"
#include "../encoder/encoder_manager.h"
#include "../frame_processor/frame_processor_manager.h"
#include "../muxer/muxer_manager.h"
#include "../viewer/sdl_display_manager.h"

typedef struct {
    AppConfig config;

    CaptureManager capture;
    ConverterManager converter;
    FrameProcessorManager processor;
    EncoderManager encoder;
    MuxerManager muxer;
    MuxerManager rtsp_muxer;
    ViewerManager viewer;

    FrameQueue raw_queue;
    FrameQueue encode_queue;
    PacketQueue packet_queue;

    pthread_t capture_thread;
    pthread_t process_thread;
    pthread_t encode_thread;
    pthread_t mux_thread;

    volatile int stop;
    int error;

    int capture_inited;
    int converter_inited;
    int processor_inited;
    int viewer_inited;
    int encoder_inited;
    int muxer_inited;
    int rtsp_muxer_inited;
    int capture_opened;
    int capture_started;
    int muxer_opened;
    int rtsp_muxer_opened;
    int muxer_header_written;
    int rtsp_muxer_header_written;
    int raw_queue_inited;
    int encode_queue_inited;
    int packet_queue_inited;
    int capture_thread_started;
    int process_thread_started;
    int encode_thread_started;
    int mux_thread_started;
} AppPipeline;

int AppPipeline_Init(AppPipeline *pipeline, const AppConfig *config);
int AppPipeline_Run(AppPipeline *pipeline);
void AppPipeline_Deinit(AppPipeline *pipeline);

#endif
