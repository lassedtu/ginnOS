/**
 * @file tcp.c
 * @brief TCP (see tcp.h).
 *
 * sequence numbers wrap, so they are compared with seq_lt and the others,
 * which look at the signed difference.
 */

#include "tcp.h"

#include "byteorder.h"
#include "checksum.h"
#include "common/memory.h"
#include "kernel/errno.h"
#include "kernel/memory/heap.h"
#include "kernel/time/timer.h"
#include "kernel/vfs/poll.h"
#include "netif.h"

#define HEADER_SIZE 20
#define OPTION_MSS  2

#define FLAG_FIN 0x01
#define FLAG_SYN 0x02
#define FLAG_RST 0x04
#define FLAG_PSH 0x08
#define FLAG_ACK 0x10

#define DEFAULT_MSS    536 // when the peer sends no MSS option (RFC 9293)
#define INITIAL_RTO_MS 1000
#define MIN_RTO_MS     200
#define MAX_RTO_MS     60000
#define INITIAL_CWND   10 // segments (RFC 6928)
#define ORPHAN_FIN_WAIT_2_MS 60000

static tcp_conn_t *connections;
static uint32_t iss_counter;

// the segment being sent (net_lock held)
static uint8_t segment_out[HEADER_SIZE + 4 + ETH_MTU];

static bool seq_lt(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

static bool seq_le(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) <= 0;
}

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/**
 * an initial sequence number: from the clock and a counter, so a new
 * connection does not take the numbers of an old one.
 */
static uint32_t new_iss(void)
{
    iss_counter += 0x9E3779B9u;
    return (uint32_t)timer_now() * 250000u + iss_counter;
}

static uint32_t receive_room(const tcp_conn_t *c)
{
    return c->receive_buffer ? TCP_BUFFER_SIZE - c->receive_count : 0;
}

uint32_t tcp_send_room(const tcp_conn_t *c)
{
    return c->send_buffer ? TCP_BUFFER_SIZE - c->send_count : 0;
}

bool tcp_can_send(const tcp_conn_t *c)
{
    return (c->state == TCP_ESTABLISHED || c->state == TCP_CLOSE_WAIT) && !c->fin_wanted;
}

static void wake(tcp_conn_t *c)
{
    wait_queue_wake_all(&c->waiters);
    poll_notify();
}

static tcp_conn_t *conn_new(void)
{
    tcp_conn_t *c = kcalloc(1, sizeof(*c));
    if (!c)
    {
        return NULL;
    }
    wait_queue_init(&c->waiters);
    c->rto_ms = INITIAL_RTO_MS;
    c->mss = DEFAULT_MSS;
    c->ssthresh = TCP_BUFFER_SIZE;
    c->next = connections;
    connections = c;
    return c;
}

/**
 * the buffers of a connection that carries data.
 */
static bool conn_buffers(tcp_conn_t *c)
{
    c->send_buffer = kmalloc(TCP_BUFFER_SIZE);
    c->receive_buffer = kmalloc(TCP_BUFFER_SIZE);
    return c->send_buffer && c->receive_buffer;
}

/**
 * the MSS this end can take on the interface to the peer. segments are
 * built in segment_out, so not more than an Ethernet frame holds, also on
 * lo.
 */
static uint16_t our_mss(uint32_t remote)
{
    uint32_t next_hop;
    netif_t *netif = ipv4_route(remote, &next_hop);
    uint32_t mtu = netif ? netif->mtu : 576;
    return (uint16_t)(min_u32(mtu, ETH_MTU) - IPV4_HEADER_SIZE - HEADER_SIZE);
}

/**
 * send one segment.
 * @param data_offset where the data starts in the send buffer (from
 *        snd_una), with length bytes.
 */
