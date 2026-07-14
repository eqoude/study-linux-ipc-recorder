# TC-001 Build Clean Test

## 1. Test Objective

验证项目清理命令是否可以正确删除编译中间目录和可执行文件目录，确保后续可以从干净状态重新构建。

## 2. Test Environment

- Host OS: Ubuntu Linux
- Target: Local x86_64 development machine
- Program: `bin/ipc_recorder`
- Camera: N/A
- Build Type: Debug build, `gcc -Wall -Wextra -g -O0`

## 3. Test Command

```bash
make clean
```

## 4. Expected Result

- `build/` 目录被清理。
- `bin/` 目录被清理。
- 命令执行完成，不出现 error。

## 5. Actual Result

实际输出：

```text
rm -rf build bin
```

清理 `build` 和 `bin` 目录成功。

## 6. Result

PASS

## 7. Notes

本测试只验证清理动作，不验证编译结果。
