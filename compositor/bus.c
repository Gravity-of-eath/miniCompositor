#define _GNU_SOURCE
#include "bus.h"
#include "transport.h"
#include "log.h"
#include "proto.h"

#include <string.h>

/* ---- topic helpers ---- */

static int topic_valid(const char *t)
{
    if (!t || !*t) return 0;
    size_t l = strlen(t);
    if (l >= MC_BUS_MAX_TOPIC_LEN) return 0;
    /* No NUL inside (strlen already guarantees), no control chars. */
    for (size_t i = 0; i < l; i++) {
        unsigned char ch = (unsigned char)t[i];
        if (ch < 0x20 || ch == 0x7f) return 0;
    }
    return 1;
}

/* Match `pattern` against literal `topic`.
 *   '*'                  -> always
 *   'prefix/' + '*'      -> topic starts with 'prefix/'
 *   anything else        -> strcmp
 */
static int topic_match(const char *pattern, const char *topic)
{
    if (pattern[0] == '*' && pattern[1] == 0) return 1;
    size_t plen = strlen(pattern);
    if (plen >= 2 && pattern[plen - 2] == '/' && pattern[plen - 1] == '*') {
        return strncmp(pattern, topic, plen - 1) == 0;
    }
    return strcmp(pattern, topic) == 0;
}

/* ---- subscription management ---- */

int mc_bus_sub_add(struct mc_client *c, const char *topic)
{
    if (!c || !topic_valid(topic)) return -MC_E_INVAL;
    for (int i = 0; i < c->bus.n; i++) {
        if (strcmp(c->bus.topics[i], topic) == 0) return 0;  /* dedup */
    }
    if (c->bus.n >= MC_BUS_MAX_SUBS_PER_CLI) return -MC_E_TOOLARGE;
    snprintf(c->bus.topics[c->bus.n], MC_BUS_MAX_TOPIC_LEN, "%s", topic);
    c->bus.n++;
    LOG_D("bus: cid=%u +sub '%s' (n=%d)", c->cid, topic, c->bus.n);
    return 0;
}

int mc_bus_sub_remove(struct mc_client *c, const char *topic)
{
    if (!c || !topic) return -MC_E_INVAL;
    for (int i = 0; i < c->bus.n; i++) {
        if (strcmp(c->bus.topics[i], topic) == 0) {
            /* swap-remove */
            if (i != c->bus.n - 1) {
                memcpy(c->bus.topics[i], c->bus.topics[c->bus.n - 1],
                       MC_BUS_MAX_TOPIC_LEN);
            }
            c->bus.topics[c->bus.n - 1][0] = 0;
            c->bus.n--;
            LOG_D("bus: cid=%u -sub '%s' (n=%d)", c->cid, topic, c->bus.n);
            return 0;
        }
    }
    return 0;  /* missing is not an error */
}

/* ---- publish ---- */

static int send_bus_msg(struct mc_client *target, const char *topic,
                        const char *sender_name,
                        const void *payload, size_t plen)
{
    /* Frame size: hdr 12 + topic tlv (4+64) + sender tlv (4+64)
     * + payload tlv (4 + plen). We bound payload above. */
    uint8_t hdrbuf[MC_HDR_BYTES + MC_MAX_PAYLOAD];
    struct mc_builder b;
    mc_builder_init(&b, hdrbuf, sizeof(hdrbuf));
    mc_put_str(&b, MC_T_BUS_TOPIC,  topic);
    mc_put_str(&b, MC_T_BUS_SENDER, sender_name);
    mc_put_bin(&b, MC_T_BUS_PAYLOAD, payload, plen);
    size_t flen = mc_builder_finalize(&b, MC_SV_BUS_MSG, 0);
    if (flen == 0) return -1;
    return mc_send_frame(target->sock, hdrbuf, flen, NULL, 0);
}

void mc_bus_publish(struct mc_server *s, struct mc_client *from,
                    const char *topic,
                    const void *payload, size_t plen)
{
    if (!topic_valid(topic) || plen > MC_BUS_MAX_PAYLOAD) {
        LOG_W("bus: drop publish from cid=%u, invalid topic or oversize (%zu)",
              from ? from->cid : 0, plen);
        return;
    }
    int delivered = 0;
    for (int i = 0; i < MC_MAX_CLIENTS; i++) {
        struct mc_client *c = &s->clients[i];
        if (c->sock <= 0) continue;
        if (c == from)   continue;             /* don't echo back */
        /* match any subscription */
        for (int k = 0; k < c->bus.n; k++) {
            if (topic_match(c->bus.topics[k], topic)) {
                if (send_bus_msg(c, topic,
                                 from ? from->name : "",
                                 payload, plen) == 0) {
                    delivered++;
                }
                break;   /* one match per client is enough */
            }
        }
    }
    LOG_D("bus: publish '%s' from cid=%u plen=%zu delivered=%d",
          topic, from ? from->cid : 0, plen, delivered);
}
