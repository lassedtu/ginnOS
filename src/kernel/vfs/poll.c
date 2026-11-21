#include "poll.h"

#include "kernel/time/timer.h"

static timed_queue_t poll_queue;

void poll_init(void)
{
    timed_queue_init(&poll_queue);
}

void poll_notify(void)
{
    timed_queue_wake_all(&poll_queue);
}

void poll_wait(uint64_t deadline)
{
    timed_queue_block(&poll_queue, deadline);
}
