<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# libc

The headers of the C library, with the doc comment of each
declaration. For what libc has and does not have, see
[libc](../../develop/libc.md).

| Header | Documented | What it is |
| --- | --- | --- |
| [`<arpa/inet.h>`](arpa-inet.md) | 5 | IPv4 addresses as text ("10.0.2.15") and back |
| [`<assert.h>`](assert.md) | 0 | assert(): with NDEBUG defined it checks nothing |
| [`<crypt.h>`](crypt.md) | 4 | password hashes: crypt() with the SHA-crypt "$5$" scheme (SHA-256), the one glibc and "openssl passwd -5" make |
| [`<ctype.h>`](ctype.md) | 0 | character classes (the C locale, ASCII) |
| [`<dirent.h>`](dirent.md) | 6 | reading directories: opendir, readdir, closedir |
| [`<dlfcn.h>`](dlfcn.md) | 4 | loading shared libraries at run time |
| [`<errno.h>`](errno.md) | 4 |  |
| [`<fb.h>`](fb.md) | 4 | userspace access to the linear framebuffer |
| [`<fcntl.h>`](fcntl.md) | 3 | file open flags and open() |
| [`<fnmatch.h>`](fnmatch.md) | 1 | shell patterns ("\*.c", "\[a-z]?"): fnmatch is musl's (see src/libc/src/regex/README.md) |
| [`<grp.h>`](grp.md) | 1 | the group database (/etc/group: name:password:gid:members) |
| [`<inttypes.h>`](inttypes.md) | 0 | printf formats for the stdint.h types |
| [`<langinfo.h>`](langinfo.md) | 1 | facts about the locale |
| [`<libgen.h>`](libgen.md) | 2 | the parts of a path |
| [`<limits.h>`](limits.md) | 0 | sizes of the integer types and system limits |
| [`<locale.h>`](locale.md) | 3 | locales |
| [`<math.h>`](math.md) | 4 | floating-point functions (double, and float with an f) |
| [`<net/if.h>`](net-if.md) | 0 | network interfaces: struct ifreq for the SIOC ioctls of &lt;sys/ioctl.h&gt;, which work on any AF\_INET socket |
| [`<netdb.h>`](netdb.md) | 5 | names of hosts and services: getaddrinfo and the older calls |
| [`<netinet/in.h>`](netinet-in.md) | 0 | IPv4 addresses and ports, and the IP protocols |
| [`<netinet/tcp.h>`](netinet-tcp.md) | 0 | TCP socket options (setsockopt at IPPROTO\_TCP) |
| [`<poll.h>`](poll.md) | 1 | waiting for file descriptors to become ready |
| [`<pthread.h>`](pthread.md) | 0 | POSIX threads: start, wait for and end threads; mutexes, condition variables, read-write locks, barriers, spin locks, one-time initialization and thread-specific data |
| [`<pty.h>`](pty.md) | 2 | open a pseudo-terminal pair, and make one a process's terminal |
| [`<pwd.h>`](pwd.md) | 1 | the user database (/etc/passwd: name:password:uid:gid:gecos:home:shell) |
| [`<regex.h>`](regex.md) | 5 | POSIX regular expressions, basic (BRE) and extended (ERE) |
| [`<sched.h>`](sched.md) | 2 | the scheduler: give up the CPU |
| [`<search.h>`](search.md) | 7 | search trees (tsearch: a balanced tree of pointers the caller owns) and linear search in an array |
| [`<semaphore.h>`](semaphore.md) | 0 | unnamed POSIX semaphores, for the threads of one process |
| [`<setjmp.h>`](setjmp.md) | 2 | non-local jumps |
| [`<shadow.h>`](shadow.md) | 1 | the password hashes (/etc/shadow: name:hash:last:min:max:warn: inactive:expire:), which only root can read |
| [`<signal.h>`](signal.md) | 7 | signals: kill, sigaction, masks |
| [`<stdarg.h>`](stdarg.md) | 0 | variable argument lists (the compiler's builtins) |
| [`<stdbool.h>`](stdbool.md) | 0 | bool, true and false |
| [`<stddef.h>`](stddef.md) | 0 | NULL, size\_t, ptrdiff\_t, offsetof |
| [`<stdint.h>`](stdint.md) | 0 | integer types of fixed width |
| [`<stdio.h>`](stdio.md) | 12 | buffered streams and formatted input/output |
| [`<stdlib.h>`](stdlib.md) | 25 | This file contains the declarations of general utility functions |
| [`<string.h>`](string.md) | 14 | This file contains the declarations of string manipulation functions |
| [`<strings.h>`](strings.md) | 0 | strcasecmp and strncasecmp (declared in string.h) |
| [`<sys/fb.h>`](sys-fb.md) | 6 | framebuffer device interface (userspace) |
| [`<sys/file.h>`](sys-file.md) | 1 | flock: advisory locks on whole files |
| [`<sys/input.h>`](sys-input.md) | 0 | the events of /dev/input (ABI) |
| [`<sys/ioctl.h>`](sys-ioctl.md) | 0 | device control requests |
| [`<sys/kbd.h>`](sys-kbd.md) | 2 | the ioctls of /dev/kbd: list, read and set the keyboard layout |
| [`<sys/mman.h>`](sys-mman.md) | 4 | memory-mapping interface (userspace) |
| [`<sys/random.h>`](sys-random.md) | 1 | random bytes from the kernel's generator (getrandom) |
| [`<sys/reboot.h>`](sys-reboot.md) | 1 | restart or turn off the machine (root only), as on Linux |
| [`<sys/resource.h>`](sys-resource.md) | 2 | process priorities: the nice value, -20 (the most CPU) to 19 (the least) |
| [`<sys/select.h>`](sys-select.md) | 1 | select(), built on poll() |
| [`<sys/socket.h>`](sys-socket.md) | 1 | sockets: Unix domain stream sockets, with file descriptor passing, and IPv4 sockets (TCP; UDP, and ICMP for ping) |
| [`<sys/soundcard.h>`](sys-soundcard.md) | 1 | the OSS sound interface: ioctl numbers and formats (ABI) |
| [`<sys/stat.h>`](sys-stat.md) | 9 | file metadata: stat() and fstat() |
| [`<sys/sysmacros.h>`](sys-sysmacros.md) | 0 | device numbers: a major (the driver) and a minor (the device), packed as Linux packs them in 32 bits |
| [`<sys/time.h>`](sys-time.md) | 2 | struct timeval and gettimeofday |
| [`<sys/times.h>`](sys-times.md) | 1 | process CPU times |
| [`<sys/types.h>`](sys-types.md) | 0 | basic system types |
| [`<sys/uio.h>`](sys-uio.md) | 0 | struct iovec: one piece of a scattered buffer (sendmsg, recvmsg) |
| [`<sys/un.h>`](sys-un.md) | 0 | the address of a Unix domain socket |
| [`<sys/utsname.h>`](sys-utsname.md) | 1 | the name of the system: uname |
| [`<sys/wait.h>`](sys-wait.md) | 2 | waiting for child processes |
| [`<syscall.h>`](syscall.md) | 1 | This file contains the declarations of system call numbers and the raw syscall interface |
| [`<syslog.h>`](syslog.md) | 1 | the system log: ginnOS writes each message to the kernel log (/dev/kmsg), with the name, the pid and the level before it |
| [`<termios.h>`](termios.md) | 2 | terminal settings |
| [`<time.h>`](time.md) | 4 | clocks, sleeping and calendar time (UTC only: there are no time zones, so local time is UTC) |
| [`<unistd.h>`](unistd.md) | 56 | This file contains the declarations of POSIX-like system call wrappers |
| [`<utime.h>`](utime.md) | 1 | utime: set a file's times in whole seconds (the older call; see utimensat in &lt;sys/stat.h&gt;) |
| [`<wchar.h>`](wchar.md) | 8 | wide characters |
| [`<wctype.h>`](wctype.md) | 2 | the classes of wide characters (Unicode code points) |
