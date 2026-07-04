#ifndef NET_IF_H
#define NET_IF_H

#include "Ifx_Lwip.h"   /* for eth_addr_t */

/**
 * @brief Init lwIP + GETH + transport layer
 */
void net_if_init(eth_addr_t mac);

/**
 * @brief Poll network stack — call from main super-loop
 */
void net_if_poll(void);

#endif /* NET_IF_H */
