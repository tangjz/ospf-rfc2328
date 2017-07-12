#include <stdio.h>
#include <string.h>

#include "global.h"
#include "process.h"
#include "lsdb.h"

// neighbor state machine
const char *state_str[] = {
	"Down",
	"Attempt",
	"Init",
	"2-Way",
	"ExStart",
	"Exchange",
	"Loading",
	"Full"
}, *event_str[] = {
	"HelloReceived",
	"Start",
	"1-WayReceived",
	"2-WayReceived",
	"AdjOK",
	"NegotiationDone",
	"ExchangeDone",
	"LoadingDone"
};
const int num_nsm = 9;
const transition nsm[] = {
	{S_Down,		E_Start,			S_Attempt},	// NBMA only
	{S_Attempt,		E_HelloReceived,	S_Init},	// NBMA only
	{S_Down,		E_HelloReceived,	S_Init},
	{S_Init,		E_2WayReceived,		S_2Way},
	{S_2Way,		E_1WayReceived,		S_Init},
	{S_2Way,		E_AdjOK,			S_ExStart},
	{S_ExStart,		E_NegotiationDone,	S_Exchange},
	{S_Exchange,	E_ExchangeDone,		S_Loading},
	{S_Loading,		E_LoadingDone,		S_Full}
};
void add_event(interface *iface, neighbor *nbr, nbr_event event) {
	puts("--------------------------------");
	printf("Interface: %s\n", iface -> name);
	printf("Neighbor: %s\n", inet_ntoa((struct in_addr){nbr -> ip}));
	printf("Old State: %s\n", state_str[nbr -> state]);
	printf("Event: %s\n", event_str[event]);
	for(int i = 0; i < num_nsm; ++i)
		if(nbr -> state == nsm[i].src_state
		&& event == nsm[i].event) {
			nbr -> state = nsm[i].dst_state;
			if(nbr -> state == S_Full
			&& iface -> dr == nbr -> ip)
				iface->state = 1;
			break;
		}
	printf("New State: %s\n", state_str[nbr -> state]);
	puts("--------------------------------");
}

// Hello packet
void process_hello(interface *iface, neighbor *nbr, const ospf_header *ospfhdr, in_addr_t src) {
	ospf_hello *hello = (ospf_hello *)((uint8_t *)ospfhdr + sizeof(ospf_header));
	const in_addr_t *const nbrs = (in_addr_t *)hello -> neighbors;
	// create a new neighbor node in nbrs list
	if(nbr == NULL) {
		nbr = malloc(sizeof(neighbor));
		nbr -> state = S_Down;
		nbr -> rxmt_timer = 0;
		nbr -> master = 1;
		nbr -> more = 1;
		nbr -> dd_seq_num = 2333; // time(NULL);
		nbr -> router_id = ospfhdr -> router_id;
		nbr -> ip = src;
		nbr -> priority = hello -> priority;
		nbr -> num_lsah = 0;
		nbr -> num_lsr = 0;
		nbr -> next = iface -> nbrs;
		iface -> nbrs = nbr;
		++(iface -> num_nbr);
	}
	nbr -> inact_timer = 0;
	if(iface -> dr != hello -> d_router)
		iface -> dr = hello -> d_router;
	iface -> bdr = hello -> bd_router;
	add_event(iface, nbr, E_HelloReceived);
	int i, num_nbr = (ntohs(ospfhdr -> length) - sizeof(ospf_header) - sizeof(ospf_hello)) / sizeof(in_addr_t);
	for(i = 0; i < num_nbr; ++i)
		if(nbrs[i] == rt_id)
			break;
	add_event(iface, nbr, i < num_nbr ? E_2WayReceived : E_1WayReceived);
	if(hello -> d_router == src
	|| hello -> d_router == iface -> ip
	|| hello -> bd_router == src
	|| hello -> bd_router == iface -> ip)
		add_event(iface, nbr, E_AdjOK);
}
void produce_hello(const interface *iface, ospf_header *ospfhdr) {
	ospf_hello *hello = (ospf_hello *)((uint8_t *)ospfhdr + sizeof(ospf_header));
	in_addr_t *nbr = hello -> neighbors;

	hello -> network_mask = iface -> mask;
	hello -> hello_interval = htons(iface -> hello_interval);
	hello -> options = OPTIONS;
	// avoid becoming DR or BDR
	hello -> priority = 0;
	hello -> dead_interval = htonl(iface -> dead_interval);
	hello -> d_router = iface -> dr;
	hello -> bd_router = iface -> bdr;
	for(const neighbor *p = iface -> nbrs; p != NULL; p = p -> next)
		*nbr++ = p -> router_id;
	ospfhdr -> type = OSPF_TYPE_HELLO;
	ospfhdr -> length = htons((uint8_t *)nbr - (uint8_t *)ospfhdr);
}

