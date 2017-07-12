#include <stdio.h>
#include <string.h>

#include "global.h"
#include "transit.h"
#include "process.h"

uint16_t cksum(const uint16_t *data, size_t len) {
	// assert(len <= 65536);
	uint32_t sum = 0;
	for( ; len > 1; len -= 2)
		sum += *data++;
	if(len) // special byte
		sum += *(uint8_t *)data;
	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += (sum >> 16); // sum may >= 65536
	return ~sum;
}

interface *recv_ospf(int sock, uint8_t buf[], int size, in_addr_t *src) {
	struct iphdr *iph;
	ospf_header *ospfhdr;
	while(1) {
		if(recvfrom(sock, buf, size, 0, NULL, NULL) < (int)sizeof(struct iphdr))
			continue;
		iph = (struct iphdr *)buf;
		*src = iph -> saddr;
		if(iph -> protocol == IP_PROTO_OSPF) {
			ospfhdr = (ospf_header *)(buf + sizeof(struct iphdr));
			memset(ospfhdr -> auth_data, 0, sizeof(ospfhdr -> auth_data));
			if(cksum((uint16_t *)ospfhdr, ntohs(ospfhdr -> length)))
				continue;
			printf("%s received from %s\n", ospf_msg_type[ospfhdr -> type], inet_ntoa((struct in_addr){*src}));
			for(int i = 0; i < num_if; ++i)
				if((iph -> saddr & ifs[i].mask) == (ifs[i].ip & ifs[i].mask))
					return ifs + i;
		}
	}
	// assert(0);
	return NULL;
}

void send_ospf(const interface *iface, struct iphdr *iph, in_addr_t dst) {
	static int id; // randomly
	struct sockaddr_in addr;
	ospf_header * ospfhdr = (ospf_header *)((uint8_t *)iph + sizeof(struct iphdr));

	// ospf header
	ospfhdr -> version = 2;
	ospfhdr -> router_id = rt_id;
	ospfhdr -> area_id = iface -> area_id;
	ospfhdr -> checksum = 0x0;
	ospfhdr -> auth_type = 0;
	memset(ospfhdr -> auth_data, 0, sizeof(ospfhdr -> auth_data));
	ospfhdr -> checksum = cksum((uint16_t *)ospfhdr, ntohs(ospfhdr-> length));

	// ip header
	iph -> ihl = 5;
	iph -> version = 4;
	iph -> tos = 0xc0;
	iph -> tot_len = htons(sizeof(struct iphdr) + ospfhdr -> length);
	iph -> id = htons(id++);
	iph -> frag_off = 0x0;
	iph -> ttl = 1;
	iph -> protocol = IP_PROTO_OSPF;
	iph -> saddr = iface -> ip;
	iph -> daddr = dst;
	iph -> check = 0x0;
	iph -> check = cksum((uint16_t*)iph, sizeof(struct iphdr));

	// send it
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = dst;
	sendto(iface -> sock, ospfhdr, ntohs(ospfhdr -> length), 0, (struct sockaddr *)&addr, sizeof(addr));
	printf("%s sent to %s\n", ospf_msg_type[ospfhdr -> type], inet_ntoa((struct in_addr){dst}));
}

void *recv_loop(void *p) {
	uint8_t buf[BUF_SIZE];
	interface *iface;
	ospf_header *ospfhdr;
	neighbor *nbr;
	in_addr_t src;
	while (*(int *)p) {
		iface = recv_ospf(sock, buf, BUF_SIZE, &src);
		ospfhdr = (ospf_header *)(buf + sizeof(struct iphdr));
		if(ospfhdr -> router_id == rt_id)
			continue; // sent from self
		for(nbr = iface -> nbrs; nbr != NULL; nbr = nbr -> next)
			if(ospfhdr -> router_id == nbr -> router_id)
				break; // find current neighbor
		switch(ospfhdr -> type) {
			case OSPF_TYPE_HELLO: {
				process_hello(iface, nbr, ospfhdr, src);
				break;
			} case OSPF_TYPE_DD: {
				process_dd(iface, nbr, ospfhdr);
				break;
			} case OSPF_TYPE_LSR: {
				process_lsr(nbr, ospfhdr);
				break;
			} case OSPF_TYPE_LSU: {
				process_lsu(iface -> a, nbr, ospfhdr);
				break;
			} case OSPF_TYPE_LSACK: {
				process_ack(nbr, ospfhdr);
				break;
			} default: break;
		}
	}
	return NULL;
}

void *send_loop(void *p) {
	uint8_t buf[BUF_SIZE];
	while(*(int *)p) {
		for(int i = 0; i < num_if; ++i) {
			// check neighbors if out of date
			neighbor **p = &ifs[i].nbrs, *q;
			for(q = *p; q != NULL; q = *p) {
				if((++(q -> inact_timer)) >= ifs[i].dead_interval) { // erase
					--ifs[i].num_nbr;
					*p = q -> next;
					free(q);
				} else { // skip
					p = &(q -> next);
				}
			}

			// Hello packet (periodical enact)
			if((++ifs[i].hello_timer) >= ifs[i].hello_interval) {
				ifs[i].hello_timer = 0;
				produce_hello(ifs + i, (ospf_header *)(buf + sizeof(struct iphdr)));
				send_ospf(ifs + i, (struct iphdr *)buf, all_spf_routers);
			}

			// other packets
			for(neighbor *p = ifs[i].nbrs; p != NULL; p = p -> next) {
				if((++(p -> rxmt_timer)) >= ifs[i].rxmt_interval) {
					p -> rxmt_timer = 0;

					// DD packet
					if(p -> state == S_ExStart
					|| p -> state == S_Exchange) {
						produce_dd(ifs + i, p, (ospf_header *)(buf + sizeof(struct iphdr)));
						send_ospf(ifs + i, (struct iphdr *)buf, p -> ip);
						if(!(p -> master || p -> more))
							add_event(ifs + i, p, E_ExchangeDone);
					}

					// LSR packet
					if(p -> state == S_Exchange
					|| p -> state == S_Loading) {
						if(p -> num_lsah) {
							produce_lsr(p, (ospf_header *)(buf + sizeof(struct iphdr)));
							send_ospf(ifs + i, (struct iphdr *)buf, p -> ip);
						} else {
							add_event(ifs + i, p, E_LoadingDone);
						}
					}

					// LSU packets
					if(p -> num_lsr
					&& (p -> state == S_Exchange
						|| p -> state == S_Loading
						|| p -> state == S_Full)) {
						produce_lsu(ifs[i].a, p, (ospf_header *)(buf + sizeof(struct iphdr)));
						send_ospf(ifs + i, (struct iphdr *)buf, p -> ip);
					}
					if(rt_lsa && p -> state == S_Full) {
						// re-flooding
						produce_lsu_special(rt_lsa, (ospf_header *)(buf + sizeof(struct iphdr)));
						send_ospf(ifs + i, (struct iphdr *)buf, p->ip);
						rt_lsa = NULL;
					}
				}

				// LSAck packet
				if(p -> state >= S_Exchange && p -> num_ack) {
					produce_ack(p, (ospf_header *)(buf + sizeof(struct iphdr)));
					send_ospf(ifs + i, (struct iphdr *)buf, p -> ip);
					p->num_ack = 0;
				}
			}
		}
		sleep(1);
	}
	return NULL;
}