/* Protocol constants of the myDAQ command channel (docs/protocol.md, sections 1-3). */
#ifndef LIBMYDAQ_PROTO_H
#define LIBMYDAQ_PROTO_H

/* Endpoints */
#define MD_EP_CMD_OUT 0x03u
#define MD_EP_CMD_IN 0x81u
#define MD_EP_EVENT_IN 0x82u
#define MD_EP_AI_IN 0x84u

/* Frame header (big-endian): u16 channel, u16 total, u16 total-4, u8 type, u8 opcode */
#define MD_FRAME_HEADER 8u
#define MD_FRAME_MAX 1024u        /* largest request we build */
#define MD_RESPONSE_MAX 16384u    /* read buffer for responses (largest seen: 512 bytes) */
#define MD_EVENT_SIZE 64u         /* interrupt endpoint frames */

/* Header channel field */
#define MD_CH_CORE 0u      /* device core (identity, status bytes) */
#define MD_CH_OBJECT 1u    /* function objects (AI, AO, DIO, DMM, calibration memory, ...) */
#define MD_CH_STREAM 7u    /* stream engine */

#define MD_TYPE_EVENT 0u
#define MD_TYPE_REQUEST 1u

/* Opcodes ("methods", names are ours) */
#define MD_OP_IDENTITY 0x00u
#define MD_OP_CORE_INFO 0x02u
#define MD_OP_RESET 0x08u      /* also: memory read (05 07), port read (05 30), DMM reset */
#define MD_OP_WRITE 0x09u
#define MD_OP_COMMIT 0x0au
#define MD_OP_START 0x0bu
#define MD_OP_QUERY 0x0cu
#define MD_OP_STATUS 0x0du
#define MD_OP_STOP 0x0eu
#define MD_OP_RELEASE 0x0fu
#define MD_OP_CAPS 0x13u

/* Response kinds */
#define MD_KIND_ACK 2u
#define MD_KIND_DATA 6u

/* Objects: 2-byte id + u16 instance, as they appear in request bodies */
#define MD_OBJ_CORE 0x02fe0000u
#define MD_OBJ_CORE_STATUS 0x02fe0001u
#define MD_OBJ_STREAM4 0x02fc0004u
#define MD_OBJ_STREAM_BASE 0x02fc0000u   /* | stream number: endpoint manager */
#define MD_OBJ_PROPERTIES 0x05010001u
#define MD_OBJ_05060000 0x05060000u
#define MD_OBJ_CALMEM 0x05070000u
#define MD_OBJ_AI_FRONTEND 0x05120000u
#define MD_OBJ_AI_FIFO 0x05130002u
#define MD_OBJ_AO_BASE 0x05200000u   /* + channel */
#define MD_OBJ_DIO_PORT0 0x05300001u
#define MD_OBJ_AI_TIMING 0x05500002u
#define MD_OBJ_AI_DATAPATH 0x05510002u
#define MD_OBJ_05600000 0x05600000u
#define MD_OBJ_DMM 0x0c900000u
#define MD_OBJ_STREAM_ENGINE4 0x00020004u   /* on channel 7 */
#define MD_OBJ_STREAM_ENGINE_BASE 0x00020000u   /* | stream number: credit / read position */

/* Status the device returns for the final stream-4 release of an AI acquisition (NI gets it too) */
#define MD_STATUS_STREAM_RELEASE ((int32_t)0xfffc9033u)

/* Calibration memory */
#define MD_CALMEM_CHUNK 0x1edu

#endif
