/* net/discovery — cycle de vie du module : pairs statiques emis au 1er tour de boucle, mDNS optionnel.
 * Contrat : on_peer ne doit pas appeler disc_free sur le disc qui l'emet. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/log/log.h"
#include "net/discovery/disc_internal.h"

static void emit_static(loop_t *l, uint64_t id, void *ud)
{
    disc_t *d = ud;
    int i;

    (void)l;
    (void)id;
    d->first_timer = 0;
    for (i = 0; i < d->n_static; i++) {
        disc_peers_seen(d, &d->static_peers[i], NULL);
    }
}

static int config_valid(const loop_t *l, const disc_config *cfg, const disc_handlers *h)
{
    size_t n;

    if (!l || !cfg || !h || !h->on_peer || !cfg->name || !cfg->pubkey || cfg->port == 0) {
        return 0;
    }
    n = strlen(cfg->name);
    return n > 0 && n <= DISC_NAME_MAX;
}

static void load_static(disc_t *d, const char *path)
{
    int n = disc_parse_peers_file(path, d->static_peers, DISC_STATIC_MAX);

    d->n_static = n > 0 ? n : 0;
}

disc_t *disc_new(loop_t *l, const disc_config *cfg, const disc_handlers *h, void *ud)
{
    disc_t *d;

    if (!config_valid(l, cfg, h)) {
        errno = EINVAL;
        return NULL;
    }
    d = calloc(1, sizeof *d);
    if (!d) {
        LOG_E(DISC_LOG, "allocation impossible");
        return NULL;
    }
    d->loop = l;
    d->h = *h;
    d->ud = ud;
    memcpy(d->pk, cfg->pubkey, sizeof d->pk);
    snprintf(d->name, sizeof d->name, "%s", cfg->name);
    d->port = cfg->port;
    if (cfg->peers_file) {
        load_static(d, cfg->peers_file);
    }
    d->first_timer = loop_timer_add(l, 0, 0, emit_static, d);
    if (d->first_timer == 0) {
        LOG_E(DISC_LOG, "loop_timer_add impossible, module abandonne");
        free(d);
        return NULL;
    }
    if (cfg->enable_mdns) {
        d->mdns = disc_mdns_new(d);
        if (!d->mdns) {
            LOG_W(DISC_LOG, "mdns desactive, statique seul");
        }
    }
    return d;
}

void disc_free(disc_t *d)
{
    if (!d) {
        return;
    }
    if (d->first_timer != 0) {
        loop_timer_cancel(d->loop, d->first_timer);
    }
    disc_mdns_free(d->mdns);
    free(d);
}
