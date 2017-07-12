#ifndef _PROCESS_H
#define _PROCESS_H

#include "packet.h"
#include "structure.h"

// neighbor state machine
extern const char *event_type[];
extern const char *state_type[];
extern const transition nsm[];

void add_event(interface *iface, neighbor *nbr, nbr_event event);

// hello packet: recv, send
void process_hello(interface *iface, neighbor *nbr, const ospf_header *ospfhdr, in_addr_t src);
void produce_hello(const interface *iface, ospf_header *ospfhdr);

// dd packet: recv, send
void process_dd(interface *iface, neighbor *nbr, const ospf_header *ospfhdr);
void produce_dd(const interface *iface, const neighbor *nbr, ospf_header *ospfhdr);

// lsr packet: recv, send
void process_lsr(neighbor *nbr, ospf_header *ospfhdr);
void produce_lsr(const neighbor *nbr, ospf_header *ospfhdr);

// lsu packets: recv, send (lsu, full)
void process_lsu(area *a, neighbor *nbr, ospf_header *ospfhdr);
void produce_lsu(const area *a, const neighbor *nbr, ospf_header *ospfhdr);
void produce_lsu_special(const lsa_header *lsa, ospf_header *ospfhdr);

// lsack packet: recv, send
void process_ack(neighbor *nbr, ospf_header *ospfhdr);
void produce_ack(const neighbor *nbr, ospf_header *ospfhdr);

#endif /* _PROCESS_H */