#include "dispatcher.h"
#include <stdlib.h>

#define MAX_HANDLERS 16

static struct {
    uint8_t              cmd_type;
    dispatcher_handler_t handler;
} s_handlers[MAX_HANDLERS];

static uint8_t s_handler_count = 0;

void dispatcher_init(void)
{
    s_handler_count = 0;
    for (int i = 0; i < MAX_HANDLERS; i++) {
        s_handlers[i].cmd_type = 0xFF;  /* invalid / unused */
        s_handlers[i].handler  = NULL;
    }
}

uint8_t dispatcher_register(uint8_t cmd_type, dispatcher_handler_t handler)
{
    if (handler == NULL) {
        return 0;
    }
    if (s_handler_count >= MAX_HANDLERS) {
        return 0;  /* table full */
    }

    s_handlers[s_handler_count].cmd_type = cmd_type;
    s_handlers[s_handler_count].handler  = handler;
    s_handler_count++;

    return 1;
}

void dispatcher_dispatch(uint8_t cmd_type, uint8_t seq,
                         const uint8_t *payload, uint8_t len)
{
    for (uint8_t i = 0; i < s_handler_count; i++) {
        if (s_handlers[i].cmd_type == cmd_type &&
            s_handlers[i].handler != NULL) {
            s_handlers[i].handler(cmd_type, seq, payload, len);
            return;  /* handled */
        }
    }

    /* No handler found — could send EVT_CMD_REJECTED here later */
}
