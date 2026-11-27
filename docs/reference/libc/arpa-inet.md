<!-- Made by tools/docs/make_reference.py from the source. Do not edit. -->

# &lt;arpa/inet.h&gt;

Source: [`src/libc/include/arpa/inet.h`](https://github.com/lassedtu/ginnOS/blob/main/src/libc/include/arpa/inet.h)

IPv4 addresses as text ("10.0.2.15") and back.

## inet\_pton

```c
int inet_pton(int af, const char *src, void *dst);
```

text to an address.

- `af`: AF\_INET.
- `dst`: receives a struct in\_addr.

Returns: 1, 0 if src is not an address, -1 (EAFNOSUPPORT) for another af.

## inet\_ntop

```c
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
```

an address to text.

- `size`: at least INET\_ADDRSTRLEN for AF\_INET.

Returns: dst, or NULL (EAFNOSUPPORT, ENOSPC).

## inet\_addr

```c
in_addr_t inet_addr(const char *text);
```

text to an address, as inet\_aton does; the address, or INADDR\_NONE.

## inet\_aton

```c
int inet_aton(const char *text, struct in_addr *out);
```

text to an address. as well as a.b.c.d this takes the older forms: a.b.c
(c is 16 bits), a.b (b is 24 bits) and a (32 bits), each part in
decimal, octal (0) or hex (0x).

Returns: 1, or 0 if text is not an address.

## inet\_ntoa

```c
char *inet_ntoa(struct in_addr in);
```

an address to text, in a buffer of the calling thread.
