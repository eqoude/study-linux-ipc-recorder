# TC-004 Help Message Test

## 1. Test Objective

验证程序帮助信息是否完整，确认 CLI 能展示主要运行参数和示例用法。

## 2. Test Environment

- Host OS: Ubuntu Linux
- Target: Local x86_64 development machine
- Program: `bin/ipc_recorder`
- Camera: N/A
- Build Type: Debug build, `gcc -Wall -Wextra -g -O0`

## 3. Test Command

```bash
./bin/ipc_recorder --help
```

## 4. Expected Result

- 正确输出 `Usage`。
- 正确输出 `Options`。
- 正确输出 `Examples`。
- 帮助信息包含主要 CLI 参数。

## 5. Actual Result

程序正确输出 `Usage`、`Options` 和 `Examples`。

帮助信息包含以下参数：

- `--device`
- `--width`
- `--height`
- `--fps`
- `--preview`
- `--record`
- `--rtsp`
- `--processor`
- `--no-processor`
- `--frames`
- `--help`

## 6. Result

PASS

## 7. Notes

CLI 参数帮助信息完整，能够说明主要运行方式。本测试不验证各参数对应功能是否全部可用。
