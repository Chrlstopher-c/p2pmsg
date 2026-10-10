#define _GNU_SOURCE
/* app/api/app_queue : file de commandes inter-threads (FIFO, bornée, sans perte sous concurrence). */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>

#include "app/api/app_queue.h"
#include "test.h"

#define PRODUCERS 4
#define PER_PROD 1000

typedef struct {
    app_queue *q;
    intptr_t   prod;
} producer_arg;

static app_cmd *make_cmd(uint64_t id)
{
    app_cmd *c = calloc(1, sizeof *c);
    if (c != NULL) {
        c->id = id;
    }
    return c;
}

static void *producer(void *arg)
{
    producer_arg *pa = arg;
    for (uint64_t i = 0; i < PER_PROD; i++) {
        app_cmd *c = make_cmd(((uint64_t)pa->prod << 32) | i);
        if (c == NULL) {
            return NULL;
        }
        while (aq_push(pa->q, c) == -ENOSPC) {
            sched_yield();
        }
    }
    return NULL;
}

static void test_fifo(void)
{
    app_queue q;
    CHECK_EQ_INT(aq_init(&q), 0);
    CHECK_EQ_INT(aq_push(&q, make_cmd(1)), 0);
    CHECK_EQ_INT(aq_push(&q, make_cmd(2)), 0);
    CHECK_EQ_INT(aq_push(&q, make_cmd(3)), 0);
    app_cmd *list = aq_take_all(&q);
    CHECK(list != NULL);
    CHECK_EQ_INT(list->id, 1);
    CHECK_EQ_INT(list->next->id, 2);
    CHECK_EQ_INT(list->next->next->id, 3);
    CHECK(list->next->next->next == NULL);
    aq_free_list(list);
    CHECK(aq_take_all(&q) == NULL);
    aq_destroy(&q);
}

static void test_full_keeps_ownership(void)
{
    app_queue q;
    CHECK_EQ_INT(aq_init(&q), 0);
    for (size_t i = 0; i < APP_QUEUE_MAX; i++) {
        CHECK_EQ_INT(aq_push(&q, make_cmd(i)), 0);
    }
    app_cmd *extra = make_cmd(999);
    CHECK_EQ_INT(aq_push(&q, extra), -ENOSPC);
    CHECK(extra->next == NULL);
    free(extra);
    aq_destroy(&q);
}

static void test_destroy_frees_pending(void)
{
    app_queue q;
    CHECK_EQ_INT(aq_init(&q), 0);
    CHECK_EQ_INT(aq_push(&q, make_cmd(1)), 0);
    CHECK_EQ_INT(aq_push(&q, make_cmd(2)), 0);
    aq_destroy(&q);
}

static void test_concurrent_no_loss(void)
{
    app_queue q;
    pthread_t th[PRODUCERS];
    producer_arg args[PRODUCERS];
    uint64_t next[PRODUCERS] = {0};
    int total = 0;
    int bad = 0;

    CHECK_EQ_INT(aq_init(&q), 0);
    for (int p = 0; p < PRODUCERS; p++) {
        args[p] = (producer_arg){.q = &q, .prod = p};
        CHECK_EQ_INT(pthread_create(&th[p], NULL, producer, &args[p]), 0);
    }
    while (total < PRODUCERS * PER_PROD) {
        app_cmd *list = aq_take_all(&q);
        if (list == NULL) {
            sched_yield();
            continue;
        }
        for (app_cmd *c = list; c != NULL;) {
            app_cmd *n = c->next;
            uint64_t p = c->id >> 32;
            uint64_t s = c->id & 0xffffffffu;
            if (p >= PRODUCERS || s != next[p]) {
                bad++;
            } else {
                next[p]++;
            }
            total++;
            free(c);
            c = n;
        }
    }
    for (int p = 0; p < PRODUCERS; p++) {
        pthread_join(th[p], NULL);
    }
    CHECK_EQ_INT(bad, 0);
    CHECK_EQ_INT(total, PRODUCERS * PER_PROD);
    CHECK(aq_take_all(&q) == NULL);
    aq_destroy(&q);
}

int main(void)
{
    RUN(test_fifo);
    RUN(test_full_keeps_ownership);
    RUN(test_destroy_frees_pending);
    RUN(test_concurrent_no_loss);
    return test_summary();
}
