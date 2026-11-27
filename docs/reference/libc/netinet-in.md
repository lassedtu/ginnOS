<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;netinet/in.h&gt;

Source: [`src/libc/include/netinet/in.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/netinet/in.h)

IPv4 addresses and ports, and the IP protocols.

the IPv6 types are here for programs that know both families. there is
no IPv6 network: socket(AF\_INET6, ...) gives EAFNOSUPPORT, and programs
then use IPv4.

addresses and ports in the structs are in network byte order (big
endian); htons, htonl, ntohs and ntohl change between it and the CPU's.

This header has no doc comments yet.
