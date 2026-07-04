#ifndef DISPATCHER_H
#define DISPATCHER_H

#include <stdint.h>

/**
 * @brief Handler signature that teammates implement.
 * @param cmd_type  The command type byte (e.g. CMD_SET_HEATER)
 * @param seq       Sequence number from the frame
 * @param payload   Pointer to payload data (NULL if len=0)
 * @param len       Payload length
 */
typedef void (*dispatcher_handler_t)(uint8_t cmd_type, uint8_t seq,
                                     const uint8_t *payload, uint8_t len);

/**
 * @brief Clear the handler table.
 */
void dispatcher_init(void);

/**
 * @brief Register a handler for a specific CMD_TYPE.
 * @return 1 on success, 0 if table full.
 */
uint8_t dispatcher_register(uint8_t cmd_type, dispatcher_handler_t handler);

/**
 * @brief Called by transport when a complete frame arrives.
 * Routes to the registered handler.
 */
void dispatcher_dispatch(uint8_t cmd_type, uint8_t seq,
                         const uint8_t *payload, uint8_t len);

#endif /* DISPATCHER_H */
