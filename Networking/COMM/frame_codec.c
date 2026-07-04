#include "frame_codec.h"
#include <stdlib.h>
#include <string.h>

/**
 * CRC-16/CCITT-FALSE
 * Polynomial: 0x1021
 * Initial value: 0xFFFF
 * Input/Output: no reflection, no final XOR
 */
uint16_t frame_codec_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

uint8_t frame_codec_pack(uint8_t cmd_type, uint8_t seq,
                         const uint8_t *payload, uint8_t len,
                         uint8_t *out_buf)
{
    if (out_buf == NULL) {
        return 0;
    }
    /* Leave room for CRC: max payload = 255 - FRAME_OVERHEAD = 250 */
    if (len > 250) {
        return 0; /* payload too large */
    }

    /* Build header */
    out_buf[0] = cmd_type;
    out_buf[1] = seq;
    out_buf[2] = len;

    /* Copy payload */
    if (len > 0 && payload != NULL) {
        memcpy(&out_buf[FRAME_HEADER_SIZE], payload, len);
    }

    /* Calculate CRC over CMD_TYPE + PAYLOAD */
    uint8_t crc_input[1 + 250];
    crc_input[0] = cmd_type;
    if (len > 0 && payload != NULL) {
        memcpy(&crc_input[1], payload, len);
    }
    uint16_t crc = frame_codec_crc16(crc_input, 1 + len);

    /* Append CRC little-endian */
    out_buf[FRAME_HEADER_SIZE + len]     = (uint8_t)(crc & 0xFF);        /* CRC_LO */
    out_buf[FRAME_HEADER_SIZE + len + 1] = (uint8_t)((crc >> 8) & 0xFF); /* CRC_HI */

    return FRAME_OVERHEAD + len;
}

uint8_t frame_codec_unpack(const uint8_t *buf, uint16_t buf_len,
                           uint8_t *out_cmd_type, uint8_t *out_seq,
                           const uint8_t **out_payload, uint8_t *out_len)
{
    if (buf == NULL || buf_len < FRAME_HEADER_SIZE) {
        return FRAME_UNPACK_INCOMPLETE; /* too short for header */
    }

    *out_cmd_type = buf[0];
    *out_seq      = buf[1];
    *out_len      = buf[2];

    uint16_t frame_total = FRAME_HEADER_SIZE + *out_len + FRAME_CRC_SIZE;
    if (buf_len < frame_total) {
        return FRAME_UNPACK_INCOMPLETE; /* not enough data yet */
    }

    if (*out_len > 0) {
        *out_payload = &buf[FRAME_HEADER_SIZE];
    } else {
        *out_payload = NULL;
    }

    /* Verify CRC over CMD_TYPE + PAYLOAD */
    uint8_t crc_input[1 + 250];
    crc_input[0] = buf[0]; /* cmd_type */
    if (*out_len > 0) {
        memcpy(&crc_input[1], *out_payload, *out_len);
    }
    uint16_t calc_crc = frame_codec_crc16(crc_input, 1 + *out_len);

    /* Read received CRC (little-endian) */
    uint16_t recv_crc = (uint16_t)buf[FRAME_HEADER_SIZE + *out_len]
                      | ((uint16_t)buf[FRAME_HEADER_SIZE + *out_len + 1] << 8);

    if (calc_crc != recv_crc) {
        return FRAME_UNPACK_CRC_ERROR;
    }

    return FRAME_UNPACK_OK;
}