static void send_segment(tcp_conn_t *c, uint32_t seq, uint8_t flags, uint32_t data_offset,
                         uint32_t length)
{
    uint8_t *s = segment_out;
    uint32_t header = HEADER_SIZE + ((flags & FLAG_SYN) ? 4 : 0);
    put16(s, c->local_port);
    put16(s + 2, c->remote_port);
    put32(s + 4, seq);
    put32(s + 8, (flags & FLAG_ACK) ? c->rcv_nxt : 0);
    s[12] = (uint8_t)((header / 4) << 4);
    s[13] = flags;
    uint32_t window = min_u32(receive_room(c), 65535);
    c->window_sent = window;
    put16(s + 14, (uint16_t)window);
    put16(s + 16, 0); // the checksum
    put16(s + 18, 0); // no urgent data
    if (flags & FLAG_SYN)
    {
        s[20] = OPTION_MSS;
        s[21] = 4;
        put16(s + 22, our_mss(c->remote));
    }
    // the data, from the ring
    uint32_t at = (c->send_head + data_offset) % TCP_BUFFER_SIZE;
    uint32_t first = min_u32(length, TCP_BUFFER_SIZE - at);
    memcpy(s + header, c->send_buffer + at, first);
    memcpy(s + header + first, c->send_buffer, length - first);

    uint32_t total = header + length;
    uint32_t sum = ipv4_pseudo_sum(c->local, c->remote, IP_PROTO_TCP, total);
    uint16_t check = checksum_finish(checksum_add(sum, s, total));
    memcpy(s + 16, &check, 2);
    if (flags & FLAG_ACK)
    {
        c->ack_now = false;
    }
    ipv4_send(c->local, c->remote, IP_PROTO_TCP, s, total);
}

/**
 * answer a segment that has no connection with a reset (RFC 9293 3.10.7.1).
 */
static void send_reset(const ipv4_header_t *ip, const uint8_t *seg, uint32_t data_length)
{
    uint8_t flags = seg[13];
    if (flags & FLAG_RST)
    {
        return;
    }
    uint8_t *s = segment_out;
    memset(s, 0, HEADER_SIZE);
    put16(s, get16(seg + 2));
    put16(s + 2, get16(seg));
    if (flags & FLAG_ACK)
    {
        put32(s + 4, get32(seg + 8));
        s[13] = FLAG_RST;
    }
    else
    {
        uint32_t length = data_length + ((flags & FLAG_SYN) ? 1 : 0) + ((flags & FLAG_FIN) ? 1 : 0);
        put32(s + 8, get32(seg + 4) + length);
        s[13] = FLAG_RST | FLAG_ACK;
    }
    s[12] = (HEADER_SIZE / 4) << 4;
    uint32_t sum = ipv4_pseudo_sum(ip->destination, ip->source, IP_PROTO_TCP, HEADER_SIZE);
    uint16_t check = checksum_finish(checksum_add(sum, s, HEADER_SIZE));
    memcpy(s + 16, &check, 2);
    ipv4_send(ip->destination, ip->source, IP_PROTO_TCP, s, HEADER_SIZE);
}

static void arm_retransmit(tcp_conn_t *c)
{
    c->retransmit_at = timer_now() + timer_ms_to_ticks(c->rto_ms);
}

/**
 * the connection is over: no more segments, and the waiters see why.
 */
static void conn_close(tcp_conn_t *c, int32_t error)
{
    if (error != 0 && c->error == 0)
    {
        c->error = error;
    }
    c->state = TCP_CLOSED;
    c->retransmit_at = 0;
    c->closes_at = 0;
    wake(c);
    if (c->parent)
    {
        wake(c->parent);
    }
}

static void enter_time_wait(tcp_conn_t *c)
{
    c->state = TCP_TIME_WAIT;
    c->retransmit_at = 0;
    c->closes_at = timer_now() + timer_ms_to_ticks(TCP_TIME_WAIT_MS);
    wake(c);
}

/**
 * send what the windows allow: data, then the FIN; or an ack if one is
 * due and nothing else carried it. the loop reads the connection again
 * after each segment, as a send over lo can change it.
 */
