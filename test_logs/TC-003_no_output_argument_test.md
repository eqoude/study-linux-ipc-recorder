# TC-003 No Output Argument Test

## 1. Test Objective

验证程序在未启用 `preview`、`record` 或 `rtsp` 输出时，是否能给出明确提示并正常退出，避免无输出配置下静默运行。

## 2. Test Environment

- Host OS: Ubuntu Linux
- Target: Local x86_64 development machine
- Program: `bin/ipc_recorder`
- Camera: N/A
- Build Type: Debug build, `gcc -Wall -Wextra -g -O0`

## 3. Test Command

```bash
./bin/ipc_recorder
```

## 4. Expected Result

- 程序提示未启用输出。
- 程序正常退出。
- 不发生崩溃。

## 5. Actual Result

实际输出：

```text
no output enabled, use --preview, --record <path>, and/or --rtsp <url>
```

程序在没有启用 preview / record / rtsp 输出时，能够给出明确错误提示，并正常退出，没有崩溃。

## 6. Result

PASS

## 7. Notes

本测试属于参数校验类黑盒测试，不涉及摄像头和编码链路。
