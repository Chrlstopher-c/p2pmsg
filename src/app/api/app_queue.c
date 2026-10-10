/* app/api : file de commandes, verrouillée par mutex. Propriété : push transfère c, take transfère la liste. */
#include <errno.h>
#include <stdlib.h>

#include "app/api/app_queue.h"

int aq_init(app_queue *q)
{
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    return pthread_mutex_init(&q->mu, NULL) == 0 ? 0 : -1;
}

void aq_free_list(app_cmd *c)
{
    while (c != NULL) {
        app_cmd *next = c->next;
        free(c->text);
        free(c);
        c = next;
    }
}

void aq_destroy(app_queue *q)
{
    pthread_mutex_lock(&q->mu);
    aq_free_list(q->head);
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    pthread_mutex_unlock(&q->mu);
    pthread_mutex_destroy(&q->mu);
}

int aq_push(app_queue *q, app_cmd *c)
{
    int rc = 0;

    pthread_mutex_lock(&q->mu);
    if (q->count >= APP_QUEUE_MAX) {
        rc = -ENOSPC;
    } else {
        c->next = NULL;
        if (q->tail != NULL) {
            q->tail->next = c;
        } else {
            q->head = c;
        }
        q->tail = c;
        q->count++;
    }
    pthread_mutex_unlock(&q->mu);
    return rc;
}

app_cmd *aq_take_all(app_queue *q)
{
    app_cmd *list;

    pthread_mutex_lock(&q->mu);
    list = q->head;
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    pthread_mutex_unlock(&q->mu);
    return list;
}
