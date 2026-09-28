/* Request building and response parsing for the command channel (docs/protocol.md section 2). */
#ifndef LIBMYDAQ_FRAME_H
#define LIBMYDAQ_FRAME_H

#include "proto.h"

#include <stddef.h>
#include <stdint.h>

/* A request body under construction. Writes past the end set `overflow` (checked on send). */
typedef struct md_buf {
    uint8_t data[MD_FRAME_MAX - MD_FRAME_HEADER];
    size_t len;
    int overflow;
} md_buf;

void md_buf_init(md_buf *b);
void md_put_u8(md_buf *b, uint8_t v);
void md_put_u16(md_buf *b, uint16_t v);   /* big-endian */
void md_put_u32(md_buf *b, uint32_t v);   /* big-endian */
void md_put_bytes(md_buf *b, const uint8_t *p, size_t n);
/* Appends bytes given as a hex string ("05 12 00 00" or "05120000"); for replayed constants. */
void md_put_hex(md_buf *b, const char *hex);

uint16_t md_get_u16(const uint8_t *p);    /* big-endian */
uint32_t md_get_u32(const uint8_t *p);
uint16_t md_get_u16le(const uint8_t *p);
uint32_t md_get_u32le(const uint8_t *p);

/* Encodes a request frame; returns its length (multiple of 4), 0 if it does not fit. */
size_t md_frame_encode(uint16_t channel, uint8_t opcode, const md_buf *body, uint8_t *out,
                       size_t cap);

/* A parsed response. `payload` points into the receive buffer. */
typedef struct md_resp {
    uint16_t channel;
    uint32_t kind;           /* MD_KIND_ACK or MD_KIND_DATA */
    int32_t status;          /* MD_KIND_DATA only, else 0 */
    const uint8_t *payload;  /* after the kind (ACK) or after the status (DATA) */
    size_t payload_len;
} md_resp;

/* Validates the header and splits the response. Returns MYDAQ_OK or MYDAQ_ERR_PROTOCOL. */
int md_frame_parse_response(const uint8_t *frame, size_t len, md_resp *resp);

/* Extracts a length-prefixed blob (u16 length counting itself) at `offset` in the payload. */
int md_resp_blob(const md_resp *resp, size_t offset, const uint8_t **data, size_t *len);

/* Formats bytes as hex into out (NUL-terminated, truncated with "..." if needed). */
void md_hex(const uint8_t *p, size_t n, char *out, size_t cap);

#endif
