<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;sys/input.h&gt;

Source: [`src/libc/include/sys/input.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/sys/input.h)

the events of /dev/input (ABI).

one event is 12 bytes: a time, a type, a code and a value. the types and
codes are the ones of Linux (linux/input-event-codes.h), so ported
programs know them. a group of events that belong together (a mouse
moving in x and y, and a button) ends with EV\_SYN / SYN\_REPORT.

the kernel has a copy in src/kernel/input/input\_event.h; the two must
stay the same.

This header has no doc comments yet.
