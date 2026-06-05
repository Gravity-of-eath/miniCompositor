/* Host unit test for common/mc_toast_wire.h. Build & run:
 *   cc -Icommon -o build/mc-toast-wire-test tools/mc-toast-wire-test.c && build/mc-toast-wire-test
 */
#include "mc_toast_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t buf[MC_TOAST_HDR_BYTES + MC_TOAST_MAX_TEXT];

    /* round-trip ASCII */
    int n = mc_toast_wire_encode(buf, sizeof(buf), "hello", 1500, 2);
    assert(n == MC_TOAST_HDR_BYTES + 5);
    uint32_t dur = 0; uint8_t pos = 0; char txt[64] = {0};
    assert(mc_toast_wire_decode(buf, (size_t)n, &dur, &pos, txt, sizeof(txt)) == 0);
    assert(dur == 1500 && pos == 2 && strcmp(txt, "hello") == 0);

    /* bad magic rejected */
    buf[0] = 0; assert(mc_toast_wire_decode(buf, (size_t)n, &dur, &pos, txt, sizeof(txt)) == -1);

    /* short payload rejected — re-encode first so rejection is due to length */
    n = mc_toast_wire_encode(buf, sizeof(buf), "hello", 1500, 2);
    assert(mc_toast_wire_decode(buf, 4, &dur, &pos, txt, sizeof(txt)) == -1);

    /* UTF-8 truncation never splits a codepoint: "中" = E4 B8 AD (3 bytes).
     * Encode into a buffer whose text room is 2 -> must drop the whole char. */
    uint8_t small[MC_TOAST_HDR_BYTES + 2];
    int m = mc_toast_wire_encode(small, sizeof(small), "\xE4\xB8\xAD", 1000, 0);
    assert(m == MC_TOAST_HDR_BYTES + 0);   /* 3-byte char dropped, not split */

    /* bad VERSION byte rejected */
    n = mc_toast_wire_encode(buf, sizeof(buf), "hello", 1500, 2);
    buf[2] = 99;
    assert(mc_toast_wire_decode(buf, (size_t)n, &dur, &pos, txt, sizeof(txt)) == -1);

    /* decode with NULL optional out-pointers works */
    n = mc_toast_wire_encode(buf, sizeof(buf), "hello", 1500, 2);
    assert(mc_toast_wire_decode(buf, (size_t)n, NULL, NULL, NULL, 0) == 0);

    /* encode with outcap < header rejected */
    uint8_t tiny[4];
    assert(mc_toast_wire_encode(tiny, sizeof(tiny), "x", 100, 0) == -1);

    /* decode-side UTF-8 truncation never splits: two 3-byte chars "中中".
     * Decode into tcap[5] (text_cap=5, room=4 bytes + NUL).
     * floor(4) on "E4 B8 AD E4 B8 AD" inspects byte[4]=0xB8 (continuation),
     * backs off to byte[3]=0xE4 (lead) -> tlen=3, one full char only. */
    n = mc_toast_wire_encode(buf, sizeof(buf), "\xE4\xB8\xAD\xE4\xB8\xAD", 1000, 0);
    assert(n == MC_TOAST_HDR_BYTES + 6);
    char tcap[5] = {0};
    assert(mc_toast_wire_decode(buf, (size_t)n, NULL, NULL, tcap, sizeof(tcap)) == 0);
    assert(strlen(tcap) == 3);

    printf("mc-toast-wire-test: OK\n");
    return 0;
}
