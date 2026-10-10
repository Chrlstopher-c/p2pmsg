/* core/loop — event loop epoll mono-thread, timers monotoniques, réveil inter-thread. */
#ifndef P2P_CORE_LOOP_H
#define P2P_CORE_LOOP_H
#include <stdint.h>

typedef struct loop loop_t;

#define LOOP_READ  0x1u
#define LOOP_WRITE 0x2u
#define LOOP_ERROR 0x4u /* EPOLLERR|EPOLLHUP|EPOLLRDHUP, toujours rapporté */

typedef void (*loop_io_cb)(loop_t *l, int fd, uint32_t events, void *ud);
typedef void (*loop_timer_cb)(loop_t *l, uint64_t timer_id, void *ud);
typedef void (*loop_wake_cb)(loop_t *l, void *ud);

loop_t *loop_new(void);   /* NULL si échec */
void    loop_free(loop_t *l);

/* fd non possédé par la boucle (jamais fermé par elle). 0 ou -errno. */
int loop_add_fd(loop_t *l, int fd, uint32_t events, loop_io_cb cb, void *ud);
int loop_mod_fd(loop_t *l, int fd, uint32_t events);
int loop_del_fd(loop_t *l, int fd); /* sûr à appeler depuis un callback, y compris celui du fd */

/* Timer en ms (horloge monotone). repeat=0 → coup unique. Retourne id > 0, ou 0 si échec. */
uint64_t loop_timer_add(loop_t *l, uint64_t delay_ms, uint64_t repeat_ms, loop_timer_cb cb, void *ud);
void     loop_timer_cancel(loop_t *l, uint64_t id); /* id inconnu/expiré : sans effet ; sûr en callback */

/* Thread-safe : seule fonction appelable depuis un autre thread. cb exécuté dans le thread de la boucle.
 * Plusieurs appels avant traitement peuvent être fusionnés en un seul cb. 0 ou -errno. */
int loop_set_wake(loop_t *l, loop_wake_cb cb, void *ud);
int loop_wake(loop_t *l);

uint64_t loop_now_ms(const loop_t *l); /* temps monotone mis en cache à chaque itération */
int  loop_run(loop_t *l);              /* jusqu'à loop_stop ; 0 ou -errno */
int  loop_run_once(loop_t *l, int timeout_ms); /* une itération, pour tests ; nb d'évènements ou -errno */
void loop_stop(loop_t *l);             /* depuis le thread de la boucle */

#endif
