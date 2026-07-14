# TC-002 Build Test

## 1. Test Objective

验证项目能否从源码成功编译并链接生成 `bin/ipc_recorder`，确认主要模块和第三方库链接路径可用。

## 2. Test Environment

- Host OS: Ubuntu Linux
- Target: Local x86_64 development machine
- Program: `bin/ipc_recorder`
- Camera: N/A
- Build Type: Debug build, `gcc -Wall -Wextra -g -O0`

## 3. Test Command

```bash
make
```

## 4. Expected Result

- 项目成功编译。
- 生成 `bin/ipc_recorder`。
- 编译过程中没有 error。
- FFmpeg、SDL2、pthread 链接成功。

## 5. Actual Result

项目成功编译，生成：

```text
bin/ipc_recorder
```

编译过程中没有 error。

链接库包括：

- FFmpeg
- SDL2
- pthread

参与构建的主要模块包括：

- `app`
- `core`
- `capture`
- `converter`
- `encoder`
- `frame_processor`
- `muxer`
- `modules`
- `sink`
- `viewer`

## 6. Result

PASS

## 7. Notes

本测试只验证编译和链接，不验证运行时摄像头、MP4、RTSP、AI 旁路功能。