static void output(tcp_conn_t *c)
{
    if (c->state == TCP_SYN_SENT || c->state == TCP_SYN_RECEIVED || c->state == TCP_LISTEN ||
        c->state == TCP_CLOSED)
    {
        return;
    }
    bool sending = c->state == TCP_ESTABLISHED || c->state == TCP_CLOSE_WAIT ||
                   c->state == TCP_FIN_WAIT_1 || c->state == TCP_CLOSING ||
                   c->state == TCP_LAST_ACK;
    for (int guard = 0; sending && guard < 64; guard++)
    {
        uint32_t in_flight = c->snd_nxt - c->snd_una;
        if (c->fin_sent)
        {
            break; // all data was sent with the FIN before it
        }
        uint32_t window = min_u32(c->snd_wnd, c->cwnd);
        if (in_flight < c->send_count && in_flight < window)
        {
            uint32_t length = min_u32(c->mss, c->send_count - in_flight);
            length = min_u32(length, window - in_flight);
            bool last = in_flight + length == c->send_count;
            if (!c->timing)
            {
                c->timing = true;
                c->timed_seq = c->snd_nxt + length;
                c->timed_at = timer_now();
            }
            c->snd_nxt = c->snd_una + in_flight + length;
            if (!c->retransmit_at)
            {
                arm_retransmit(c);
            }
            send_segment(c, c->snd_una + in_flight, FLAG_ACK | (last ? FLAG_PSH : 0), in_flight,
                         length);
            continue;
        }
        if (c->fin_wanted && in_flight == c->send_count)
        {
            c->fin_sent = true;
            c->snd_nxt = c->snd_una + c->send_count + 1;
            if (c->state == TCP_ESTABLISHED)
            {
                c->state = TCP_FIN_WAIT_1;
            }
            else if (c->state == TCP_CLOSE_WAIT)
            {
                c->state = TCP_LAST_ACK;
            }
            if (!c->retransmit_at)
            {
                arm_retransmit(c);
            }
            send_segment(c, c->snd_una + c->send_count, FLAG_FIN | FLAG_ACK, 0, 0);
            continue;
        }
        if (c->snd_wnd == 0 && c->send_count > in_flight && !c->retransmit_at)
        {
            arm_retransmit(c); // the window probe comes with the timer
        }
        break;
    }
    if (c->ack_now)
    {
        send_segment(c, c->snd_nxt, FLAG_ACK, 0, 0);
    }
}

/**
 * the MSS option of a SYN, if it has one.
 */
static uint16_t parse_mss(const uint8_t *seg, uint32_t header)
{
    uint32_t at = HEADER_SIZE;
    while (at < header)
    {
        uint8_t kind = seg[at];
        if (kind == 0)
        {
            break;
        }
        if (kind == 1)
        {
            at++;
            continue;
        }
        if (at + 1 >= header || seg[at + 1] < 2 || at + seg[at + 1] > header)
        {
            break;
        }
        if (kind == OPTION_MSS && seg[at + 1] == 4)
        {
            uint16_t mss = get16(seg + at + 2);
            return mss < 64 ? 64 : mss;
        }
        at += seg[at + 1];
    }
    return DEFAULT_MSS;
}

static void update_rtt(tcp_conn_t *c, uint32_t ms)
{
    if (c->srtt_ms == 0)
    {
        c->srtt_ms = ms ? ms : 1;
        c->rttvar_ms = ms / 2;
    }
    else
    {
        uint32_t delta = c->srtt_ms > ms ? c->srtt_ms - ms : ms - c->srtt_ms;
        c->rttvar_ms = (3 * c->rttvar_ms + delta) / 4;
        c->srtt_ms = (7 * c->srtt_ms + ms) / 8;
    }
    uint32_t variance = 4 * c->rttvar_ms < 10 ? 10 : 4 * c->rttvar_ms;
    uint32_t rto = c->srtt_ms + variance;
    c->rto_ms = rto < MIN_RTO_MS ? MIN_RTO_MS : rto > MAX_RTO_MS ? MAX_RTO_MS : rto;
}

/**
 * an ack of new data (snd_una < ack <= snd_nxt).
 */
static void acked(tcp_conn_t *c, uint32_t ack)
{
    uint32_t count = ack - c->snd_una;
    bool fin = c->fin_sent && ack == c->snd_una + c->send_count + 1;
    uint32_t data = fin ? count - 1 : count;
    if (data > c->send_count)
    {
        data = c->send_count;
    }
    c->send_head = (c->send_head + data) % TCP_BUFFER_SIZE;
    c->send_count -= data;
    c->snd_una = ack;
    if (fin)
    {
        c->fin_acked = true;
    }
    if (c->timing && seq_le(c->timed_seq, ack))
    {
        c->timing = false;
        update_rtt(c, (uint32_t)(timer_now() - c->timed_at) * (1000 / TIMER_HZ));
    }
    // congestion control: slow start, then one MSS per window
    if (c->cwnd < c->ssthresh)
    {
        c->cwnd += min_u32(data, c->mss);
    }
    else if (c->cwnd > 0)
    {
        c->cwnd += (uint32_t)c->mss * c->mss / c->cwnd + 1;
    }
    c->retries = 0;
    c->dup_acks = 0;
    c->retransmit_at = 0;
    if (c->snd_una != c->snd_nxt)
    {
        arm_retransmit(c); // more is on the way
    }
}

