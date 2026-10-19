#!/bin/sh
# fake_slow_prtget.sh — Fake prt-get implementation for OctoCRUX integration testing.
# Demonstrates:
#  1. Line-by-line live streaming output.
#  2. Running beyond the legacy 30-second timeout threshold (up to 36 seconds).
#  3. Graceful handling and exit on SIGTERM cancellation.
#  4. Logging invocation arguments to verify batched single-command execution.

# If OCTOCRUX_PROBE_LOG is set, record the invocation arguments
if [ -n "$OCTOCRUX_PROBE_LOG" ]; then
    echo "$@" >> "$OCTOCRUX_PROBE_LOG"
fi

# Trap SIGTERM (sent by CruxProcessRunner::cancel via QProcess::terminate)
cleanup() {
    echo "[fake_slow_prtget] Received SIGTERM cancellation signal! Cleaning up child processes..."
    if [ -n "$CHILD_PID" ] && kill -0 "$CHILD_PID" 2>/dev/null; then
        kill -TERM "$CHILD_PID" 2>/dev/null
    fi
    exit 143
}
trap cleanup TERM INT

DURATION=${FAKE_PRTGET_DURATION:-36}
TARGET=${2:-testpkg}

echo "=======> [fake_slow_prtget] Starting simulated build for '$TARGET' (target duration: ${DURATION}s)"

# Spawn a simulated child worker process in the background
sleep "$DURATION" &
CHILD_PID=$!

i=1
while [ "$i" -le "$DURATION" ]; do
    if ! kill -0 "$CHILD_PID" 2>/dev/null; then
        break
    fi
    echo "=======> Building '$TARGET' (second $i of $DURATION)"
    echo "Compiling module_${i}.cpp with gcc-14..."
    sleep 1
    i=$((i + 1))
done

wait "$CHILD_PID" 2>/dev/null
echo "=======> [fake_slow_prtget] Build completed successfully for '$TARGET'"
exit 0
