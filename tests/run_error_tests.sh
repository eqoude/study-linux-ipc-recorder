#!/bin/sh
set -eu

TEST_BIN_DIR="${1:-build/tests}"

"${TEST_BIN_DIR}/test_ipc_error"
"${TEST_BIN_DIR}/test_media_packet"
"${TEST_BIN_DIR}/test_thread_queue"

if ! grep -q 'IpcError_ToString(ret)' app/app_pipeline.c; then
    printf '%s\n' "[FAIL] app/app_pipeline.c does not use IpcError_ToString(ret)"
    exit 1
fi

if ! grep -q '\[pipeline\].*%s (%d)' app/app_pipeline.c; then
    printf '%s\n' "[FAIL] app/app_pipeline.c does not print error string and code"
    exit 1
fi

printf '%s\n' "[PASS] app_pipeline error logs include readable error string and code"

printf '%s\n' "All error handling tests passed."
