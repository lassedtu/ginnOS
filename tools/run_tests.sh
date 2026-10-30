#!/usr/bin/env bash
# boot the test kernel image headless and turn the result into an exit code.
#
# usage: run_tests.sh <qemu> <disk-image> <timeout-seconds> [qemu flags...]
#
# the test kernel prints results over serial (shown here via -serial stdio)
# and ends by writing to QEMU's isa-debug-exit device. QEMU then exits with
# status (value << 1) | 1: 1 means every test passed, 3 means a test failed
# or the kernel panicked. anything else (killed by the watchdog, QEMU error)
# is treated as a failure too.

set -u

if [ "$#" -lt 3 ]; then
    echo "usage: $0 <qemu> <disk-image> <timeout-seconds> [qemu flags...]" >&2
    exit 2
fi

qemu="$1"
image="$2"
timeout="$3"
shift 3

"$qemu" "$@" \
    -drive "if=ide,index=0,format=raw,file=$image" \
    -display none -no-reboot \
    -serial stdio \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 &
qemu_pid=$!

# watchdog: poll instead of a background sleeper, so nothing is left running.
timed_out=0
ticks=$((timeout * 10))
while kill -0 "$qemu_pid" 2>/dev/null; do
    if [ "$ticks" -le 0 ]; then
        timed_out=1
        kill "$qemu_pid" 2>/dev/null
        break
    fi
    sleep 0.1
    ticks=$((ticks - 1))
done

wait "$qemu_pid"
status=$?

if [ "$timed_out" -eq 1 ]; then
    echo "make test: FAILED (timed out after ${timeout}s, the kernel hung)" >&2
    exit 1
fi

case "$status" in
    1)
        echo "make test: all tests passed"
        exit 0
        ;;
    3)
        echo "make test: FAILED (see the FAIL / KERNEL PANIC lines above)" >&2
        exit 1
        ;;
    *)
        echo "make test: FAILED (unexpected QEMU exit status $status)" >&2
        exit 1
        ;;
esac