static void established(tcp_conn_t *c, uint16_t window, uint32_t seq, uint32_t ack)
{
    c->state = TCP_ESTABLISHED;
    c->snd_wnd = window;
    c->snd_wl1 = seq;
    c->snd_wl2 = ack;
    c->cwnd = (uint32_t)INITIAL_CWND * c->mss;
    c->retries = 0;
    c->retransmit_at = 0;
}

/**
 * a SYN to a listener: a new connection in SYN_RECEIVED, and a SYN-ACK.
 */
static void listener_syn(tcp_conn_t *l, const ipv4_header_t *ip, const uint8_t *seg,
                         uint32_t header)
{
    if (l->children >= l->backlog)
    {
        return; // full: the peer tries again
    }
    tcp_conn_t *c = conn_new();
    if (!c)
    {
        return;
    }
    c->orphan = true; // until it is accepted
    if (!conn_buffers(c))
    {
        conn_close(c, 0);
        return;
    }
    c->parent = l;
    l->children++;
    c->local = ip->destination;
    c->remote = ip->source;
    c->local_port = get16(seg + 2);
    c->remote_port = get16(seg);
    c->irs = get32(seg + 4);
    c->rcv_nxt = c->irs + 1;
    c->iss = new_iss();
    c->snd_una = c->iss;
    c->snd_nxt = c->iss + 1;
    c->snd_wnd = get16(seg + 14);
    c->mss = min_u32(parse_mss(seg, header), our_mss(c->remote));
    c->state = TCP_SYN_RECEIVED;
    arm_retransmit(c);
    send_segment(c, c->iss, FLAG_SYN | FLAG_ACK, 0, 0);
}

/**
 * take a connection out of its listener (it was reset, or is accepted).
 */
static void leave_listener(tcp_conn_t *c)
{
    tcp_conn_t *l = c->parent;
    if (!l)
    {
        return;
    }
    for (tcp_conn_t **p = &l->queue_head; *p; p = &(*p)->queue_next)
    {
        if (*p == c)
        {
            *p = c->queue_next;
            if (l->queue_tail == c)
            {
                l->queue_tail = NULL;
                for (tcp_conn_t *q = l->queue_head; q; q = q->queue_next)
                {
                    l->queue_tail = q;
                }
            }
            break;
        }
    }
    c->queue_next = NULL;
    c->parent = NULL;
    l->children--;
}

static tcp_conn_t *find(uint32_t local, uint16_t local_port, uint32_t remote,
                        uint16_t remote_port)
{
    tcp_conn_t *listener = NULL;
    for (tcp_conn_t *c = connections; c; c = c->next)
    {
        if (c->local_port != local_port || c->state == TCP_CLOSED)
        {
            continue;
        }
        if (c->state == TCP_LISTEN)
        {
            if ((c->local == 0 || c->local == local) && (!listener || c->local != 0))
            {
                listener = c;
            }
            continue;
        }
        if (c->remote_port == remote_port && c->remote == remote && c->local == local)
        {
            return c;
        }
    }
    return listener;
}

/**
 * a segment for a connection in SYN_SENT.
 */
static void syn_sent(tcp_conn_t *c, const uint8_t *seg, uint32_t header)
{
    uint8_t flags = seg[13];
    uint32_t seq = get32(seg + 4);
    uint32_t ack = get32(seg + 8);
    bool ack_ok = (flags & FLAG_ACK) && seq_lt(c->iss, ack) && seq_le(ack, c->snd_nxt);
    if ((flags & FLAG_ACK) && !ack_ok)
    {
        return; // an old segment (a reset would go with RFC 9293; the peer resends)
    }
    if (flags & FLAG_RST)
    {
        if (ack_ok)
        {
            conn_close(c, -ECONNREFUSED);
        }
        return;
    }
    if (!(flags & FLAG_SYN))
    {
        return;
    }
    c->irs = seq;
    c->rcv_nxt = seq + 1;
    c->mss = min_u32(parse_mss(seg, header), c->mss);
    if (ack_ok)
    {
        c->snd_una = ack;
        established(c, get16(seg + 14), seq, ack);
        c->ack_now = true;
        output(c);
        wake(c);
    }
    else
    {
        // both ends sent a SYN at once
        c->state = TCP_SYN_RECEIVED;
        send_segment(c, c->iss, FLAG_SYN | FLAG_ACK, 0, 0);
    }
}

