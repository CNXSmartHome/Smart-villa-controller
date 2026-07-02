#!/usr/bin/env bash
# Build & run the SVC-100 host-side unit tests (no ESP-IDF required).
#
# Termination guarantees:
#   - Linux with coreutils `timeout`: wrapped in `timeout 60`.
#   - macOS / no `timeout`: a portable background watchdog kills the run after
#     60 s and returns nonzero.
#   - Sanitizers: AddressSanitizer + UBSan are ON by default on Linux but OFF by
#     default on Darwin, where LeakSanitizer's ptrace probe hangs in sandboxes.
#     Override with SVC_HOST_SANITIZERS=1/0. If a sanitized run times out, we
#     automatically retry once WITHOUT sanitizers so the suite still completes.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
bin="$here/host_tests"
os="$(uname -s 2>/dev/null || echo unknown)"
watchdog_secs=60

# Sanitizer default: on for Linux, off for Darwin (avoids the ptrace hang).
if [ -z "${SVC_HOST_SANITIZERS:-}" ]; then
    if [ "$os" = "Darwin" ]; then SVC_HOST_SANITIZERS=0; else SVC_HOST_SANITIZERS=1; fi
fi

# LeakSanitizer disabled regardless (we free everything; ptrace hangs sandboxes).
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

SOURCES=(
    "$here/test_main.c"
    "$root/components/svc_common/svc_json.c"
    "$root/components/storage/svc_config.c"
    "$root/components/webui/webui_authz.c"
    "$root/components/webui/webui_settings.c"
    "$root/components/logic/logic.c"
    "$root/components/control/control_logic.c"
    "$root/components/svc_hal/hal_board.c"
    "$root/components/hal_kincony/kincony_io_map.c"
    "$root/components/presence_fusion/presence_fusion.c"
)
INCLUDES=(
    -I "$here/shims"
    -I "$root/components/svc_common/include"
    -I "$root/components/dinput/include"
    -I "$root/components/board/include"
    -I "$root/components/storage/include"
    -I "$root/components/netmgr/include"
    -I "$root/components/webui/include"
    -I "$root/components/logic/include"
    -I "$root/components/control/include"
    -I "$root/components/svc_hal/include"
    -I "$root/components/hal_kincony/include"
    -I "$root/components/presence_fusion/include"
)

build() {
    # NOTE: do not expand an empty array under `set -u` — bash 3.2 (macOS
    # default) treats "${san[@]}" as an unbound variable. Use explicit if/else.
    echo "build: sanitizers=$SVC_HOST_SANITIZERS (os=$os)"
    if [ "$SVC_HOST_SANITIZERS" = "1" ]; then
        gcc -Wall -Wextra -std=c11 -g -fsanitize=address,undefined \
            "${INCLUDES[@]}" "${SOURCES[@]}" -o "$bin"
    else
        gcc -Wall -Wextra -std=c11 -g \
            "${INCLUDES[@]}" "${SOURCES[@]}" -o "$bin"
    fi
}

# Portable watchdog: run $bin, kill after $watchdog_secs, return 124 on timeout.
# Job-control (set -m) puts $bin in its own process group so on timeout we can
# kill the whole group — reaping any children a test binary might have spawned.
run_watchdog() {
    set -m
    "$bin" &
    local pid=$!
    set +m
    local i=0
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$i" -ge "$watchdog_secs" ]; then
            echo "host tests TIMED OUT after ${watchdog_secs}s; killing process group $pid"
            kill -9 -- "-$pid" 2>/dev/null || kill -9 "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
            return 124
        fi
        sleep 1
        i=$((i + 1))
    done
    wait "$pid"
}

run_tests() {
    if command -v timeout >/dev/null 2>&1; then
        timeout "$watchdog_secs" "$bin"
    else
        run_watchdog
    fi
}

build
rc=0
run_tests || rc=$?

# Fallback: a sanitized run that timed out (124) is retried once without
# sanitizers so the official command still produces a result.
if [ "$rc" = "124" ] && [ "$SVC_HOST_SANITIZERS" = "1" ]; then
    echo "sanitized run timed out; retrying without sanitizers (SVC_HOST_SANITIZERS=0)"
    SVC_HOST_SANITIZERS=0
    build
    rc=0
    run_tests || rc=$?
fi

if [ "$rc" -ne 0 ]; then
    echo "host tests FAILED (exit $rc)"
    exit "$rc"
fi
echo "host tests OK"
