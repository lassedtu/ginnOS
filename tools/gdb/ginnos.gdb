# gdb setup for debugging the ginnOS kernel under QEMU. loaded by `make gdb`
# and `make debug`, which also load build/kernel.elf and connect to QEMU.
#
# by hand:
#   qemu-system-i386 -drive if=ide,index=0,format=raw,file=build/ginnos.img -S -gdb tcp:127.0.0.1:1234
#   gdb -ex "file build/kernel.elf" -x tools/gdb/ginnos.gdb -ex "target remote 127.0.0.1:1234"

set architecture i386
set disassembly-flavor intel
set pagination off
set confirm off
set print pretty on

# stop on any kernel panic, before it prints and halts. `bt` then shows the
# caller; for exceptions, `frame 1` and `p *regs` show the trapped state.
break kernel_panic
break kernel_panic_at

# user programs are separate ELF files linked at their own addresses, so their
# symbols are not loaded. add one with e.g.:
#   add-symbol-file build/user/bin/sh

# show the instruction about to run on every stop. the first stop is the BIOS
# reset vector in real mode, which gdb decodes as 32-bit garbage; that is
# expected. `c` boots on to the kernel.
display/i $pc