/**
 * true if a segment is in the receive window (RFC 9293 3.10.7.4).
 */
static bool acceptable(const tcp_conn_t *c, uint32_t seq, uint32_t length)
{
    uint32_t window = receive_room(c);
    if (window == 0)
    {
        return length == 0 && seq == c->rcv_nxt;
    }
    uint32_t end = c->rcv_nxt + window;
    bool start_in = seq_le(c->rcv_nxt, seq) && seq_lt(seq, end);
    if (length == 0)
    {
        return start_in;
    }
    uint32_t last = seq + length - 1;
    return start_in || (seq_le(c->rcv_nxt, last) && seq_lt(last, end));
}

/**
 * put received data into the receive buffer.
 */
static void take_data(tcp_conn_t *c, const uint8_t *data, uint32_t length)
{
    if (c->shut_read)
    {
        c->rcv_nxt += length; // taken, and thrown away
        return;
    }
    uint32_t at = (c->receive_head + c->receive_count) % TCP_BUFFER_SIZE;
    uint32_t first = min_u32(length, TCP_BUFFER_SIZE - at);
    memcpy(c->receive_buffer + at, data, first);
    memcpy(c->receive_buffer, data + first, length - first);
    c->receive_count += length;
    c->rcv_nxt += length;
}

/**
 * a segment for a synchronized connection (SYN_RECEIVED and after).
 */
