#ifndef _GLOBAL_H
#define _GLOBAL_H

#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "packet.h"
#include "structure.h"

#define IP_PROTO_OSPF	89
#define AREA_MAX		256
#define IF_MAX			256
#define BUF_SIZE		2048	// eth MTU = 1500 < 2048
#define OPTIONS			0x02	// E: 1

extern const int max_age, max_age_diff;
extern const char *ospf_msg_type[];

extern int sock, num_area, num_if;
extern in_addr_t rt_id, all_spf_routers, all_d_routers;
extern area areas[AREA_MAX];
extern interface ifs[IF_MAX];
extern lsa_header *rt_lsa;

#endif /* _GLOBAL_H */