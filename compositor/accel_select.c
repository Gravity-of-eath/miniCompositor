/*
 * Pick the best 2D backend at startup.
 *
 * Rules:
 *   1. If env MC_ACCEL is set to a specific name, try that one only.
 *   2. Otherwise ("auto" / unset): try HW backends in priority order
 *      (G2D first, then RGA), fall back to CPU on any init failure.
 *   3. Every backend's init() must self-test enough to fail-fast when
 *      the device is missing or the ABI mismatches. If init() returns
 *      non-zero, we move on to the next candidate.
 */
#define _GNU_SOURCE
#include "accel.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

static const struct mc_accel_ops *try_one(const struct mc_accel_ops *ops)
{
    if (!ops || !ops->init) return NULL;
    if (ops->init() == 0) {
        LOG_I("accel: using '%s'", ops->name);
        return ops;
    }
    LOG_I("accel: backend '%s' init failed, trying next", ops->name);
    return NULL;
}

const struct mc_accel_ops *mc_accel_select(void)
{
    const char *want = getenv("MC_ACCEL");
    if (!want || !*want) want = "auto";

    /* Explicit selection: try only that one, no fallback. Returning NULL
     * propagates as a hard error so the user notices a typo or a missing
     * device. */
    if (strcmp(want, "cpu") == 0) {
        return try_one(&mc_accel_cpu);
    }
#ifdef MC_ENABLE_G2D
    if (strcmp(want, "g2d") == 0) {
        const struct mc_accel_ops *o = try_one(&mc_accel_g2d);
        if (o) return o;
        LOG_W("accel: MC_ACCEL=g2d requested but unavailable");
        return NULL;
    }
#endif
#ifdef MC_ENABLE_RGA
    if (strcmp(want, "rga") == 0) {
        const struct mc_accel_ops *o = try_one(&mc_accel_rga);
        if (o) return o;
        LOG_W("accel: MC_ACCEL=rga requested but unavailable");
        return NULL;
    }
#endif

    /* Auto: hw first, then cpu. */
#ifdef MC_ENABLE_G2D
    {
        const struct mc_accel_ops *o = try_one(&mc_accel_g2d);
        if (o) return o;
    }
#endif
#ifdef MC_ENABLE_RGA
    {
        const struct mc_accel_ops *o = try_one(&mc_accel_rga);
        if (o) return o;
    }
#endif
    return try_one(&mc_accel_cpu);
}