static void synchronized(tcp_conn_t *c, const uint8_t *seg, uint32_t header, uint32_t length)
{
    uint8_t flags = seg[13];
    uint32_t seq = get32(seg + 4);
    uint32_t ack = get32(seg + 8);
    const uint8_t *data = seg + header;
    uint32_t data_length = length - header;
    bool fin = (flags & FLAG_FIN) != 0;
    uint32_t seg_length = data_length + ((flags & FLAG_SYN) ? 1 : 0) + (fin ? 1 : 0);

    if (c->state == TCP_TIME_WAIT)
    {
        // a FIN sent again: ack it again, and wait longer
        if (fin)
        {
            c->ack_now = true;
            output(c);
            c->closes_at = timer_now() + timer_ms_to_ticks(TCP_TIME_WAIT_MS);
        }
        return;
    }

    // 1: the sequence number
    if (!acceptable(c, seq, seg_length))
    {
        if (!(flags & FLAG_RST))
        {
            c->ack_now = true;
            output(c);
        }
        return;
    }
    // 2: a reset
    if (flags & FLAG_RST)
    {
        if (c->state == TCP_SYN_RECEIVED && c->parent)
        {
            leave_listener(c);
            conn_close(c, 0);
        }
        else
        {
            conn_close(c, -ECONNRESET);
        }
        return;
    }
    // 3: a SYN in the window is an error: reset (RFC 5961 would ack instead)
    if ((flags & FLAG_SYN) && seq != c->irs)
    {
        send_segment(c, c->snd_nxt, FLAG_RST, 0, 0);
        conn_close(c, -ECONNRESET);
        return;
    }
    // 4: the ack
    if (!(flags & FLAG_ACK))
    {
        return;
    }
    uint16_t window = get16(seg + 14);
    if (c->state == TCP_SYN_RECEIVED)
    {
        if (!(seq_lt(c->snd_una, ack) && seq_le(ack, c->snd_nxt)))
        {
            send_segment(c, ack, FLAG_RST, 0, 0); // an ack of something never sent
            return;
        }
        c->snd_una = ack;
        established(c, window, seq, ack);
        tcp_conn_t *l = c->parent;
        if (l)
        {
            // into the listener's queue, for accept
            if (l->queue_tail)
            {
                l->queue_tail->queue_next = c;
            }
            else
            {
                l->queue_head = c;
            }
            l->queue_tail = c;
            wake(l);
        }
    }
    else if (seq_lt(c->snd_una, ack) && seq_le(ack, c->snd_nxt))
    {
        acked(c, ack);
        wake(c);
    }
    else if (ack == c->snd_una && data_length == 0 && !fin && c->snd_una != c->snd_nxt &&
             window == c->snd_wnd)
    {
        // a duplicate ack: the third one resends the first segment at once
        if (++c->dup_acks == 3)
        {
            uint32_t in_flight = c->snd_nxt - c->snd_una;
            c->ssthresh = in_flight / 2 > 2u * c->mss ? in_flight / 2 : 2u * c->mss;
            c->cwnd = c->ssthresh;
            if (c->send_count > 0)
            {
                send_segment(c, c->snd_una, FLAG_ACK, 0, min_u32(c->mss, c->send_count));
            }
        }
    }
    else if (seq_lt(c->snd_nxt, ack))
    {
        c->ack_now = true; // an ack of data not sent yet
        output(c);
        return;
    }
    // the send window, from the newest segment
    if (seq_lt(c->snd_wl1, seq) || (c->snd_wl1 == seq && seq_le(c->snd_wl2, ack)))
    {
        bool opened = c->snd_wnd == 0 && window > 0;
        c->snd_wnd = window;
        c->snd_wl1 = seq;
        c->snd_wl2 = ack;
        if (opened)
        {
            c->retransmit_at = 0;
            c->retries = 0;
            wake(c);
        }
    }
    // our FIN acked: the closing states move on
    if (c->fin_acked)
    {
        if (c->state == TCP_FIN_WAIT_1)
        {
            c->state = TCP_FIN_WAIT_2;
            if (c->orphan)
            {
                c->closes_at = timer_now() + timer_ms_to_ticks(ORPHAN_FIN_WAIT_2_MS);
            }
        }
        else if (c->state == TCP_CLOSING)
        {
            enter_time_wait(c);
        }
        else if (c->state == TCP_LAST_ACK)
        {
            conn_close(c, 0);
            return;
        }
    }

    // 5: the data, in order only
    if (seq_lt(seq, c->rcv_nxt))
    {
        uint32_t old = c->rcv_nxt - seq;
        if (old > data_length)
        {
            fin = fin && old == data_length; // a FIN not seen yet, after old data
            data_length = 0;
        }
        else
        {
            data += old;
            data_length -= old;
        }
        seq = c->rcv_nxt;
    }
    bool takes_data = c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT_1 ||
                      c->state == TCP_FIN_WAIT_2;
    if (seq != c->rcv_nxt)
    {
        // out of order: dropped; the duplicate ack tells the sender
        c->ack_now = data_length > 0 || fin;
        data_length = 0;
        fin = false;
    }
    else if (data_length > 0 && takes_data)
    {
        uint32_t room = receive_room(c);
        if (data_length > room)
        {
            data_length = room;
            fin = false; // the FIN comes again after the rest
        }
        take_data(c, data, data_length);
        c->ack_now = true;
        wake(c);
    }

    // 6: the FIN
    if (fin && seq + data_length == c->rcv_nxt && takes_data)
    {
        c->rcv_nxt++;
        c->fin_received = true;
        c->ack_now = true;
        if (c->state == TCP_ESTABLISHED)
        {
            c->state = TCP_CLOSE_WAIT;
        }
        else if (c->state == TCP_FIN_WAIT_1)
        {
            c->state = c->fin_acked ? TCP_TIME_WAIT : TCP_CLOSING;
        }
        else if (c->state == TCP_FIN_WAIT_2)
        {
            c->state = TCP_TIME_WAIT;
        }
        if (c->state == TCP_TIME_WAIT)
        {
            enter_time_wait(c);
        }
        wake(c);
    }
    output(c);
}

void tcp_receive(netif_t *netif, const ipv4_header_t *ip, const uint8_t *seg, uint32_t length)
{
    (void)netif;
    if (length < HEADER_SIZE)
    {
        ipv4_stats.bad_header++;
        return;
    }
    uint32_t header = (uint32_t)(seg[12] >> 4) * 4;
    uint32_t sum = ipv4_pseudo_sum(ip->source, ip->destination, IP_PROTO_TCP, length);
    if (header < HEADER_SIZE || header > length || checksum_finish(checksum_add(sum, seg, length)))
    {
        ipv4_stats.bad_header++;
        return;
    }
    if (ip->destination == IPV4_BROADCAST)
    {
        return;
    }
    tcp_conn_t *c = find(ip->destination, get16(seg + 2), ip->source, get16(seg));
    uint8_t flags = seg[13];
    if (!c)
    {
        send_reset(ip, seg, length - header);
        return;
    }
    if (c->state == TCP_LISTEN)
    {
        if (flags & FLAG_RST)
        {
            return;
        }
        if (flags & FLAG_ACK)
        {
            send_reset(ip, seg, length - header);
            return;
        }
        if (flags & FLAG_SYN)
        {
            listener_syn(c, ip, seg, header);
        }
        return;
    }
    if (c->state == TCP_SYN_SENT)
    {
        syn_sent(c, seg, header);
        return;
    }
    synchronized(c, seg, header, length);
}

