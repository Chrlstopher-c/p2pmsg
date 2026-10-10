/* net/discovery — publication et browse mDNS `_p2pmsg._tcp` via avahi-client, sur AvahiPoll de core/loop.
 * Pas de thread. Client cree en AVAHI_CLIENT_NO_FAIL : le daemon absent n'est pas fatal. En FAILURE, le client
 * est detruit depuis un timer (jamais depuis un callback avahi) puis recree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/strlst.h>
#include "core/log/log.h"
#include "net/discovery/disc_internal.h"

#define MDNS_RETRY_MS 5000u
#define MDNS_LABEL_MAX 63u

struct disc_mdns {
    disc_t              *owner;
    AvahiPoll            poll;
    AvahiClient         *client;
    AvahiEntryGroup     *group;
    AvahiServiceBrowser *browser;
    uint64_t             retry_timer;
    char                 publish_name[MDNS_LABEL_MAX + 1];
};

static void client_connect(disc_mdns_t *m);
static void client_cb(AvahiClient *c, AvahiClientState state, void *ud);
static void browse_cb(AvahiServiceBrowser *b, AvahiIfIndex iface, AvahiProtocol proto, AvahiBrowserEvent ev,
                      const char *name, const char *type, const char *domain, AvahiLookupResultFlags flags,
                      void *ud);

/* Tronque a la taille de dst - 1 (label DNS 63 octets max) sans avertissement de troncature. */
static void copy_label(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);

    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void hex_encode(const uint8_t *src, char *dst)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < 32; i++) {
        dst[2 * i] = digits[src[i] >> 4];
        dst[2 * i + 1] = digits[src[i] & 0x0fu];
    }
    dst[64] = '\0';
}

static AvahiStringList *build_txt(const disc_t *d)
{
    char pk_kv[3 + 64 + 1];
    char name_kv[5 + DISC_NAME_MAX + 1];

    memcpy(pk_kv, "pk=", 3);
    hex_encode(d->pk, pk_kv + 3);
    snprintf(name_kv, sizeof name_kv, "name=%s", d->name);
    return avahi_string_list_new(name_kv, pk_kv, "v=" DISC_TXT_VERSION, NULL);
}

static void add_service(disc_mdns_t *m)
{
    AvahiStringList *txt = build_txt(m->owner);
    int rc;

    if (!txt) {
        LOG_E(DISC_LOG, "mdns: TXT allocation impossible");
        return;
    }
    rc = avahi_entry_group_add_service_strlst(m->group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0,
                                              m->publish_name, DISC_SVC_TYPE, NULL, NULL,
                                              m->owner->port, txt);
    avahi_string_list_free(txt);
    if (rc < 0) {
        LOG_W(DISC_LOG, "mdns: add_service %s: %s", m->publish_name, avahi_strerror(rc));
        return;
    }
    rc = avahi_entry_group_commit(m->group);
    if (rc < 0) {
        LOG_W(DISC_LOG, "mdns: commit %s: %s", m->publish_name, avahi_strerror(rc));
    }
}

static void rename_service(disc_mdns_t *m)
{
    char *alt = avahi_alternative_service_name(m->publish_name);

    if (!alt) {
        LOG_E(DISC_LOG, "mdns: nom alternatif impossible");
        return;
    }
    copy_label(m->publish_name, sizeof m->publish_name, alt);
    avahi_free(alt);
    LOG_W(DISC_LOG, "mdns: collision de nom, publie sous %s", m->publish_name);
    avahi_entry_group_reset(m->group);
    add_service(m);
}

static void group_cb(AvahiEntryGroup *g, AvahiEntryGroupState state, void *ud)
{
    disc_mdns_t *m = ud;

    (void)g;
    switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
        LOG_I(DISC_LOG, "mdns: service publie name=%s", m->owner->name);
        break;
    case AVAHI_ENTRY_GROUP_COLLISION:
        rename_service(m);
        break;
    case AVAHI_ENTRY_GROUP_FAILURE:
        LOG_W(DISC_LOG, "mdns: publication refusee par avahi");
        break;
    default:
        break;
    }
}

