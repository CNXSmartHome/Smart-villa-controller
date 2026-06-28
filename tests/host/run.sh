#!/usr/bin/env bash
# Build & run the SVC-100 host-side unit tests (no ESP-IDF required).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
bin="$here/host_tests"

# AddressSanitizer + UBSan make the bounded-buffer tests catch real OOB reads.
# LeakSanitizer is DISABLED: it relies on ptrace, which hangs/aborts inside many
# sandboxes and CI containers. We free everything we allocate, so leak detection
# adds no coverage here. abort_on_error + halt_on_error make any finding fatal.
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

gcc -Wall -Wextra -std=c11 -g -fsanitize=address,undefined \
    -I "$here/shims" \
    -I "$root/components/svc_common/include" \
    -I "$root/components/dinput/include" \
    -I "$root/components/board/include" \
    -I "$root/components/storage/include" \
    -I "$root/components/netmgr/include" \
    -I "$root/components/webui/include" \
    "$here/test_main.c" \
    "$root/components/svc_common/svc_json.c" \
    "$root/components/storage/svc_config.c" \
    "$root/components/webui/webui_authz.c" \
    -o "$bin"

# Hard timeout so a hang can never wedge CI; treat timeout/non-zero as failure.
# Capture the exit code without letting `set -e` abort first.
rc=0
if command -v timeout >/dev/null 2>&1; then
    timeout 60 "$bin" || rc=$?
else
    "$bin" || rc=$?
fi
if [ "$rc" -ne 0 ]; then
    echo "host tests FAILED (exit $rc)"
    exit "$rc"
fi
echo "host tests OK"
