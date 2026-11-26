# ginnOS

ginnOS (Ginn short for Ginnungagap, and OS short for Operating System) is a unix-like hobby operating system, written from scratch in c and assembly, which i'm developing alongside a course on operating systems i'm taking at DTU.

![Example of Metaballs simulation using the Bifrost framebuffer](https://github.com/lassedtu/ginnOS/blob/main/metaballs.gif)

I started this project because i wanted to understand how an operating system is really coded instead of just theory. I plan to keep building and improving it as i learn more.

The name ginnOS is taken from Ginnungagap, the primordial void in Norse mythology from which the world was created.

> "That was the age when nothing was; / There was no sand, nor sea, nor cool waves, / No earth nor sky nor grass there, / Only Ginnungagap."
> - Völuspá, Poetic Edda

### Documentation

The manual is being written in [`docs/`](docs/), next to the code. It has
three parts: using ginnOS, writing programs for it, and how it works inside.

## Features

ginnOS is a Unix-like system for x86 written from scratch in C and assembly.
It builds as a 32-bit (i686) or a 64-bit (x86_64) system and boots with
Limine on BIOS and UEFI machines. It starts a graphical login and a desktop
with its own apps. When there is no display, or when
`/etc/ginnos/boot.conf` says `session = text`, it starts a shell on the
console. Everything below is implemented and working today.

### Boot and kernel core

- Boots with [Limine](https://codeberg.org/Limine/Limine) on BIOS and UEFI
  machines: through Multiboot 2 on i686 (framebuffer, memory map, ACPI RSDP,
  command line and a boot module are read from the Multiboot 2 tags) and
  through the Limine boot protocol on x86_64. The boot menu has a folder
  of screen sizes
- The root filesystem is an ext2 partition with the `GINNOS_EXT2` label on
  an IDE, SATA or NVMe disk (MBR or GPT). If there is none, the root is the
  image that Limine loads into RAM (for a boot from a USB stick; changes are
  lost at reboot). The kernel only reads the other disks, so it does not
  write to the disk of another operating system
- The older custom two-stage BIOS bootloader (stage1 MBR + stage2) is still
  available with `make BOOTLOADER=custom` (i686 only)
- i686 protected-mode and x86_64 long-mode kernels with a GDT (ring 0/3
  segments + TSS)
- IDT, IRQ handling and a 256-vector ISR framework; the local APIC and
  I/O APIC (from the ACPI MADT, with the interrupt source overrides) or the
  8259 PICs when there is no MADT; the local APIC timer is the tick
- ACPI tables (RSDP, RSDT/XSDT, MADT, FADT, HPET, MCFG). `reboot` uses the
  FADT reset register. `poweroff` uses the `_S5` sleep type read from the
  DSDT (no AML interpreter)
- PCI enumeration with BAR sizes and MSI/MSI-X capabilities (`/proc/pci`)
- Device interrupts: MSI-X, MSI, or the shared INTx line for each PCI
  device, with counts in `/proc/interrupts`
- Shared-IRQ registration layer (`irq_request`/`irq_free`, per-handler enable)
- Kernel panic + assertion infrastructure, unified `kerr_t` error type
- Symbolised backtraces on panics and CPU exceptions (frame-pointer walk +
  a symbol table embedded at build time), on screen and over serial
- The boot log on the screen and in `/dev/kmsg`; leveled kernel logging
  (`klog`) and a COM1 serial driver for headless debugging (the kernel
  also starts on machines with no serial port)
- E820 memory-map detection and boot-time region reservation
- CMOS real-time clock for the wall-clock time; `clock_gettime` and
  `clock_settime` (`CLOCK_REALTIME`, `CLOCK_MONOTONIC`, process CPU time),
  `nanosleep` and `times` with per-process user and system time
- The FPU and SSE registers of each thread are saved on every switch and
  in signal frames
- `uname` and `sethostname`

### Memory management

- Physical memory manager (bitmap allocator)
- Kernel heap: first-fit allocator with splitting, coalescing, expansion,
  `kmalloc`/`kfree`/`krealloc` (in-place when possible)/`kcalloc`, allocation
  statistics, and an optional `HEAP_DEBUG` mode (redzone canaries, freed-memory
  poisoning)
- Virtual memory: a higher-half kernel with a direct map of RAM, kernel page
  tables shared by every process, a separate user half per process,
  `ioremap` for device memory (also above 4 GiB), a page-fault handler, and
  a map/unmap/translate API
- 8 KiB kernel stacks with an unmapped guard page below each; a double-fault
  task gate (its own TSS and stack) reports an overflow as a "kernel stack
  overflow" panic with a backtrace instead of a silent triple-fault reset
- User address space (i686): the program at `0x400000` with read-only code,
  an unmapped null page, a heap after the program, an mmap region at
  `0x40000000`, and a stack below `0xC0000000` that grows on demand up to
  16 MiB with a guard page under it
- Each process keeps a table of its memory areas (VMAs). Pages are allocated
  when they are first touched, and a bad access kills only the process
  ("segmentation fault", killed by SIGSEGV) instead of the kernel
- `mmap` (anonymous and device-backed, `MAP_FIXED`), `munmap` and `mprotect`
- Shared memory between processes, which the desktop uses for window
  buffers

### Processes, threads and multitasking

- Process table (slots on the heap) with per-process page directory, kernel
  stack, fd table, cwd and root
- `/bin/init` is PID 1: it reads `/etc/ginnos/boot.conf`, starts DHCP on
  each network card, starts the login screen (or the shell on the console
  if there is no display or the login screen fails), and reaps orphans
- Users and groups: real, effective and saved ids, `setuid`/`setgid`,
  set-uid and set-gid programs, a umask, and file permission checks on
  open, create, remove, rename, chdir and exec (with sticky `/tmp`);
  `/etc/passwd`, `/etc/group` and `/etc/shadow` (`$5$` SHA-256 crypt
  passwords)
- ELF32 and ELF64 loader (`PT_LOAD` mapping) with `argc`/`argv`/`envp`
  passing (up to 128 KiB of arguments)
- `fork` with copy-on-write: parent and child share their pages read-only
  (each frame has a reference count) until one of them writes
- `execve` replaces the calling program and keeps its pid, cwd and fds; a
  failed `execve` returns to the old program
- Ring 3 execution via `iret` (and `syscall`/`iretq` on x86_64), TSS
  kernel-stack switching
- Round-robin scheduler with an O(1) intrusive ready queue, an idle (hlt)
  task, and nice values (`getpriority`, `setpriority`, `nice`)
- Parent/child relationships, `waitpid(pid, &status, options)` with `-1`
  (any child), `WNOHANG`, `WUNTRACED`, `WCONTINUED` and POSIX status words,
  `getppid`, `sched_yield`; a process frees its fds, argv and address space
  when it exits, and orphans are reaped by the kernel
- POSIX signals: `kill` (also to process groups), `sigaction` (`SA_RESTART`,
  `SA_NODEFER`, `SA_RESETHAND`, `SA_NOCLDSTOP`), `sigprocmask`,
  `sigpending`, `sigsuspend`, `alarm`; user handlers run on a frame on the
  user stack and return through `sigreturn`; default actions terminate,
  ignore, stop or continue; a sleeping syscall is interrupted (`EINTR`) or
  restarted (`SA_RESTART`)
- Faults in user code are signals, not kernel panics: `SIGSEGV`, `SIGFPE`
  (divide by zero), `SIGILL`, `SIGBUS`, `SIGTRAP`; also `SIGPIPE`,
  `SIGCHLD` and `SIGALRM`
- Process groups and sessions (`setpgid`, `getpgid`, `setsid`, `getsid`)
- Threads: `clone` makes a thread that shares the memory, open files,
  cwd and signal handlers of its process. A process with threads acts as
  one process for `getpid`, `kill`, `waitpid`, stops and `execve`; `tgkill`
  sends to one thread, and `exit_group` ends them all. Each thread has
  its own thread-local storage (the FS base on x86_64, a GS segment on
  i686), and `futex` lets threads sleep on a word in memory
- `chroot`

### Concurrency

- Syscalls are preemptible, so shared kernel state is locked: spinlocks
  (`spin_lock_irqsave`) on the heap, PMM, process table, pipes, keyboard ring,
  console, device registry and mount table; a sleeping `mutex_t` serialises
  ext2 and whole console writes. The rules are written down at the top of
  `src/kernel/sync/spinlock.h`
- Generic wait queues (block/wake-one/wake-all), and
  `wait_queue_block_locked()` to sleep on a condition without losing a wakeup;
  pipes and keyboard reads sleep instead of busy-waiting

### Filesystem and I/O

- VFS layer with a mount table (longest-prefix resolution) and a filesystem
  operations vtable (`fs_ops_t`), the VFS never calls a filesystem directly
- Open files are refcounted open-file descriptions: `dup`, `dup2`, `fork`
  and spawn share one description, so they share the file offset and status
  flags; `FD_CLOEXEC` descriptors close on `execve`; up to 64 open files
  per process
- `open` flags: `O_RDONLY`/`O_WRONLY`/`O_RDWR` (checked on every read and
  write), `O_CREAT`, `O_EXCL`, `O_TRUNC`, `O_APPEND`, `O_DIRECTORY`,
  `O_CLOEXEC`, `O_NONBLOCK`; the `*at` calls relative to a directory fd
- ext2 driver: reads and writes through direct and single/double-indirect
  blocks (files up to 64 MiB with 1 KiB blocks; reads also triple-indirect);
  inode and block allocation, directory operations (create, remove, read,
  write, truncate, stat, rename); hard links and symbolic links; file times
  as timespecs (`utimensat`); named pipes and device nodes (`mknod`,
  `mkfifo`); freed inodes and truncated blocks go back to the volume
  (`e2fsck` finds no errors after the test programs run)
- `flock` file locks
- A write-through, LRU sector cache (512 KiB) under the root filesystem
- devfs at `/dev`, over the device registry: the disks, `tty0`, `console`,
  `fb0`, `display`, `kbd`, `input`, `dsp`, `mixer`, `kmsg`, `null`,
  `random`, `urandom`, `ptmx` and `pts/`
- tmpfs at `/tmp`: files and directories in kernel memory
- procfs at `/proc`: `meminfo` (memory, heap and cache use), `uptime`,
  `pci`, `interrupts`, `audio`, `net`, `<pid>/status` and `self`
- `poll` and `select` over pipes, terminals, sockets and files
- Block-device abstraction with drivers for IDE (ATA PIO), SATA (AHCI, DMA)
  and NVMe disks, a RAM disk, and MBR and GPT partition tables; a process
  that waits for a SATA or NVMe disk sleeps until its interrupt
- Pipes (16 KiB ring buffers on the kernel heap, no fixed limit) with
  blocking, EOF/EPIPE, reference-counted endpoints, and atomic writes of up
  to `PIPE_BUF` (4 KiB)
- Pseudo-terminals (`/dev/ptmx`, `/dev/pts/<n>`) for the terminal app

### Input

- PS/2 and USB keyboards (xHCI, HID boot protocol, events by interrupt)
- PS/2 and USB mice, and the QEMU tablet; events for programs in
  `/dev/input`
- Keyboard layouts that change while the system runs (`kbdlayout`): US and
  Danish (Mac), with dead keys and `æ`, `ø`, `å`, `€` on the desktop

### System calls

- 106 syscalls via `int 0x80` on i686 and the `syscall` instruction on
  x86_64, with `-errno` returns; the kernel and libc share one errno table.
  The numbers are in `src/kernel/syscall/syscall.h`
- User pointers are checked against the page tables (`copy_from_user`,
  `copy_to_user`, `strncpy_from_user`): a bad pointer returns `-EFAULT`
  instead of crashing the kernel
- Processes and threads: `exit`, `exit_group`, `fork`, `execve`, `clone`,
  `getpid`, `gettid`, `getppid`, `waitpid`, `sched_yield`, `sbrk`,
  `setpgid`, `getpgid`, `setsid`, `getsid`, `set_tls`, `futex`, `chroot`,
  `getpriority`, `setpriority`
- Users, time and the system: the uid and gid calls, `umask`,
  `clock_gettime`, `clock_settime`, `nanosleep`, `times`, `uname`,
  `sethostname`, `reboot`, `getrandom`
- Signals: `kill`, `tgkill`, `sigaction`, `sigprocmask`, `sigreturn`,
  `sigpending`, `sigsuspend`, `alarm`
- Files: `open`, `close`, `read`, `write`, `pread`, `pwrite`, `lseek`,
  `ftruncate`, `fsync`, `fcntl`, `flock`, `dup2`, `pipe`, `poll`, `stat`,
  `fstat`, `access`, `rename`, `unlink`, `mkdir`, `rmdir`, `getdents`,
  `getcwd`, `chdir`, `chmod`, `fchmod`, `chown`, `fchown`, and the `*at`
  calls (`openat`, `mkdirat`, `unlinkat`, `renameat`, `fstatat`,
  `faccessat`, `fchmodat`, `fchownat`, `symlinkat`, `readlinkat`, `linkat`,
  `utimensat`, `mknodat`)
- Sockets: `socket`, `socketpair`, `bind`, `listen`, `accept`, `connect`,
  `sendmsg`, `recvmsg`, `shutdown`, `getsockname`, `getpeername`,
  `setsockopt`, `getsockopt`
- Devices and memory: `ioctl` (device control, termios and job control on
  the terminals), `mmap`, `munmap`, `mprotect`, `shm_create`, `pollkey`

### Networking

- Our own TCP/IP stack in the kernel: Ethernet, ARP, IPv4, ICMP, UDP and
  TCP, over a virtio-net card (`/proc/net` shows the interfaces)
- BSD sockets: `AF_INET` (TCP stream, UDP datagram and ICMP ping sockets)
  and `AF_UNIX` (stream sockets that can pass file descriptors)
- `dhcp` gets an address at boot and keeps it; `ip` shows and sets the
  addresses and the default route
- DNS in libc: `getaddrinfo` asks the name servers in `/etc/resolv.conf`,
  after `/etc/hosts`; `host <name>` looks a name up
- `ping`, and `fetch [-i] [-I] [-k] [-o file] url` for HTTP/1.1 and HTTPS
- `curl` as a port, with HTTPS through mbedTLS

### Randomness and TLS

- A kernel random number generator: a BLAKE2s pool (fed by RDSEED or
  RDRAND, the cycle counter at each interrupt and timing jitter) keys a
  ChaCha20 generator with fast key erasure. Programs read it through
  `/dev/random`, `/dev/urandom` and `getrandom`
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls) as a port, with the
  Mozilla list of certificate authorities in the image, so `fetch` and
  `curl` check the server's certificate

### Display and graphics

- A display layer with drivers for virtio-gpu (the screen size changes
  while the system runs, and the hardware draws the pointer) and the
  framebuffer that the bootloader set up; `display` lists and sets the
  modes, and the desktop follows a change. Framebuffers above 4 GiB work
- Layered TTY: a device-independent VT/ANSI terminal state machine
  (`tty_backend_t`) decoupled from the output device
- The console is a POSIX-style terminal: termios (`ICANON` line editing with
  erase/kill/EOF, `ECHO`, `ISIG`, non-canonical input, and a ginnOS key-event
  mode for full-screen programs), a foreground process group, Ctrl+C,
  Ctrl+\ and Ctrl+Z sending `SIGINT`, `SIGQUIT` and `SIGTSTP`, `SIGTTIN` for
  background reads, and `TIOCGWINSZ`
- VGA text-mode backend (fallback console)
- Framebuffer console: JetBrains Mono bitmap-font glyph renderer (font baked
  offline from a TTF), SGR colours, and double buffering with dirty-row
  flushing, with automatic fallback to VGA text when no framebuffer is
  present
- Linux-style **`/dev/fb0`** framebuffer device: a char device with
  `read`/`write`/`lseek`/`ioctl`/`mmap`, exposing `fb_fix_screeninfo` /
  `fb_var_screeninfo` (per-channel bitfields). A `metaballs` demo draws to
  it via `open` + `ioctl` + `mmap`

### Sound

- Intel HDA (the sound hardware of almost every PC since 2004) and AC'97
  drivers. HDA finds the codec's line out, speaker and headphone outputs
  and plays on all of them.
- `/dev/dsp` with the OSS interface (`<sys/soundcard.h>`): 8-bit or
  16-bit, mono or stereo, 8 to 48 kHz as the device allows, blocking or
  `O_NONBLOCK` writes, `poll`. Up to 8 programs can play at once: the
  kernel adds their streams together. `/dev/mixer` sets the volume.
- `/proc/audio` shows the device, what is queued and played, and the
  underruns.
- `play file.wav` plays PCM WAV files (and converts the rate when the
  device lacks it), `beep [hz] [ms]` plays a tone, and `audiotest` plays
  test tones and checks the interface. `/usr/share/sounds` has two sample
  sounds.

### The desktop

- heimdalld, the compositor: windows with title bars and window
  buttons drawn from the theme, popups, panels, focus, the pointer, the
  clipboard and keyboard input. It follows the screen size and draws at
  scale 2 on large screens. Programs talk to it through `libheimdall` over
  a unix socket, with the window pixels in shared memory
- Bifrost, the login screen: a large clock, the users, and a password
  field. A wrong password says "Incorrect password"
- asgard, the desktop shell: the wallpaper, a taskbar with the open
  windows, a launcher (categories, search, recently used first, arrow
  keys), a clock with a calendar, the volume, the keyboard layout, Log
  out, Restart and Power off. `startde` starts the desktop from a shell
- The apps:
  - Mimir, the terminal (a pty and `lib/vt`)
  - Yggdrasil, the file manager: a side bar, a path bar, grid and list
    views, sort by name, size, type or date, a filter field, rename in
    place, new folders, the trash
  - Saga, the text editor
  - Freyja, the image viewer (PNG and JPEG)
  - Huginn, the system monitor: processes, CPU and memory
  - Settings: Appearance (light or dark, the accent colour, the scale), Display,
    Keyboard, Sound, Terminal and About, with a search field
  - Gallery (`idunn-gallery`): every idunn component, in light and dark
  - DOOM in a window (with `make DOOM=1`)
- Light and dark mode: a theme change reaches every open app and the
  title bars at once. The defaults are in `/etc/ginnos/theme.conf`; a
  user's own values are in `~/.config/ginnos/<name>.conf`
- The default users: `root` (password `ginnos`) and `user` (password
  `user`). `passwd` changes a password, `su` changes the user. Change the
  passwords on a machine that other people can use
- Boot to the console in place of the login screen: set `session = text`
  in `/etc/ginnos/boot.conf`

### Libraries for programs (`src/lib/`)

- `gfx`: 2D drawing with round shapes, borders, box shadows and
  `color-mix` as CSS draws them
- `bragi`: TrueType text (Inter and JetBrains Mono) with sRGB blending
- `idunn`: the widget toolkit (buttons, text fields, lists, grids, tabs,
  menus, dialogs, a file chooser and more) and the light and dark theme
- `heimdall`: the client side of the compositor protocol
- `conf`: settings files; `apps`: the `.app` files of the desktop's
  programs; `mime`: file types; `icon`: the Lucide and Tela icon packs;
  `image`: PNG and JPEG through stb_image; `vt`: a terminal emulator
  without a screen
- Host tests with golden images (`make host-test`)

### C standard library (libc.a)

- `<stdio.h>`: buffered `FILE` streams (`fopen`, `fgets`, `getline`,
  `fread`, `fwrite`, `fseek`, `setvbuf`, `fmemopen`, ...), a full `printf`
  family (flags, width, precision, `hh`/`h`/`l`/`ll`/`z`, `%f %e %g` with
  exact float conversion), `sscanf`
- `<string.h>`: the usual functions, also `strdup`, `strndup`, `strtok_r`,
  `strspn`, `strcasecmp`, `memchr`
- `<stdlib.h>`: `malloc` (size-class lists with boundary tags; large blocks
  from `mmap`), the `strtol` family, `qsort`, `bsearch`, `rand`,
  `setenv`/`getenv`, `realpath`, `atexit`, `abort`
- `<math.h>` (also `pow`, `exp` and `log` in 80-bit, and the `M_`
  constants), `<regex.h>` (from musl), `<fnmatch.h>`, `<search.h>`,
  `<wchar.h>` and `<wctype.h>` (UTF-8), `<libgen.h>`, `<locale.h>`,
  `<langinfo.h>`, `<syslog.h>`
- `<time.h>` (`time`, `clock_gettime`, `nanosleep`, `gmtime`, `mktime`,
  `strftime`, `strptime`), `<sys/time.h>`, `<sys/times.h>`, `<ctype.h>`,
  `<setjmp.h>`, `<assert.h>`, `<stdint.h>`, `<inttypes.h>`, `<stdbool.h>`,
  `<stddef.h>`, `<pwd.h>`, `<grp.h>`, `<shadow.h>`, `<crypt.h>`
- `<unistd.h>`: the syscall surface (`read`/`write`/`close`/`fork`/`execve`/
  `pipe`/`dup`/`dup2`/`lseek`/`pread`/`access`/`fsync`/`pathconf`/… and
  `read_event`)
- `<fcntl.h>` (`open`, `creat`, `fcntl`, the `*at` calls), `<sys/stat.h>`
  (`stat`, `fstat`, `S_IS*`), `<dirent.h>` (`opendir`, `fdopendir`,
  `readdir`, `closedir`), `<poll.h>`, `<sys/select.h>`, `<sys/wait.h>`
  (`waitpid`, `wait`, `W*` macros), `<sys/file.h>` (`flock`), `<limits.h>`,
  `<stdarg.h>`
- `<signal.h>` (`sigaction`, `signal`, `kill`, `raise`, masks), `<termios.h>`
  (`tcgetattr`, `tcsetattr`, `cfmakeraw`, `tcsetpgrp`), `<pty.h>`,
  `<sys/ioctl.h>`, `<sys/types.h>`
- `<sys/socket.h>`, `<netinet/in.h>`, `<arpa/inet.h>`, `<netdb.h>`
  (`getaddrinfo`), `<sys/un.h>`, `<net/if.h>`
- `<sys/mman.h>` (`mmap`, `munmap`, `mprotect`), `<sys/fb.h>`
  (framebuffer), `<sys/soundcard.h>` (OSS), `<sys/random.h>`
  (`getrandom`), `<sys/reboot.h>`
- `<errno.h>`: `errno` (one for each thread), `perror`, `strerror`
- `<pthread.h>`: threads (`pthread_create`, `join`, `detach`, `exit`),
  mutexes, condition variables, read-write locks, barriers, spin locks,
  `pthread_once` and thread-specific data; `<semaphore.h>`; stdio and
  `malloc` take locks once a program has threads
- `crt0` with `argc`/`argv`/`envp` setup and thread-local storage

### The ginnOS toolchain

- A GCC and binutils that know ginnOS as a target (`i686-ginnos`,
  `x86_64-ginnos`), built by `tools/toolchain/build.sh`. Programs build as
  on any Unix: `x86_64-ginnos-gcc hello.c -o hello`, or
  `./configure --host=x86_64-ginnos`. See
  [tools/toolchain/README.md](tools/toolchain/README.md)
- `make sysroot` puts the libc headers and libraries into
  `build/<arch>/sysroot`, and a CMake toolchain file is in
  `tools/toolchain/cmake`
- Ports (programs from other projects) are scripts in `ports/<name>/`;
  `make` builds them into the image

### Userspace shell (`/bin/sh`)

- Line editor: cursor movement, insert/delete, Home/End, Ctrl+A/E/K/U/W/L/D,
  and lines longer than the terminal is wide
- Command history (128 entries, Up/Down, dedup)
- Environment variables (`export`, `unset`, `env`, `printenv`, `which`) with
  `$VAR` / `${VAR}` / `$?` / `$$` expansion
- PATH-based command lookup
- Tokenizer with double/single quoting and backslash escaping
- Pipelines (`cmd1 | cmd2 | cmd3`) and I/O redirection (`>`, `>>`, `<`)
- Job control: each pipeline is a process group; `cmd &`, Ctrl+Z, `jobs`,
  `fg`, `bg`, `kill [-signal] pid|%job`, and a note when a background job
  ends
- Builtins: `cd`, `exit`, `history`, `export`, `unset`, `env`, `printenv`,
  `which`, `jobs`, `fg`, `bg`, `kill`
- Not there yet: `;`, `&&` and `||`, scripts, and control flow (`if`,
  `for`, `while`)

### Bundled userspace programs

- The Unix tools from [sbase](https://core.suckless.org/sbase/) (MIT),
  built as a port without changes: `ls`, `cat`, `cp`, `mv`, `rm`, `grep`,
  `sed`, `find`, `tar`, `sort`, `dd`, `bc`, `date` and about 90 more, all
  in one program (`/bin/sbase-box`) with a symbolic link for each tool
- [Lua](https://www.lua.org/) 5.4 (MIT), the interpreter as `/bin/lua`,
  built as a port without changes
- [SDL 3](https://libsdl.org/) (zlib licence), built as a port with a ginnOS
  video driver: each SDL window is a desktop window, drawn with SDL's
  software renderer, with keyboard, mouse, resizing and the clipboard;
  sound plays through SDL's OSS driver on `/dev/dsp`. Programs link the
  static `libSDL3.a` from the sysroot. `sdldemo` tries it out (draw with
  the mouse, Space plays a tone)
- [curl](https://curl.se/) (curl licence) and
  [mbedTLS](https://github.com/Mbed-TLS/mbedtls) (Apache 2.0), as ports
- The system: `init`, `sh`, `su`, `passwd`, `id`, `clear`, `uptime`,
  `reboot` and `poweroff` (root only), `conf` (read and change settings
  files), `apps` (list or start the desktop's programs), `display`,
  `kbdlayout`, `evtest` (print input events), `startde`
- The network: `dhcp`, `ip`, `host`, `ping`, `fetch`
- `gfetch`: the system at a glance, with Ginnungagap as the logo
- `ze` (text editor for the console), and the `metaballs`, `gfxdemo` and
  `heimdemo` demos
- `doom`: Doom through [doomgeneric](https://github.com/ozkl/doomgeneric)
  (a submodule, not changed), with the free
  [Freedoom](https://freedoom.github.io/) game data in `/usr/share/doom`.
  It runs in a desktop window or on the framebuffer. Keys: arrows to
  move, Ctrl to fire, Space to open doors, Shift to run, Alt to strafe,
  Escape for the menu. Sound effects and music play on `/dev/dsp`; the
  music is the OPL3 emulation and MUS player of
  [Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom) 2.3.0
  (also a submodule, not changed). Doom is not built by default; build it
  with `make DOOM=1` (add `FREEDOOM=0` to leave out the 28 MiB game data).
  To play with your own WAD files (for example the shareware `doom1.wad`),
  put them in `.hidden/` (git ignores it); `make DOOM=1` copies them into
  `/usr/share/doom`, and `doom` uses the Doom WAD before Freedoom
  (`doom -iwad <file>` picks one).
- `play`, `beep` and `audiotest`: play WAV files, a tone, and test tones
  (see Sound above)
- Test programs: `memtest` (user memory: heap, stack growth, `mmap`,
  `munmap`, `mprotect`, and faults that must kill only the process),
  `proctest` (`fork`, copy-on-write, pipes, open flags, shared offsets,
  `waitpid`, `execve`), `fstest` (the file syscalls, tmpfs, large ext2
  files), `sigtest` (signals, process groups and the terminal),
  `threadtest` and `pthreadtest` (threads in the kernel and in libc),
  `shmtest`, `ptytest`, `sockettest`, and `libctest` (the C library)

### Portability and tooling

- Two architectures from one source tree: i686 (32-bit, Multiboot 2 or the
  custom bootloader) and x86_64 (64-bit, the Limine boot protocol, 4-level
  paging, the `syscall` instruction, ELF64 programs). `make ARCH=x86_64`
  builds the 64-bit kernel, libc and all programs into `build/x86_64/`
- Architecture-neutral abstraction layers (`arch_*` for interrupts, MMU,
  context switch, boot protocol); `src/arch/x86` holds both x86 ports
- Pointer-width types throughout (`uintptr_t`/`size_t`/`phys_addr_t`, one
  `PAGE_SIZE`, `phys_to_virt`/`virt_to_phys`, a `syscall_arg()` accessor) and
  compiler-checked printf formats
- The build is in parts under `mk/` (see the top of the `Makefile`):
  `make test` (in-kernel tests), `make host-test` (library and program
  tests on the host), `make lint` (clang-tidy), `make check64` (x86_64
  portability check), `make format` (clang-format), `make gdb`/`make debug`
  (QEMU + gdb attached), `make run-uefi` (QEMU with UEFI firmware), and
  `make compile_commands` for LSP
- Consistent `snake_case` conventions documented in `STYLE.md`; ext2 image,
  bitmap-font, icon and wallpaper helper tools under `tools/`

## Set up the tools

The build needs two kinds of compiler:

- an `i686-elf` cross compiler (and `x86_64-elf` for the 64-bit build) for
  the kernel, libc and our own programs, from the steps below or from
  `shell.nix`;
- the ginnOS toolchain for the ports, which `tools/toolchain/build.sh`
  builds (see [Build and run](#build-and-run)).

### macOS

Clone the project:

```bash
git clone https://github.com/lassedtu/ginnOS
cd ginnOS
```

Install the host dependencies:

```bash
brew install gmp mpfr libmpc isl texinfo nasm qemu mtools e2fsprogs cmake
brew install x86_64-elf-gcc   # only for the 64-bit build (make ARCH=x86_64)
brew install gdb   # optional, for make gdb / make debug
```

Create the build directories for the i686 cross compiler:

```bash
mkdir -p "$HOME/src/cross"
mkdir -p "$HOME/opt/cross"
cd "$HOME/src/cross"
```

Build and install binutils:

```bash
curl -LO https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz
tar -xf binutils-2.42.tar.xz
mkdir -p build-binutils
cd build-binutils

../binutils-2.42/configure \
  --target=i686-elf \
  --prefix="$HOME/opt/cross" \
  --with-sysroot \
  --disable-nls \
  --disable-werror

make -j"$(sysctl -n hw.ncpu)"
make install
cd ..
```

Build and install GCC (C only):

```bash
curl -LO https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz
tar -xf gcc-14.2.0.tar.xz
mkdir -p build-gcc
cd build-gcc

../gcc-14.2.0/configure \
  --target=i686-elf \
  --prefix="$HOME/opt/cross" \
  --disable-nls \
  --enable-languages=c \
  --without-headers

make -j"$(sysctl -n hw.ncpu)" all-gcc all-target-libgcc
make install-gcc install-target-libgcc
cd ..
```

Add the cross compiler to `PATH`:

```bash
echo 'export PATH="$HOME/opt/cross/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

Check the tools:

```bash
i686-elf-gcc --version
i686-elf-ld --version
i686-elf-objcopy --version
nasm -v
qemu-system-i386 --version
```

### NixOS and Nix

`shell.nix` at the project root has the `i686-elf` and `x86_64-elf` cross
compilers, binutils, NASM, QEMU, Python 3, gdb, mtools, e2fsprogs, git and
OVMF. Enter the shell:

```bash
nix-shell
```

The shell does not have the ginnOS toolchain or what it needs to build
(gmp, mpfr, libmpc, isl), and it does not have `cmake` for the SDL and curl
ports. Without them, build an image with no ports: `make PORTS=`.

### Ubuntu and Debian

Clone the project:

```bash
git clone https://github.com/lassedtu/ginnOS
cd ginnOS
```

Install the host dependencies:

```bash
sudo apt update
sudo apt install build-essential curl nasm qemu-system-x86 \
  libgmp-dev libmpfr-dev libmpc-dev libisl-dev texinfo bison flex \
  python3 e2fsprogs mtools git ovmf cmake
sudo apt install gdb   # optional, for make gdb / make debug
```

Create the build directories for the i686 cross compiler:

```bash
mkdir -p "$HOME/src/cross"
mkdir -p "$HOME/opt/cross"
cd "$HOME/src/cross"
```

Build and install binutils:

```bash
curl -LO https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.xz
tar -xf binutils-2.42.tar.xz
mkdir -p build-binutils
cd build-binutils

../binutils-2.42/configure \
  --target=i686-elf \
  --prefix="$HOME/opt/cross" \
  --with-sysroot \
  --disable-nls \
  --disable-werror

make -j"$(nproc)"
make install
cd ..
```

Build and install GCC (C only):

```bash
curl -LO https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz
tar -xf gcc-14.2.0.tar.xz
mkdir -p build-gcc
cd build-gcc

../gcc-14.2.0/configure \
  --target=i686-elf \
  --prefix="$HOME/opt/cross" \
  --disable-nls \
  --enable-languages=c \
  --without-headers

make -j"$(nproc)" all-gcc all-target-libgcc
make install-gcc install-target-libgcc
cd ..
```

Add the cross compiler to `PATH`:

```bash
echo 'export PATH="$HOME/opt/cross/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

If you use zsh instead of bash, append the same line to `~/.zshrc`.

Check the tools:

```bash
i686-elf-gcc --version
i686-elf-ld --version
i686-elf-objcopy --version
nasm -v
qemu-system-i386 --version
```

For the 64-bit build, build an `x86_64-elf` cross compiler the same way
with `--target=x86_64-elf`.

### Windows (WSL2)

Use WSL2 with Ubuntu. It gives the same Linux tools as a native install.

Open PowerShell as Administrator and run:

```powershell
wsl --install -d Ubuntu
```

Restart if Windows asks you to. Ubuntu then finishes its setup and asks
you to make a Linux user account.

If WSL is already installed and you only need the distro:

```powershell
wsl --list --online
wsl --install -d Ubuntu
```

Open Ubuntu (Start menu, or type `wsl` in PowerShell) and clone the
project into the Linux filesystem, for example `~/projects/ginnOS`. Do not
build from `/mnt/c/...`: file I/O across the Windows/Linux boundary is
slow, and `make` can see wrong file times there.

```bash
mkdir -p ~/projects
cd ~/projects
git clone https://github.com/lassedtu/ginnOS
cd ginnOS
```

From here, follow [Ubuntu and Debian](#ubuntu-and-debian) in the WSL
terminal, then [Build and run](#build-and-run). On most Windows 11 + WSL2
set-ups, QEMU opens in its own window with no extra configuration.

## Build and run

The image includes ports (programs from other projects): sbase, Lua,
SDL 3, mbedTLS, the CA certificates and curl. They are built with the
ginnOS toolchain, a GCC and binutils that know ginnOS. Build it once for
each architecture. It downloads GCC and binutils, needs gmp, mpfr, libmpc
and isl, takes about 20 minutes, and goes into `tools/toolchain/prefix`.
The ports also need `git`, `curl` and `yacc` (macOS has all three), and
SDL and curl need `cmake`.

```bash
tools/toolchain/build.sh i686
tools/toolchain/build.sh x86_64
```

`make PORTS=` builds an image without the ports. That image has no `ls`,
`cat` and the other Unix tools (sbase has them), no `curl`, and `fetch`
has no HTTPS.

Then, from the project root:

```bash
make
make run
```

The first build fetches a pinned Limine release with git into
`build/limine`, and the fonts and icon packs into `build/fonts` and
`build/icons` (`make clean` keeps them, `make distclean` removes them).
The disk image `build/ginnos.img` has a FAT boot partition (Limine,
`limine.conf`, the kernel and a copy of the root image) and the ext2 root
filesystem.

`make run` gives QEMU a virtio-vga card, an Intel HDA sound card and a
virtio-net card on QEMU's user network (the guest is 10.0.2.15, the
gateway 10.0.2.2). These options change it:

| Option | Default | What it does |
| --- | --- | --- |
| `RESOLUTION=1280x800` | `1440x900` | the size of the QEMU window and of the wallpaper; the virtio-gpu driver takes it at start, and the desktop follows when you resize the window |
| `QEMU_VGA=std` | `virtio` | the plain VGA card, fixed at `RESOLUTION` |
| `BOOT_RESOLUTION=1920x1080` | empty (Limine picks) | the screen size the first boot entry asks for |
| `AUDIO=ac97` or `AUDIO=0` | `hda` | an AC'97 card, or no sound card |
| `QEMU_AUDIODEV=<backend>` | `coreaudio` (macOS), `pa` (Linux) | the QEMU audio backend |
| `NET=0` | `1` | no network card |
| `GPT=1` | empty (MBR) | a GPT disk (it boots with BIOS and UEFI too) |
| `BOOTLOADER=custom` | `limine` | the older custom BIOS bootloader (i686 only) |
| `DOOM=1` | `0` | build Doom; the first build fetches Freedoom 0.13.0 (checked by SHA-256) into `build/freedoom` |
| `FONTS=0`, `ICONS=0`, `WALLPAPER=` | on | leave out the fonts, the icon packs or the wallpaper |

Boot the same image with UEFI firmware (QEMU's OVMF build; set
`OVMF_CODE=` if the build does not find it):

```bash
make run-uefi
```

Run the in-kernel test suite (it boots a separate test kernel headless and
exits with the result), and the library and program tests on the host:

```bash
make test
make host-test
```

Build and run the 64-bit system (needs `x86_64-elf-gcc`; the output goes to
`build/x86_64/`, so both builds can live side by side). Every make target
takes `ARCH`:

```bash
make ARCH=x86_64
make run ARCH=x86_64
make run-uefi ARCH=x86_64
make test ARCH=x86_64
```

If an i686 build fails with `unknown pseudo-op: .base64`, the cross gcc is
newer than its binutils (gcc 16 needs binutils 2.43 or later). The build
then looks for newer binutils from Homebrew (`brew install
i686-elf-binutils`) and uses them; or update binutils in your toolchain.

Check the kernel for 64-bit portability (needs a host `clang`; nothing is
built or run). It compiles every kernel, driver, filesystem and test source
for x86_64 with clang's stricter warnings, and fails on any pointer/integer
cast, 64-to-32-bit narrowing or printf format mismatch:

```bash
make check64
```

Debug the kernel with gdb. QEMU starts halted with a gdb stub, gdb attaches
with `build/kernel.elf` and `tools/gdb/ginnos.gdb` (breakpoints on
`kernel_panic`/`kernel_panic_at`), and QEMU stops when gdb exits. Type `c` to
boot. `make debug` does the same with the kernel built `-Og -DDEBUG`, which is
easier to step through:

```bash
make gdb                 # optimised kernel, with debug info
make debug               # -Og kernel
make gdb GDB_PORT=1240   # if port 1234 is taken
```

gdb must support i386 (Homebrew's and most distro `gdb` builds do; set
`GDB=...` to use another one, e.g. `i686-elf-gdb`). Kernel panics and CPU
exceptions also print a symbolised backtrace on screen and over serial, so
many crashes can be read without a debugger.

## Boot a real PC

Write the image to a USB stick. This erases the stick, so check the device
name first:

```bash
sudo dd if=build/ginnos.img of=/dev/<usb-stick> bs=4M conv=fsync
```

The kernel cannot read USB storage, so on a real machine booted from a USB
stick the root filesystem is the copy in RAM, and changes are lost at
reboot. The kernel looks for its root on the IDE (ATA) disk, on SATA disks
(AHCI) and on the NVMe disk, with MBR or GPT, but it uses a partition only
if its ext2 volume label is `GINNOS_EXT2`. It only reads the partition
tables and superblocks of other disks, so it does not write to the disk of
another operating system.

## The root filesystem

`make` builds the ext2 root from [tools/ext2/rootfs](tools/ext2/rootfs),
our programs and the ports, with
[tools/ext2/make_image.py](tools/ext2/make_image.py). Every file belongs to root
with mode 0755 or 0644, except the ones listed in
[tools/ext2/permissions](tools/ext2/permissions). The image
size follows the contents; to use another folder or set the size in MiB:

```bash
make EXT2_SOURCE_DIR=/absolute/path/to/your/folder
make EXT2_SIZE_MB=64
```

The helper needs `e2fsprogs` (`brew install e2fsprogs` on macOS).

## The console font

The framebuffer console renders text with a bitmap font baked from a
TrueType font. The kernel only copies bitmaps; no TrueType rasterizer
runs at run time. [tools/font/make_font.py](tools/font/make_font.py)
rasterizes a TTF offline and writes the C header the renderer uses
([src/drivers/video/fb/font.h](src/drivers/video/fb/font.h)). The header
is in git, so the normal build has no font dependency.

The default is JetBrains Mono Regular at a 10x20 cell. Make the header
again (only when you change the font or the size):

```bash
python3 tools/font/make_font.py src/drivers/video/fb/font.h
```

Use a different font, size, or cell:

```bash
python3 tools/font/make_font.py \
  --font /path/to/Font-Regular.ttf --size 16 --cell 10x20 \
  src/drivers/video/fb/font.h
```

The tool needs Pillow:

```bash
pip install Pillow
```

JetBrains Mono is licensed under the SIL Open Font License 1.1; the notice is
copied into the generated header.

## Credits and learning resources

- Nanobyte: https://www.youtube.com/@nanobyte-dev
  Operating system tutorials that have been a big help.

- OSDev Wiki: https://wiki.osdev.org/
  The main reference used for low level concepts and implementation.