static void publish(disc_mdns_t *m)
{
    m->group = avahi_entry_group_new(m->client, group_cb, m);
    if (!m->group) {
        LOG_W(DISC_LOG, "mdns: entry group impossible: %s",
              avahi_strerror(avahi_client_errno(m->client)));
        return;
    }
    add_service(m);
}

static void start_browser(disc_mdns_t *m)
{
    m->browser = avahi_service_browser_new(m->client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
                                           DISC_SVC_TYPE, NULL, 0, browse_cb, m);
    if (!m->browser) {
        LOG_W(DISC_LOG, "mdns: browser impossible: %s", avahi_strerror(avahi_client_errno(m->client)));
    }
}

static int txt_copy(AvahiStringList *txt, const char *key, char *dst, size_t cap)
{
    AvahiStringList *node = avahi_string_list_find(txt, key);
    char *k = NULL;
    char *v = NULL;
    size_t sz = 0;
    int rc = -1;

    if (!node || avahi_string_list_get_pair(node, &k, &v, &sz) < 0) {
        avahi_free(k);
        avahi_free(v);
        return -1;
    }
    if (v) {
        snprintf(dst, cap, "%s", v);
        rc = 0;
    }
    avahi_free(k);
    avahi_free(v);
    return rc;
}

static int build_peer(const char *svc, const AvahiAddress *addr, uint16_t port, AvahiStringList *txt,
                      disc_peer *p)
{
    char pk_txt[64 + 1];
    char ver[8];

    memset(p, 0, sizeof *p);
    if (txt_copy(txt, "v", ver, sizeof ver) != 0 || strcmp(ver, DISC_TXT_VERSION) != 0) {
        LOG_W(DISC_LOG, "mdns: service %s ignored, TXT v absent ou version inconnue", svc);
        return -1;
    }
    if (txt_copy(txt, "pk", pk_txt, sizeof pk_txt) != 0
        || disc_parse_hex_key(pk_txt, strlen(pk_txt), p->pubkey) != 0) {
        LOG_W(DISC_LOG, "mdns: service %s ignored, pk invalide", svc);
        return -1;
    }
    if (txt_copy(txt, "name", p->name, sizeof p->name) != 0) {
        snprintf(p->name, sizeof p->name, "%s", svc);
    }
    if (!addr || !avahi_address_snprint(p->host, sizeof p->host, addr)) {
        return -1;
    }
    p->port = port;
    p->source = DISC_MDNS;
    return 0;
}

static void resolve_cb(AvahiServiceResolver *r, AvahiIfIndex iface, AvahiProtocol proto,
                       AvahiResolverEvent ev, const char *name, const char *type, const char *domain,
                       const char *host, const AvahiAddress *addr, uint16_t port, AvahiStringList *txt,
                       AvahiLookupResultFlags flags, void *ud)
{
    disc_mdns_t *m = ud;
    disc_peer p;

    (void)iface;
    (void)proto;
    (void)type;
    (void)domain;
    (void)host;
    (void)flags;
    if (ev == AVAHI_RESOLVER_FOUND && build_peer(name, addr, port, txt, &p) == 0) {
        disc_peers_seen(m->owner, &p, name);
    }
    avahi_service_resolver_free(r);
}

static void start_resolve(disc_mdns_t *m, AvahiIfIndex iface, AvahiProtocol proto, const char *name,
                          const char *type, const char *domain)
{
    if (!avahi_service_resolver_new(m->client, iface, proto, name, type, domain, AVAHI_PROTO_UNSPEC, 0,
                                    resolve_cb, m)) {
        LOG_W(DISC_LOG, "mdns: resolve impossible pour %s", name);
    }
}

