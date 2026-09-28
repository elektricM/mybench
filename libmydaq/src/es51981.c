/* Cyrustek ES51981 serial packet (the DMM converter's output; docs/protocol.md section 9).
 *
 * 11 bytes: range, digit x4, function, status, option1, option2, CR, LF. Every byte is
 * 0x30 + value, so ':'..'?' encode 10..15. Bit assignments follow the public Cyrustek format
 * (as also used by sigrok's es519xx parser). Verified on the myDAQ: range, digits, function 0xB
 * (voltage), status bit 2 (minus) and bit 3; the others are the documented meanings. */
#include "libmydaq/mydaq.h"

#include <string.h>

#define ES_PACKET 11u

static int nibble(uint8_t c, int *v)
{
    if (c < 0x30u || c > 0x3fu)
        return 0;
    *v = c - 0x30;
    return 1;
}

int mydaq_es51981_parse(const uint8_t *p, size_t len, mydaq_es51981 *out)
{
    int v[9];
    size_t i;
    if (!p || !out || len < 9u)
        return MYDAQ_ERR_INVALID_ARG;
    memset(out, 0, sizeof *out);
    memcpy(out->raw, p, len < ES_PACKET ? len : ES_PACKET);
    for (i = 0; i < 9; ++i)
        if (!nibble(p[i], &v[i]))
            return MYDAQ_ERR_PROTOCOL;
    if (len >= ES_PACKET && (p[9] != '\r' || p[10] != '\n'))
        return MYDAQ_ERR_PROTOCOL;
    for (i = 1; i <= 4; ++i)
        if (v[i] > 9)
            return MYDAQ_ERR_PROTOCOL;   /* ':'..'?' never appear in the digits */

    out->range = v[0] & 7;
    out->digits = v[1] * 1000 + v[2] * 100 + v[3] * 10 + v[4];
    out->function = v[5];
    /* status */
    out->judge = (v[6] >> 3) & 1;
    out->negative = (v[6] >> 2) & 1;
    out->battery_low = (v[6] >> 1) & 1;
    out->overload = v[6] & 1;
    /* option 1 */
    out->hold_max = (v[7] >> 3) & 1;
    out->hold_min = (v[7] >> 2) & 1;
    out->relative = (v[7] >> 1) & 1;
    out->rmr = v[7] & 1;
    /* option 2 */
    out->dc = (v[8] >> 3) & 1;
    out->ac = (v[8] >> 2) & 1;
    out->autorange = (v[8] >> 1) & 1;
    out->vahz = v[8] & 1;
    return MYDAQ_OK;
}