/**
 * free a connection (closed, and no socket has it).
 */
static void conn_free(tcp_conn_t *c)
{
    for (tcp_conn_t **p = &connections; *p; p = &(*p)->next)
    {
        if (*p == c)
        {
            *p = c->next;
            break;
        }
    }
    kfree(c->send_buffer);
    kfree(c->receive_buffer);
    kfree(c);
}

tcp_conn_t *tcp_connect(uint32_t local, uint16_t local_port, uint32_t remote,
                        uint16_t remote_port)
{
    tcp_conn_t *c = conn_new();
    if (!c)
    {
        return NULL;
    }
    if (!conn_buffers(c))
    {
        c->orphan = true; // the timer frees it
        return NULL;
    }
    c->local = local;
    c->local_port = local_port;
    c->remote = remote;
    c->remote_port = remote_port;
    c->iss = new_iss();
    c->snd_una = c->iss;
    c->snd_nxt = c->iss + 1;
    c->mss = our_mss(remote);
    c->state = TCP_SYN_SENT;
    arm_retransmit(c);
    send_segment(c, c->iss, FLAG_SYN, 0, 0);
    return c;
}

tcp_conn_t *tcp_listen(uint32_t local, uint16_t local_port, uint32_t backlog)
{
    tcp_conn_t *c = conn_new();
    if (!c)
    {
        return NULL;
    }
    c->local = local;
    c->local_port = local_port;
    c->backlog = backlog == 0 ? 1 : backlog > TCP_BACKLOG_MAX ? TCP_BACKLOG_MAX : backlog;
    c->state = TCP_LISTEN;
    return c;
}

tcp_conn_t *tcp_accept(tcp_conn_t *listener)
{
    tcp_conn_t *c = listener->queue_head;
    if (!c)
    {
        return NULL;
    }
    leave_listener(c);
    c->orphan = false;
    return c;
}

uint32_t tcp_write(tcp_conn_t *c, const uint8_t *data, uint32_t length)
{
    uint32_t n = min_u32(length, tcp_send_room(c));
    if (n == 0 || !tcp_can_send(c))
    {
        return 0;
    }
    uint32_t at = (c->send_head + c->send_count) % TCP_BUFFER_SIZE;
    uint32_t first = min_u32(n, TCP_BUFFER_SIZE - at);
    memcpy(c->send_buffer + at, data, first);
    memcpy(c->send_buffer, data + first, n - first);
    c->send_count += n;
    output(c);
    return n;
}

uint32_t tcp_read(tcp_conn_t *c, uint8_t *out, uint32_t length, bool peek)
{
    uint32_t n = min_u32(length, c->receive_count);
    uint32_t first = min_u32(n, TCP_BUFFER_SIZE - c->receive_head);
    memcpy(out, c->receive_buffer + c->receive_head, first);
    memcpy(out + first, c->receive_buffer, n - first);
    if (peek || n == 0)
    {
        return n;
    }
    c->receive_head = (c->receive_head + n) % TCP_BUFFER_SIZE;
    c->receive_count -= n;
    // tell the peer when the window opened enough to matter (RFC 9293 3.8.6.2.2)
    uint32_t room = min_u32(receive_room(c), 65535);
    if (room >= c->window_sent + 2u * c->mss || (c->window_sent < c->mss && room >= c->mss))
    {
        c->ack_now = true;
        output(c);
    }
    return n;
}

void tcp_shutdown_write(tcp_conn_t *c)
{
    if (c->state == TCP_SYN_SENT)
    {
        conn_close(c, 0);
        return;
    }
    if (c->state == TCP_ESTABLISHED || c->state == TCP_CLOSE_WAIT ||
        c->state == TCP_SYN_RECEIVED)
    {
        c->fin_wanted = true;
        output(c);
    }
}

