#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>

/**
 * @brief Initialize transport layer
 * - UDP socket for telemetry (TX to 192.168.10.10:6000)
 * - TCP server for commands (listen on port 6001)
 */
void transport_init(void);

/**
 * @brief Send telemetry packet via UDP
 * @param buf  Frame buffer (already packed by frame_codec)
 * @param len  Buffer length
 * @return 1 on success, 0 on failure
 */
uint8_t transport_send_telemetry(const uint8_t *buf, uint8_t len);

/**
 * @brief Send event packet via TCP to the connected NXP client
 * @param buf  Frame buffer (already packed by frame_codec)
 * @param len  Buffer length
 * @return 1 on success, 0 on failure
 */
uint8_t transport_send_event(const uint8_t *buf, uint8_t len);

/**
 * @brief Placeholder for periodic transport tasks
 */
void transport_poll(void);

#endif /* TRANSPORT_H */
