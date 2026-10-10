/* core/loop, interne : tas min des timers (loop_heap.c). Non exporté. */
#ifndef P2P_CORE_LOOP_INTERNAL_H
#define P2P_CORE_LOOP_INTERNAL_H
#include "core/loop/loop.h"

#include <stddef.h>

typedef struct timer_node {
    uint64_t deadline;
    uint64_t id;
    uint64_t repeat;
    loop_timer_cb cb;
    void *ud;
    int cancelled;
} timer_node;

typedef struct timer_heap {
    timer_node *items;
    size_t len;
    size_t cap;
} timer_heap;

int  timer_heap_push(timer_heap *h, timer_node n);   /* 0 ou -ENOMEM */
void timer_heap_pop(timer_heap *h);
timer_node *timer_heap_top_live(timer_heap *h);      /* NULL si aucun timer vivant ; purge les annulés */
void timer_heap_cancel(timer_heap *h, uint64_t id);
void timer_heap_free(timer_heap *h);

#endif
