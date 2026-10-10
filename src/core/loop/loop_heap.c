/* core/loop : tas binaire min sur l'échéance des timers. Annulation paresseuse (flag, purge au sommet). */
#include "core/loop/loop_internal.h"

#include <errno.h>
#include <stdlib.h>

#define TIMER_HEAP_INIT 16

static void heap_swap(timer_heap *h, size_t a, size_t b)
{
    timer_node t = h->items[a];

    h->items[a] = h->items[b];
    h->items[b] = t;
}

static void heap_up(timer_heap *h, size_t i)
{
    while (i > 0) {
        size_t p = (i - 1) / 2;

        if (h->items[i].deadline >= h->items[p].deadline) {
            break;
        }
        heap_swap(h, i, p);
        i = p;
    }
}

static void heap_down(timer_heap *h, size_t i)
{
    for (;;) {
        size_t s = i;
        size_t a = 2 * i + 1;
        size_t b = a + 1;

        if (a < h->len && h->items[a].deadline < h->items[s].deadline) {
            s = a;
        }
        if (b < h->len && h->items[b].deadline < h->items[s].deadline) {
            s = b;
        }
        if (s == i) {
            return;
        }
        heap_swap(h, i, s);
        i = s;
    }
}

static int heap_grow(timer_heap *h)
{
    size_t cap = h->cap ? h->cap * 2 : TIMER_HEAP_INIT;
    timer_node *items = realloc(h->items, cap * sizeof *items);

    if (!items) {
        return -ENOMEM;
    }
    h->items = items;
    h->cap = cap;
    return 0;
}

int timer_heap_push(timer_heap *h, timer_node n)
{
    size_t i;

    if (h->len == h->cap && heap_grow(h) != 0) {
        return -ENOMEM;
    }
    i = h->len++;
    h->items[i] = n;
    heap_up(h, i);
    return 0;
}

void timer_heap_pop(timer_heap *h)
{
    if (h->len == 0) {
        return;
    }
    h->len--;
    if (h->len == 0) {
        return;
    }
    h->items[0] = h->items[h->len];
    heap_down(h, 0);
}

timer_node *timer_heap_top_live(timer_heap *h)
{
    while (h->len > 0) {
        if (!h->items[0].cancelled) {
            return &h->items[0];
        }
        timer_heap_pop(h);
    }
    return NULL;
}

void timer_heap_cancel(timer_heap *h, uint64_t id)
{
    size_t i;

    for (i = 0; i < h->len; ++i) {
        if (h->items[i].id == id) {
            h->items[i].cancelled = 1;
            return;
        }
    }
}

void timer_heap_free(timer_heap *h)
{
    free(h->items);
    h->items = NULL;
    h->len = 0;
    h->cap = 0;
}