static void browse_cb(AvahiServiceBrowser *b, AvahiIfIndex iface, AvahiProtocol proto, AvahiBrowserEvent ev,
                      const char *name, const char *type, const char *domain, AvahiLookupResultFlags flags,
                      void *ud)
{
    disc_mdns_t *m = ud;

    (void)b;
    (void)flags;
    switch (ev) {
    case AVAHI_BROWSER_NEW:
        start_resolve(m, iface, proto, name, type, domain);
        break;
    case AVAHI_BROWSER_REMOVE:
        disc_peers_lost(m->owner, name);
        break;
    case AVAHI_BROWSER_FAILURE:
        LOG_W(DISC_LOG, "mdns: browser en echec");
        break;
    default:
        break;
    }
}

static void schedule_retry(disc_mdns_t *m);

static void reset_client(disc_mdns_t *m)
{
    if (m->client) {
        avahi_client_free(m->client);
        m->client = NULL;
    }
    m->group = NULL;
    m->browser = NULL;
}

static void retry_cb(loop_t *l, uint64_t id, void *ud)
{
    disc_mdns_t *m = ud;

    (void)l;
    (void)id;
    m->retry_timer = 0;
    reset_client(m);
    client_connect(m);
}

static void schedule_retry(disc_mdns_t *m)
{
    if (m->retry_timer != 0) {
        return;
    }
    m->retry_timer = loop_timer_add(m->owner->loop, MDNS_RETRY_MS, 0, retry_cb, m);
    if (m->retry_timer == 0) {
        LOG_E(DISC_LOG, "mdns: retry timer impossible, avahi abandonne");
    }
}

static void client_cb(AvahiClient *c, AvahiClientState state, void *ud)
{
    disc_mdns_t *m = ud;

    m->client = c;
    switch (state) {
    case AVAHI_CLIENT_S_RUNNING:
        LOG_I(DISC_LOG, "mdns: avahi-daemon joignable");
        if (!m->group) {
            publish(m);
        }
        if (!m->browser) {
            start_browser(m);
        }
        break;
    case AVAHI_CLIENT_S_COLLISION:
        LOG_W(DISC_LOG, "mdns: collision du nom d'hote avahi");
        break;
    case AVAHI_CLIENT_CONNECTING:
        LOG_W(DISC_LOG, "mdns: avahi-daemon indisponible, statique seul (reconnexion automatique)");
        break;
    case AVAHI_CLIENT_FAILURE:
        LOG_W(DISC_LOG, "mdns: client avahi en echec (%s), nouvel essai",
              avahi_strerror(avahi_client_errno(c)));
        m->group = NULL;
        m->browser = NULL;
        schedule_retry(m);
        break;
    default:
        break;
    }
}

static void client_connect(disc_mdns_t *m)
{
    int err = 0;

    m->client = avahi_client_new(&m->poll, AVAHI_CLIENT_NO_FAIL, client_cb, m, &err);
    if (!m->client) {
        LOG_W(DISC_LOG, "mdns: avahi_client_new: %s, nouvel essai dans %u ms", avahi_strerror(err),
              MDNS_RETRY_MS);
        schedule_retry(m);
    }
}

disc_mdns_t *disc_mdns_new(disc_t *d)
{
    disc_mdns_t *m = calloc(1, sizeof *m);

    if (!m) {
        LOG_E(DISC_LOG, "mdns: allocation impossible");
        return NULL;
    }
    m->owner = d;
    disc_poll_init(&m->poll, d->loop);
    copy_label(m->publish_name, sizeof m->publish_name, d->name);
    client_connect(m);
    return m;
}

void disc_mdns_free(disc_mdns_t *m)
{
    if (!m) {
        return;
    }
    if (m->retry_timer != 0) {
        loop_timer_cancel(m->owner->loop, m->retry_timer);
    }
    if (m->client) {
        avahi_client_free(m->client);
    }
    free(m);
}
