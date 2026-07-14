TARGET := ipc_recorder

CC := gcc

ENABLE_VIEWER ?= 0

BUILD_DIR := build
BIN_DIR := bin
OUTPUT_DIR := output

FFMPEG_CFLAGS := $(shell pkg-config --cflags libavcodec libavformat libavutil libswscale)
FFMPEG_LIBS := $(shell pkg-config --libs libavcodec libavformat libavutil libswscale)

CFLAGS := -Wall -Wextra -g -O0 \
          -Icore \
          -Icapture \
          -Iconverter \
          -Iencoder \
          -Iframe_processor \
          -Imuxer \
          -Imodules \
          -Isink \
          $(FFMPEG_CFLAGS) \
          $(CFLAGS_EXTRA)

LIBS := $(FFMPEG_LIBS)
LDFLAGS := -pthread

SRCS := \
    app/main.c \
    app/app_config.c \
    app/app_pipeline.c \
    $(wildcard core/*.c) \
    $(wildcard capture/*.c) \
    $(wildcard converter/*.c) \
    $(wildcard encoder/*.c) \
    $(wildcard frame_processor/*.c) \
    $(wildcard muxer/*.c) \
    $(wildcard modules/*.c) \
    $(wildcard sink/*.c)

ifeq ($(ENABLE_VIEWER),1)
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)
CFLAGS += -DENABLE_VIEWER -Iviewer $(SDL_CFLAGS)
LIBS += $(SDL_LIBS)
SRCS += $(wildcard viewer/*.c)
endif

OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

TARGET_PATH := $(BIN_DIR)/$(TARGET)
TEST_BIN_DIR := $(BUILD_DIR)/tests
ERROR_TESTS := \
    $(TEST_BIN_DIR)/test_ipc_error \
    $(TEST_BIN_DIR)/test_media_packet \
    $(TEST_BIN_DIR)/test_thread_queue

all: $(TARGET_PATH)

$(TARGET_PATH): $(OBJS)
	@mkdir -p $(BIN_DIR) $(OUTPUT_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS) $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET_PATH) --help

$(TEST_BIN_DIR)/test_ipc_error: tests/test_ipc_error.c core/ipc_error.c core/ipc_error.h
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) $(CFLAGS) -o $@ tests/test_ipc_error.c core/ipc_error.c

$(TEST_BIN_DIR)/test_media_packet: tests/test_media_packet.c core/media_packet.c core/media_packet.h core/ipc_error.c core/ipc_error.h
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) $(CFLAGS) -o $@ tests/test_media_packet.c core/media_packet.c core/ipc_error.c $(FFMPEG_LIBS)

$(TEST_BIN_DIR)/test_thread_queue: tests/test_thread_queue.c core/thread_queue.c core/thread_queue.h core/media_packet.c core/media_packet.h core/ipc_error.c core/ipc_error.h
	@mkdir -p $(TEST_BIN_DIR)
	$(CC) $(CFLAGS) -o $@ tests/test_thread_queue.c core/thread_queue.c core/media_packet.c core/ipc_error.c $(FFMPEG_LIBS) $(LDFLAGS)

test-error: $(ERROR_TESTS)
	sh tests/run_error_tests.sh $(TEST_BIN_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

distclean: clean
	rm -rf $(OUTPUT_DIR)

.PHONY: all run test-error clean distclean
