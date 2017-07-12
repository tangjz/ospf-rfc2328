#ifndef _TRANSIT_H
#define _TRANSIT_H

#include "global.h"
#include "packet.h"

// transit for socket
interface *recv_ospf(int sock, uint8_t buf[], int size, in_addr_t *src);
void send_ospf(const interface *iface, struct iphdr *iph, in_addr_t dst);

// transit for message
void *recv_loop(void *p);
void *send_loop(void *p);

#endif /* _TRANSIT_H */