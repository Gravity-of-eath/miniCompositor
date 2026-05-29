/*
 * Bus broker: topic-based pub/sub between mc clients.
 *
 * Topic matching rules:
 *   '*'          matches any topic
 *   'prefix/' + '*'  matches anything starting with 'prefix/'
 *   'exact'      literal match
 *
 * Out of scope (intentional):
 *   - multi-level globbing
 *   - QoS / acks / retries
 *   - retained / last-value messages
 *   - cross-host routing
 */
#ifndef MC_BUS_H
#define MC_BUS_H

#include <stddef.h>
#include <stdint.h>

#define MC_BUS_MAX_TOPIC_LEN     64
#define MC_BUS_MAX_SUBS_PER_CLI  16
#define MC_BUS_MAX_PAYLOAD       4096

struct mc_client;
struct mc_server;

/* Per-client subscription list. Embedded in struct mc_client. */
struct mc_bus_subs {
    char topics[MC_BUS_MAX_SUBS_PER_CLI][MC_BUS_MAX_TOPIC_LEN];
    int  n;
};

/* Add `topic` to the client's subscription list. Duplicates are silently
 * deduped. Returns 0 on success, negative MC_E_* on failure (full / invalid). */
int  mc_bus_sub_add(struct mc_client *c, const char *topic);

/* Remove `topic`. Missing topic is not an error. */
int  mc_bus_sub_remove(struct mc_client *c, const char *topic);

/* Forwards a published message: for each peer client whose subscriptions
 * match `topic`, send SV_BUS_MSG. `from` is not included (publisher doesn't
 * receive its own messages). */
void mc_bus_publish(struct mc_server *s, struct mc_client *from,
                    const char *topic,
                    const void *payload, size_t plen);

#endif
