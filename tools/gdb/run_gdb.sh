#!/usr/bin/env bash
# boot QEMU halted with a gdb stub and attach gdb to it (make gdb / make debug).
#
# usage: run_gdb.sh <qemu> <disk-image> <kernel-elf> <gdb> <port> \
#                   [qemu flags...] [-- gdb args...]
#
# QEMU runs in its own session, so pressing Ctrl-C in gdb (to interrupt the
# kernel) does not reach QEMU too: QEMU installs its own SIGINT handler and
# would otherwise quit. QEMU is stopped when gdb exits.

set -u

if [ "$#" -lt 5 ]; then
    echo "usage: $0 <qemu> <disk-image> <kernel-elf> <gdb> <port> [qemu flags...] [-- gdb args...]" >&2
    exit 2
fi

qemu="$1"
image="$2"
elf="$3"
gdb="$4"
port="$5"
shift 5

qemu_flags=()
while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do
    qemu_flags+=("$1")
    shift
done
[ "$#" -gt 0 ] && shift # drop the --
gdb_args=("$@")

script_dir="$(cd "$(dirname "$0")" && pwd)"

# python's os.setsid stands in for setsid(1), which macOS does not ship.
"${PYTHON:-python3}" -c 'import os, sys; os.setsid(); os.execvp(sys.argv[1], sys.argv[1:])' \
    "$qemu" ${qemu_flags[@]+"${qemu_flags[@]}"} \
    -drive "if=ide,index=0,format=raw,file=$image" \
    -S -gdb "tcp:127.0.0.1:$port" &
qemu_pid=$!
trap 'kill "$qemu_pid" 2>/dev/null' EXIT

# wait for the stub to listen before attaching.
tries=50
until nc -z 127.0.0.1 "$port" 2>/dev/null; do
    tries=$((tries - 1))
    if [ "$tries" -le 0 ] || ! kill -0 "$qemu_pid" 2>/dev/null; then
        echo "run_gdb: qemu did not open the gdb stub on port $port" >&2
        exit 1
    fi
    sleep 0.1
done

"$gdb" -q -ex "file $elf" -x "$script_dir/ginnos.gdb" \
    -ex "target remote 127.0.0.1:$port" ${gdb_args[@]+"${gdb_args[@]}"}