/**
 * abort a connection: a reset to the peer.
 */
static void abort_conn(tcp_conn_t *c)
{
    if (c->state != TCP_CLOSED && c->state != TCP_LISTEN && c->state != TCP_SYN_SENT &&
        c->state != TCP_TIME_WAIT)
    {
        send_segment(c, c->snd_nxt, FLAG_RST | FLAG_ACK, 0, 0);
    }
    conn_close(c, 0);
}

void tcp_release(tcp_conn_t *c)
{
    c->orphan = true;
    if (c->state == TCP_LISTEN)
    {
        // the connections nobody accepted are reset
        for (tcp_conn_t *child = connections; child; child = child->next)
        {
            if (child->parent == c)
            {
                leave_listener(child);
                child->orphan = true;
                abort_conn(child);
            }
        }
        conn_close(c, 0);
        return;
    }
    if (c->state == TCP_SYN_SENT)
    {
        conn_close(c, 0);
        return;
    }
    if (c->receive_count > 0 && c->state != TCP_CLOSED && c->state != TCP_TIME_WAIT)
    {
        abort_conn(c); // data the user did not read (RFC 2525)
        return;
    }
    tcp_shutdown_write(c);
    if (c->state == TCP_FIN_WAIT_2)
    {
        c->closes_at = timer_now() + timer_ms_to_ticks(ORPHAN_FIN_WAIT_2_MS);
    }
}

/**
 * the retransmission timer of one connection ran out.
 */
static void retransmit(tcp_conn_t *c)
{
    c->retransmit_at = 0;
    c->timing = false;
    if (c->state == TCP_SYN_SENT || c->state == TCP_SYN_RECEIVED)
    {
        if (++c->retries > TCP_SYN_RETRIES)
        {
            leave_listener(c);
            conn_close(c, -ETIMEDOUT);
            return;
        }
        c->rto_ms = min_u32(c->rto_ms * 2, MAX_RTO_MS);
        arm_retransmit(c);
        send_segment(c, c->iss, c->state == TCP_SYN_SENT ? FLAG_SYN : FLAG_SYN | FLAG_ACK, 0, 0);
        return;
    }
    uint32_t in_flight = c->snd_nxt - c->snd_una;
    if (c->snd_wnd == 0 && in_flight == 0 && c->send_count > 0)
    {
        // the window probe: one byte; the answer says the window
        c->rto_ms = min_u32(c->rto_ms * 2, MAX_RTO_MS);
        arm_retransmit(c);
        send_segment(c, c->snd_una, FLAG_ACK, 0, 1);
        return;
    }
    if (++c->retries > TCP_MAX_RETRIES)
    {
        abort_conn(c);
        c->error = -ETIMEDOUT;
        return;
    }
    // go back to the oldest byte not acked, with a small window
    c->ssthresh = in_flight / 2 > 2u * c->mss ? in_flight / 2 : 2u * c->mss;
    c->cwnd = c->mss;
    c->snd_nxt = c->snd_una;
    if (c->fin_sent && !c->fin_acked)
    {
        c->fin_sent = false; // sent again after the data
    }
    c->rto_ms = min_u32(c->rto_ms * 2, MAX_RTO_MS);
    output(c);
    if (!c->retransmit_at && c->snd_una != c->snd_nxt)
    {
        arm_retransmit(c);
    }
}

void tcp_timer(uint64_t now)
{
    uint32_t flags = net_lock();
    tcp_conn_t *next;
    for (tcp_conn_t *c = connections; c; c = next)
    {
        next = c->next;
        if (c->closes_at && now >= c->closes_at)
        {
            conn_close(c, 0);
        }
        if (c->retransmit_at && now >= c->retransmit_at && c->state != TCP_CLOSED)
        {
            retransmit(c);
        }
    }
    // free the closed connections without a socket, after the work above
    for (tcp_conn_t *c = connections; c; c = next)
    {
        next = c->next;
        if (c->state == TCP_CLOSED && c->orphan && !c->parent)
        {
            conn_free(c);
        }
    }
    net_unlock(flags);
}

void tcp_init(void)
{
    timer_add_poll_hook(tcp_timer);
}
