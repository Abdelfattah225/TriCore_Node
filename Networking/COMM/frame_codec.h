#ifndef FRAME_CODEC_H
#define FRAME_CODEC_H

#include <stdint.h>
#include <stddef.h>

/* Message format with CRC-16:
 *   [CMD_TYPE 1B][SEQ 1B][LEN 1B][PAYLOAD NB][CRC16_LO 1B][CRC16_HI 1B]
 * CRC-16/CCITT (poly 0x1021, init 0xFFFF) covers CMD_TYPE + PAYLOAD.
 */
#define FRAME_HEADER_SIZE  3   /* CMD_TYPE + SEQ + LEN            */
#define FRAME_CRC_SIZE     2   /* CRC16 little-endian             */
#define FRAME_OVERHEAD     (FRAME_HEADER_SIZE + FRAME_CRC_SIZE)  /* 5 */

/* Command types (shared contract with NXP) */
#define CMD_SET_HEATER      0x01
#define CMD_SET_SEAT        0x02
#define CMD_SET_TRUNK       0x03
#define CMD_SET_AMBIENT_LED 0x04
#define CMD_REQUEST_SENSORS 0x10
#define EVT_SENSOR_DATA     0x80
#define EVT_FAULT_EVENT     0x81
#define EVT_CMD_REJECTED    0x82

/**
 * @brief Pack a message + CRC into a buffer
 * @param cmd_type  Command/event type byte
 * @param seq       Sequence number
 * @param payload   Pointer to payload data (can be NULL if len=0)
 * @param len       Payload length (0-250)
 * @param out_buf   Output buffer (must be at least FRAME_OVERHEAD + len)
 * @return Total packed size (header + payload + CRC), or 0 on error
 */
uint8_t frame_codec_pack(uint8_t cmd_type, uint8_t seq,
                         const uint8_t *payload, uint8_t len,
                         uint8_t *out_buf);

/* Return codes for frame_codec_unpack */
#define FRAME_UNPACK_INCOMPLETE  0   /* not enough data yet   */
#define FRAME_UNPACK_OK          1   /* valid frame, CRC OK   */
#define FRAME_UNPACK_CRC_ERROR   2   /* frame complete, CRC bad*/

/**
 * @brief Unpack a message from a buffer and verify CRC
 * @param buf           Input buffer
 * @param buf_len       Total buffer length
 * @param out_cmd_type  Output: command type
 * @param out_seq       Output: sequence number
 * @param out_payload   Output: pointer to payload (points into buf)
 * @param out_len       Output: payload length
 * @return FRAME_UNPACK_OK / FRAME_UNPACK_INCOMPLETE / FRAME_UNPACK_CRC_ERROR
 */
uint8_t frame_codec_unpack(const uint8_t *buf, uint16_t buf_len,
                           uint8_t *out_cmd_type, uint8_t *out_seq,
                           const uint8_t **out_payload, uint8_t *out_len);

/**
 * @brief Calculate CRC-16/CCITT (poly 0x1021, init 0xFFFF)
 * @param data  Data to checksum
 * @param len   Data length
 * @return CRC-16 value
 */
uint16_t frame_codec_crc16(const uint8_t *data, uint16_t len);

#endif /* FRAME_CODEC_H */
