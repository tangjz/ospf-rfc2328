#ifndef _STRUCTURE_H
#define _STRUCTURE_H

#include <arpa/inet.h>

#include "packet.h"

typedef enum {
	S_Down,
	S_Attempt, // NBMA only
	S_Init,
	S_2Way,
	S_ExStart,
	S_Exchange,
	S_Loading,
	S_Full
} nbr_state;

typedef enum {
	E_HelloReceived,
	E_Start, // NBMA only
	E_1WayReceived,
	E_2WayReceived,
	E_AdjOK,
	E_NegotiationDone,
	E_ExchangeDone,
	E_LoadingDone
} nbr_event;

typedef struct {
	nbr_state src_state;
	nbr_event event;
	nbr_state dst_state;
} transition;

typedef struct neighbor {
	int inact_timer;					/* Inactivity Timer */
	int rxmt_timer;
	nbr_state state;
	int master;
	int more;
	uint32_t dd_seq_num;
	in_addr_t router_id;				/* Router ID */
	in_addr_t ip;						/* IP */
	uint8_t priority;
	int num_lsah;
	lsa_header lsahs[256];
	int num_lsr;
	ospf_lsr lsrs[256];
	int num_ack;
	lsa_header acks[256];
	struct neighbor *next;
} neighbor;

typedef struct {
	char name[16];
	int state;
	int sock;
	struct area *a;
	in_addr_t area_id;
	in_addr_t ip;
	in_addr_t mask;
	in_addr_t dr;
	in_addr_t bdr;
	int hello_interval;
	int dead_interval;
	int hello_timer;
	int wait_timer;
	uint16_t cost;
	uint16_t inf_trans_delay;
	int rxmt_interval;
	int num_nbr;
	neighbor *nbrs;
} interface;

typedef struct {
	in_addr_t ip;
	in_addr_t mask;
	in_addr_t next;
	uint16_t metric;
	const char *iface;
} route;

typedef struct area {
	int num_if;
	interface *ifs[16];
	int num_lsa;
	lsa_header *lsas[256];
	int num_route;
	route routes[256];
} area;

#endif
