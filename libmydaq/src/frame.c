#include "frame.h"

#include "libmydaq/mydaq.h"

#include <string.h>

void md_buf_init(md_buf *b)
{
    b->len = 0;
    b->overflow = 0;
}

void md_put_bytes(md_buf *b, const uint8_t *p, size_t n)
{
    if (b->overflow || n > sizeof b->data - b->len) {
        b->overflow = 1;
        return;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

void md_put_u8(md_buf *b, uint8_t v) { md_put_bytes(b, &v, 1); }

void md_put_u16(md_buf *b, uint16_t v)
{
    const uint8_t p[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    md_put_bytes(b, p, 2);
}

void md_put_u32(md_buf *b, uint32_t v)
{
    const uint8_t p[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    md_put_bytes(b, p, 4);
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

void md_put_hex(md_buf *b, const char *hex)
{
    int hi = -1;
    for (; *hex; ++hex) {
        const int v = hex_value(*hex);
        if (v < 0)
            continue;   /* separators */
        if (hi < 0) {
            hi = v;
        } else {
            md_put_u8(b, (uint8_t)(hi << 4 | v));
            hi = -1;
        }
    }
    if (hi >= 0)
        b->overflow = 1;   /* odd digit count: a bug in a constant */
}

uint16_t md_get_u16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint32_t md_get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
uint16_t md_get_u16le(const uint8_t *p) { return (uint16_t)(p[1] << 8 | p[0]); }
uint32_t md_get_u32le(const uint8_t *p)
{
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

size_t md_frame_encode(uint16_t channel, uint8_t opcode, const md_buf *body, uint8_t *out,
                       size_t cap)
{
    const size_t n = MD_FRAME_HEADER + body->len;
    const size_t total = (n + 3u) & ~(size_t)3u;
    if (body->overflow || total > cap || total > 0xffffu)
        return 0;
    out[0] = (uint8_t)(channel >> 8);
    out[1] = (uint8_t)channel;
    out[2] = (uint8_t)(total >> 8);
    out[3] = (uint8_t)total;
    out[4] = (uint8_t)((total - 4u) >> 8);
    out[5] = (uint8_t)(total - 4u);
    out[6] = MD_TYPE_REQUEST;
    out[7] = opcode;
    memcpy(out + MD_FRAME_HEADER, body->data, body->len);
    memset(out + n, 0, total - n);
    return total;
}

int md_frame_parse_response(const uint8_t *frame, size_t len, md_resp *resp)
{
    uint16_t total, inner;
    if (len < MD_FRAME_HEADER + 4u)
        return MYDAQ_ERR_PROTOCOL;
    total = md_get_u16(frame + 2);
    inner = md_get_u16(frame + 4);
    if (total != len || inner != total - 4u || frame[6] != MD_TYPE_REQUEST || frame[7] != 0)
        return MYDAQ_ERR_PROTOCOL;
    resp->channel = md_get_u16(frame);
    resp->kind = md_get_u32(frame + 8);
    if (resp->kind == MD_KIND_ACK) {
        resp->status = 0;
        resp->payload = frame + 12;
        resp->payload_len = len - 12u;
        return MYDAQ_OK;
    }
    if (resp->kind == MD_KIND_DATA) {
        if (len < 16u)
            return MYDAQ_ERR_PROTOCOL;
        resp->status = (int32_t)md_get_u32(frame + 12);
        resp->payload = frame + 16;
        resp->payload_len = len - 16u;
        return MYDAQ_OK;
    }
    return MYDAQ_ERR_PROTOCOL;
}

int md_resp_blob(const md_resp *resp, size_t offset, const uint8_t **data, size_t *len)
{
    size_t n;
    if (resp->payload_len < offset + 2u)
        return MYDAQ_ERR_PROTOCOL;
    n = md_get_u16(resp->payload + offset);
    if (n < 2u || offset + n > resp->payload_len)
        return MYDAQ_ERR_PROTOCOL;
    *data = resp->payload + offset + 2u;
    *len = n - 2u;
    return MYDAQ_OK;
}

void md_hex(const uint8_t *p, size_t n, char *out, size_t cap)
{
    static const char digits[] = "0123456789abcdef";
    size_t o = 0, i;
    if (cap == 0)
        return;
    for (i = 0; i < n; ++i) {
        if (o + 3u + 4u >= cap) {   /* room for "xx " and "...\0" */
            memcpy(out + o, "...", 3);
            o += 3;
            break;
        }
        out[o++] = digits[p[i] >> 4];
        out[o++] = digits[p[i] & 15u];
        if (i + 1 < n)
            out[o++] = ' ';
    }
    out[o] = '\0';
}
