# The network

ginnOS connects to a network through a virtio-net card. This is the
network card of QEMU and of most virtual machines. ginnOS has no driver
for the network card of a real PC yet. ginnOS has IPv4, and no IPv6.

The code: [`src/user/dhcp/`](https://github.com/lassedtu/ginnOS/tree/main/src/user/dhcp),
[`src/user/ip/`](https://github.com/lassedtu/ginnOS/tree/main/src/user/ip),
[`src/user/fetch/`](https://github.com/lassedtu/ginnOS/tree/main/src/user/fetch),
[`src/kernel/net/`](https://github.com/lassedtu/ginnOS/tree/main/src/kernel/net)

## Connect

You do not have to do anything. At boot, init starts `dhcp -b` for each
network card. `dhcp` gets an address, a gateway and the name servers
from the network, and shows them on the console:

```
dhcp: eth0: 10.0.2.15/24, gateway 10.0.2.2, lease 86400 s
```

`dhcp` then stays in the background and renews the lease. `make run`
gives QEMU a network card on QEMU's user network, where ginnOS gets
`10.0.2.15`. To start QEMU with no network card, use `make run NET=0`.

## See the network

`ip` shows the interfaces:

```
$ ip
lo: up, mtu 65535
    inet 127.0.0.1/8
eth0: up, mtu 1500
    ether 52:54:00:12:34:56
    inet 10.0.2.15/24  gateway 10.0.2.2
```

| Interface | What it is |
| --- | --- |
| `lo` | the loopback interface, `127.0.0.1` |
| `eth0`, `eth1`, ... | the network cards, in the order that the kernel finds them |

## The tools

| Command | What it does |
| --- | --- |
| `ping [-c count] [-i seconds] [-W seconds] host` | sends ICMP echo requests and shows the replies |
| `host name...` | shows the IPv4 addresses of a name |
| `fetch [-i] [-I] [-k] [-o file] url` | gets a file over HTTP or HTTPS |
| `curl` | curl, with HTTP and HTTPS (from the `curl` port) |
| `ip` | shows and sets the addresses of the interfaces |
| `dhcp [-b] [interface]` | gets an address for an interface |

Examples:

```
$ ping -c 1 example.com
PING example.com (172.66.147.243): 56 data bytes
64 bytes from 172.66.147.243: icmp_seq=0 time=10.0 ms
--- 172.66.147.243 ping statistics ---
1 packets transmitted, 1 received, 0% packet loss
round-trip min/avg/max = 10.0/10.0/10.0 ms

$ host example.com
example.com has address 104.20.23.154
example.com has address 172.66.147.243

$ fetch -o /tmp/index.html https://example.com
fetch: saved 577 bytes to /tmp/index.html
```

### fetch

`fetch` writes the body to the standard output, or to a file with `-o`.

| Option | Result |
| --- | --- |
| `-o file` | writes the body to `file` |
| `-i` | also writes the response headers |
| `-I` | sends `HEAD` and writes only the headers |
| `-k` | does not check the certificate of the server |

The URL is `http://` or `https://`, then the host, an optional `:port`
and the path. If the URL has no `http://` or `https://`, `fetch` uses
HTTP. `fetch` follows up to 5 redirects.

### HTTPS

`fetch` and `curl` use TLS 1.2 and 1.3 from the mbedTLS port. They
check the certificate of the server against the authorities in the
`ca-certificates` port. An image built with `make PORTS=` has no ports:
then there is no `curl`, and `fetch` has only HTTP.

`curl` has no HTTP/2, no compressed transfers and no IPv6.

## Names

A program looks up a name in this order:

1. `/etc/hosts`. Each line is an address, then its names:

   ```
   127.0.0.1 localhost
   ```

2. The name servers in `/etc/resolv.conf`. `dhcp` writes this file:

   ```
   # from dhcp (eth0)
   nameserver 10.0.2.3
   ```

   The resolver asks each name server two times, then the next one.

## Set an address by hand

To set an address, a netmask and a gateway without DHCP, use `ip` as
root:

```sh
$ ip addr set eth0 10.0.2.20/24
$ ip route set default via 10.0.2.2
```

The `dhcp` in the background sets its own address again when it renews
the lease. To keep your address, stop that `dhcp` first with `kill`.
Write the name servers in `/etc/resolv.conf`.

The address stays until the next boot.

## What does not work yet

- network cards other than virtio-net (for example Intel and Realtek
  cards in real PCs)
- IPv6
- a firewall, and incoming connections from outside QEMU's user
  network (QEMU's user network lets ginnOS connect out only)
- a settings file for a static address