// DD packet
void process_dd(interface *iface, neighbor *nbr, const ospf_header *ospfhdr) {
	ospf_dd * dd = (ospf_dd *)((uint8_t *)ospfhdr + sizeof(ospf_header));
	if(dd -> flags & DD_FLAG_I) {
		if(dd -> flags & DD_FLAG_M && ntohl(rt_id) < ntohl(ospfhdr -> router_id)) {
			nbr -> master = 0;
			nbr -> dd_seq_num = ntohl(dd -> seq_num);
			add_event(iface, nbr, E_NegotiationDone);
		}
	} else {
		if(!(dd -> flags & DD_FLAG_MS) && dd -> seq_num == htonl(nbr -> dd_seq_num)) {
			nbr -> master = 1;
			add_event(iface, nbr, E_NegotiationDone);
		}
		if(nbr -> master) {
			if(dd -> seq_num == htonl(nbr -> dd_seq_num)) {
				add_event(iface, nbr, E_NegotiationDone);
				++(nbr -> dd_seq_num);
				if(!(dd -> flags & DD_FLAG_M))
					add_event(iface, nbr, E_ExchangeDone);
			}
		} else {
			if(dd -> seq_num == htonl(nbr -> dd_seq_num + 1)) {
				++(nbr -> dd_seq_num);
				nbr -> more = dd -> flags & DD_FLAG_M;
			}
		}
		int num_lsah = (ntohs(ospfhdr -> length) - sizeof(ospf_header) - sizeof(ospf_dd)) / sizeof(lsa_header);
		if(num_lsah == 0)
			nbr -> more = 0;
		for(int i = 0; i < num_lsah; ++i) {
			lsa_header *lsah = find_lsa(iface -> a, (dd -> lsahs) + i);
			if(!lsah || cmp_lsah(lsah, (dd -> lsahs) + i) < 0)
				add_lsah(nbr, (dd -> lsahs) +i);
		}
	}
}
void produce_dd(const interface *iface, const neighbor *nbr, ospf_header *ospfhdr) {
	ospf_dd *dd = (ospf_dd *)((uint8_t *)ospfhdr + sizeof(ospf_header));
	lsa_header *lsah = dd -> lsahs;

	dd -> mtu = htons(1500); // ethernet
	dd -> options = OPTIONS;
	dd -> flags = 0;
	if(nbr -> master)
		dd -> flags |= DD_FLAG_MS;
	if(nbr -> state == S_ExStart)
		dd -> flags |= DD_FLAG_I;
	if(nbr -> more)
		dd -> flags |= DD_FLAG_M;
	dd -> seq_num = htonl(nbr -> dd_seq_num);
	if(nbr -> state == S_Exchange && nbr -> more)
		for(int i = 0; i < (iface -> a) -> num_lsa; ++i)
			memcpy(lsah++, (iface -> a) -> lsas[i], sizeof(lsa_header));
	ospfhdr -> type = OSPF_TYPE_DD;
	ospfhdr -> length = htons((uint8_t *)lsah - (uint8_t *)ospfhdr);
}

// LSR packet
int lsr_eql(const ospf_lsr *a, const ospf_lsr *b) {
	return a -> ls_type == b -> ls_type
	&& a -> ls_id == b -> ls_id
	&& a -> ad_router == b -> ad_router;
}
void process_lsr(neighbor *nbr, ospf_header *ospfhdr) {
	ospf_lsr *lsr = (ospf_lsr *)((uint8_t *)ospfhdr + sizeof(ospf_header));
	ospf_lsr *last = (ospf_lsr *)((uint8_t *)ospfhdr + ntohs(ospfhdr -> length));
	for( ; lsr < last; ++lsr) {
		int i = 0;
		for(i = 0; i < nbr -> num_lsr; ++i)
			if(lsr_eql(lsr, (nbr -> lsrs) + i))
				break;
		if(i == nbr -> num_lsr)
			nbr -> lsrs[(nbr -> num_lsr)++] = *lsr;
	}
}
void produce_lsr(const neighbor *nbr, ospf_header *ospfhdr) {
	ospf_lsr *lsr = (ospf_lsr *)((uint8_t *)ospfhdr + sizeof(ospf_header));
	for(int i = 0; i < nbr -> num_lsah; ++i) {
		lsr -> ls_type = htonl(nbr -> lsahs[i].type);
		lsr -> ls_id = nbr -> lsahs[i].id;
		lsr -> ad_router = nbr -> lsahs[i].ad_router;
		++lsr;
	}
	ospfhdr -> type = OSPF_TYPE_LSR;
	ospfhdr -> length = htons((uint8_t *)lsr - (uint8_t *)ospfhdr);
}

