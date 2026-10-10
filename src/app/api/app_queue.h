/* app/api (interne) — file de commandes thread-safe : app_send/app_stop (autre thread) → thread réseau. */
#ifndef P2P_APP_API_QUEUE_H
#define P2P_APP_API_QUEUE_H
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define APP_QUEUE_MAX 4096u
#define APP_TEXT_MAX  4096u

typedef enum { CMD_SEND, CMD_STOP } app_cmd_kind;

typedef struct app_cmd {
    app_cmd_kind    kind;
    uint8_t         peer[32];
    uint64_t        id;
    uint64_t        ts_ms;
    char           *text;
    struct app_cmd *next;
} app_cmd;

typedef struct {
    pthread_mutex_t mu;
    app_cmd        *head;
    app_cmd        *tail;
    size_t          count;
} app_queue;

int      aq_init(app_queue *q);
void     aq_destroy(app_queue *q);               /* libère les commandes restantes et le mutex */
int      aq_push(app_queue *q, app_cmd *c);      /* 0 ; -ENOSPC si pleine (c reste à l'appelant) */
app_cmd *aq_take_all(app_queue *q);              /* détache la liste entière, ordre FIFO ; NULL si vide */
void     aq_free_list(app_cmd *c);               /* libère une liste détachée (texte compris) */

#endif
