#pragma once

/**
 * @file tcp.h
 * @brief TCP (RFC 9293): reliable byte streams between ports.
 *
 * a connection (tcp_conn_t) keeps the state of one end: the sequence
 * numbers, a send buffer and a receive buffer, and the timers. the socket
 * code (tcp_socket.c) puts data in and takes data out; this code sends
 * and receives the segments.
 *
 * what it does:
 * - the three-way handshake, both ways; a listener with a queue of
 *   connections to accept.
 * - retransmission with a timeout from the measured round-trip time
 *   (RFC 6298), backing off to TCP_MAX_RETRIES; fast retransmit after
 *   three duplicate acks.
 * - slow start and congestion avoidance (RFC 5681).
 * - the receive window (no window scaling, so at most 64 KiB); a probe
 *   when the peer's window is zero.
 * - the MSS option.
 * - close with FIN (TIME_WAIT for TCP_TIME_WAIT_MS), reset with RST.
 * what it does not do (yet): segments that come out of order are dropped
 * (the sender sends them again), no selective acks, no timestamps, no
 * keepalive, no urgent data. acks are sent at once (no delayed acks), and
 * small writes go at once (no Nagle).
 *
 * locking: all of it runs with net_lock held. a connection is freed only
 * by tcp_timer (closed and with no socket), never while the stack works on
 * it, so a send that loops back through lo cannot free the connection
 * under its caller.
 */

#include "ipv4.h"
#include "kernel/sync/wait_queue.h"

#define TCP_BUFFER_SIZE  65535 // each of the send and the receive buffer
#define TCP_MAX_RETRIES  8     // timeouts in a row before the connection fails
#define TCP_SYN_RETRIES  5
#define TCP_TIME_WAIT_MS 30000
#define TCP_BACKLOG_MAX  32

typedef enum
{
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT,
} tcp_state_t;

typedef struct tcp_conn tcp_conn_t;

struct tcp_conn
{
    tcp_conn_t *next; // on the list of all connections
    tcp_state_t state;
    uint32_t local;
    uint32_t remote;
    uint16_t local_port; // host byte order
    uint16_t remote_port;

    // sending: the buffer holds the bytes from snd_una on (sent and not
    // acked, then not sent yet); SYN and FIN are not in it
    uint32_t iss;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t snd_wnd;
    uint32_t snd_wl1;
    uint32_t snd_wl2;
    uint8_t *send_buffer;
    uint32_t send_head; // where the byte at snd_una is
    uint32_t send_count;
    bool fin_wanted; // the user closed for writing: FIN after the data
    bool fin_sent;
    bool fin_acked;
    uint16_t mss; // the most data in one segment to the peer

    // receiving
    uint32_t irs;
    uint32_t rcv_nxt;
    uint8_t *receive_buffer;
    uint32_t receive_head;
    uint32_t receive_count;
    uint32_t window_sent; // the window in the last segment sent
    bool fin_received;
    bool shut_read; // the user takes no more data: it is thrown away

    // timers and congestion control (times in ticks)
    uint64_t retransmit_at; // 0: off
    uint32_t rto_ms;
    uint32_t srtt_ms; // 0: no measurement yet
    uint32_t rttvar_ms;
    bool timing;      // a segment is timed for the round-trip time
    uint32_t timed_seq;
    uint64_t timed_at;
    uint32_t retries;
    uint32_t cwnd;
    uint32_t ssthresh;
    uint32_t dup_acks;
    uint64_t closes_at; // TIME_WAIT, or an orphan in FIN_WAIT_2

    int32_t error;  // -ECONNREFUSED, -ECONNRESET, -ETIMEDOUT (for the socket)
    bool ack_now;   // send an ack with the next output
    bool orphan;    // no socket has it: freed when closed

    // a listener's queue: connections that finished the handshake
    tcp_conn_t *parent; // the listener of a connection not accepted yet
    tcp_conn_t *queue_head;
    tcp_conn_t *queue_tail;
    tcp_conn_t *queue_next;
    uint32_t backlog;
    uint32_t children; // connections of this listener not accepted yet

    wait_queue_t waiters; // readers, writers, connect and accept
};

/**
 * set up TCP (from net_init): the timer.
 */
void tcp_init(void);

/**
 * a received segment (net_lock held).
 */
void tcp_receive(netif_t *netif, const ipv4_header_t *ip, const uint8_t *segment,
                 uint32_t length);

/**
 * start a connection: send a SYN (net_lock held).
 * @return the connection in SYN_SENT (or already ESTABLISHED over lo), or
 *         NULL if there is no memory.
 */
tcp_conn_t *tcp_connect(uint32_t local, uint16_t local_port, uint32_t remote,
                        uint16_t remote_port);

/**
 * a listener on a port (net_lock held).
 * @param local the address, or 0 for any.
 * @return the connection in LISTEN, or NULL if there is no memory.
 */
tcp_conn_t *tcp_listen(uint32_t local, uint16_t local_port, uint32_t backlog);

/**
 * take the next connection from a listener's queue (net_lock held).
 * @return it (not orphaned: the caller gives it a socket), or NULL.
 */
tcp_conn_t *tcp_accept(tcp_conn_t *listener);

/**
 * put data into the send buffer and send what the windows allow (net_lock
 * held).
 * @return the bytes taken (0 when the buffer is full).
 */
uint32_t tcp_write(tcp_conn_t *conn, const uint8_t *data, uint32_t length);

/**
 * take data from the receive buffer (net_lock held). this opens the window
 * again, and the peer is told when that matters.
 * @param peek leave the data in the buffer.
 * @return the bytes taken.
 */
uint32_t tcp_read(tcp_conn_t *conn, uint8_t *out, uint32_t length, bool peek);

/**
 * no more data from the user: send FIN after the data (net_lock held).
 */
void tcp_shutdown_write(tcp_conn_t *conn);

/**
 * the socket lets the connection go (net_lock held): a listener resets
 * the connections in its queue; a connection with data the user did not
 * read is reset; any other is closed with FIN. the connection is freed
 * when it is closed.
 */
void tcp_release(tcp_conn_t *conn);

/**
 * the room in the send buffer.
 */
uint32_t tcp_send_room(const tcp_conn_t *conn);

/**
 * true in the states where the user can send data.
 */
bool tcp_can_send(const tcp_conn_t *conn);

/**
 * the timers, each tick (net_lock is taken here).
 */
void tcp_timer(uint64_t now);