// LSU packet
void process_lsu(area *a, neighbor *nbr, ospf_header *ospfhdr) {
	uint8_t *p = (uint8_t *)ospfhdr + sizeof(ospf_header) + 4;
	int num_lsr = ntohl(*((uint32_t *)((uint8_t *)ospfhdr + sizeof(ospf_header))));
	for(int i = 0; i < num_lsr; ++i) {
		lsa_header *lsah = (lsa_header *)p;
		uint16_t sum = ntohs(lsah -> checksum);
		lsah -> checksum = 0;
		if(sum != fletcher16(p + sizeof(lsah -> age),
				ntohs(lsah -> length) - sizeof(lsah -> age)))
			continue;
		lsah -> checksum = htons(sum);
		for(int i = 0; i < nbr -> num_lsah; ++i)
			if(lsah_eql((nbr -> lsahs) + i, lsah)) { // erase old lsah
				nbr -> lsahs[i] = nbr -> lsahs[--(nbr -> num_lsah)];
				break;
			}
		nbr -> acks[(nbr -> num_ack)++] = *lsah;
		insert_lsa(a, lsah);
		p += ntohs(lsah -> length);
	}
}
void produce_lsu(const area *a, const neighbor *nbr, ospf_header *ospfhdr) {
	uint8_t *p = (uint8_t *)ospfhdr + sizeof(ospf_header) + 4;
	*((uint32_t *)((uint8_t *)ospfhdr + sizeof(ospf_header))) = ntohl(nbr -> num_lsr);
	for(int i = 0; i < nbr -> num_lsr; ++i) {
		const ospf_lsr *nxt = (nbr -> lsrs) + i;
		for(int j = 0; j < a -> num_lsa; ++j) {
			lsa_header *cur = a -> lsas[j];
			if(nxt -> ls_type == htonl(cur -> type)
			&& nxt -> ls_id == cur -> id
			&& nxt -> ad_router == cur -> ad_router) { // insert new lsah
				size_t len = htons(cur -> length);
				memcpy(p, cur, len);
				p += len;
				break;
			}
		}
	}
	ospfhdr -> type = OSPF_TYPE_LSU;
	ospfhdr -> length = htons(p - (uint8_t *)ospfhdr);
}
void produce_lsu_special(const lsa_header *lsa, ospf_header *ospfhdr) {
	uint8_t *p = (uint8_t *)ospfhdr + sizeof(ospf_header) + 4;
	*((uint32_t *)((uint8_t *)ospfhdr + sizeof(ospf_header))) = ntohl(1); // only itself
	size_t len = htons(lsa -> length);
	memcpy(p, lsa, len);
	p += len;
	ospfhdr -> type = OSPF_TYPE_LSU;
	ospfhdr -> length = htons(p - (uint8_t *)ospfhdr);
}

// LSAck packet
void process_ack(neighbor *nbr, ospf_header *ospfhdr) {
	if(nbr -> state < S_Exchange)
		return; // disable
	lsa_header *lsah = (lsa_header *)((uint8_t *)ospfhdr + sizeof(ospf_header));
	int num_lsah = (ntohs(ospfhdr -> length) - sizeof(ospf_header)) / sizeof(lsa_header);
	for(int i = 0; i < num_lsah; ++i) {
		lsa_header *cur = lsah + i;
		for(int j = 0; j < nbr -> num_lsr; ++j) {
			ospf_lsr *nxt = (nbr -> lsrs) + i;
			if(nxt -> ls_type == htonl(cur -> type)
			&& nxt -> ls_id == cur -> id
			&& nxt -> ad_router == cur -> ad_router) {
				*nxt = nbr -> lsrs[--(nbr -> num_lsr)];
				break;
			}
		}
	}
}
void produce_ack(const neighbor *nbr, ospf_header *ospfhdr) {
	lsa_header *lsah = (lsa_header *)((uint8_t *)ospfhdr + sizeof(ospf_header));
	for(int i = 0; i < nbr -> num_ack; ++i)
		*lsah++ = nbr -> acks[i];
	ospfhdr -> type = OSPF_TYPE_LSACK;
	ospfhdr -> length = htons((uint8_t*)lsah - (uint8_t *)ospfhdr);
}