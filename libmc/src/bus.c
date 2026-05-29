#define _GNU_SOURCE
#include "internal.h"

#include <string.h>

uint32_t mc_internal_next_serial(mc_ctx_t *c);

static int send_topic_only(mc_ctx_t *c, uint16_t type, const char *topic)
{
    if (!c || !c->alive || !topic || !*topic) return MC_E_INVAL;
    uint8_t buf[128];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_str(&b, MC_T_BUS_TOPIC, topic);
    size_t flen = mc_builder_finalize(&b, type, mc_internal_next_serial(c));
    if (mc_send_frame(c->sock, buf, flen, NULL, 0) < 0) {
        mc_internal_mark_dead(c, "send during BUS_SUB/UNSUB");
        return MC_E_PROTO;
    }
    return MC_OK;
}

int mc_bus_subscribe(mc_ctx_t *c, const char *topic)
{
    return send_topic_only(c, MC_CL_BUS_SUB, topic);
}

int mc_bus_unsubscribe(mc_ctx_t *c, const char *topic)
{
    return send_topic_only(c, MC_CL_BUS_UNSUB, topic);
}

int mc_bus_publish(mc_ctx_t *c, const char *topic,
                   const void *payload, uint32_t len)
{
    if (!c || !c->alive) return MC_E_PROTO;
    if (!topic || !*topic) return MC_E_INVAL;
    /* Hard cap to match the compositor's MC_BUS_MAX_PAYLOAD. */
    if (len > 4096) return MC_E_TOOLARGE;

    uint8_t buf[MC_HDR_BYTES + MC_MAX_PAYLOAD];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_str(&b, MC_T_BUS_TOPIC, topic);
    if (len > 0 && payload) {
        mc_put_bin(&b, MC_T_BUS_PAYLOAD, payload, (size_t)len);
    }
    size_t flen = mc_builder_finalize(&b, MC_CL_BUS_PUB,
                                      mc_internal_next_serial(c));
    if (mc_send_frame(c->sock, buf, flen, NULL, 0) < 0) {
        mc_internal_mark_dead(c, "send during BUS_PUB");
        return MC_E_PROTO;
    }
    return MC_OK;
}
