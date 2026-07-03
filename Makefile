TARGET := ipc_recorder

CC := gcc

BUILD_DIR := build
BIN_DIR := bin
OUTPUT_DIR := output

FFMPEG_CFLAGS := $(shell pkg-config --cflags libavcodec libavformat libavutil)
FFMPEG_LIBS := $(shell pkg-config --libs libavcodec libavformat libavutil)
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

CFLAGS := -Wall -Wextra -g -O0 \
          -Icore \
          -Icapture \
          -Iconverter \
          -Iencoder \
          -Iframe_processor \
          -Imuxer \
          -Imodules \
          -Iviewer \
          $(FFMPEG_CFLAGS) \
          $(SDL_CFLAGS)

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
    $(wildcard viewer/*.c)

OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

TARGET_PATH := $(BIN_DIR)/$(TARGET)

all: $(TARGET_PATH)

$(TARGET_PATH): $(OBJS)
	@mkdir -p $(BIN_DIR) $(OUTPUT_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(FFMPEG_LIBS) $(SDL_LIBS) $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET_PATH) --help

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

distclean: clean
	rm -rf $(OUTPUT_DIR)

.PHONY: all run clean distclean
