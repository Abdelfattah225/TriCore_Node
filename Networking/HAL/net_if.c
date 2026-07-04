#include "net_if.h"
#include "transport.h"

void net_if_init(eth_addr_t mac)
{
    Ifx_Lwip_init(mac);
    transport_init();
}

void net_if_poll(void)
{
    Ifx_Lwip_pollTimerFlags();    /* Drive lwIP timers (TCP retransmit, ACK, etc.) */
    Ifx_Lwip_pollReceiveFlags();  /* CHECK GETH HARDWARE FOR INCOMING PACKETS */
    transport_poll();
}
