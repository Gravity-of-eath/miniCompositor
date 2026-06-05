/* mc_toast() client API — encode and publish a ui/toast bus message. */
#include "mc.h"
#include "mc_toast_wire.h"
#include "internal.h"   /* MC_E_* */

int mc_toast(mc_ctx_t *ctx, const char *text, int duration_ms, mc_toast_pos_t pos)
{
    if (!ctx || !text) return MC_E_INVAL;
    uint32_t dur = (duration_ms <= 0) ? MC_TOAST_DEFAULT_MS : (uint32_t)duration_ms;
    uint8_t p = (pos < MC_TOAST_POS_BOTTOM || pos > MC_TOAST_POS_TOP)
              ? MC_TOAST_POS_BOTTOM : (uint8_t)pos;

    uint8_t buf[MC_TOAST_HDR_BYTES + MC_TOAST_MAX_TEXT];
    int n = mc_toast_wire_encode(buf, sizeof(buf), text, dur, p);
    if (n < 0) return MC_E_INVAL;   /* unreachable: ctx/text already checked */
    return mc_bus_publish(ctx, "ui/toast", buf, (uint32_t)n);
}
