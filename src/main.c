#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "global.h"
#include "lsdb.h"
#include "spf.h"
#include "transit.h"

// init interface, socket, area, and others
void ospf_init() {
	int num_ifr;
	struct ifreq ifrs[IF_MAX];
	struct ifconf ifc;

	all_spf_routers = inet_addr("224.0.0.5");
	all_d_routers = inet_addr("224.0.0.6");

	sock = socket(AF_PACKET, SOCK_DGRAM, 0);
	ifc.ifc_len = sizeof(ifrs);
	ifc.ifc_req = ifrs;
	ioctl(sock, SIOCGIFCONF, &ifc);
	num_ifr = ifc.ifc_len / sizeof(struct ifreq);
	num_if = 0;
	rt_id = 0;
	for(int i = 0; i < num_ifr; ++i) {
		struct ifreq *cur = ifrs + i;
		interface *nxt = ifs + num_if;
		if(strcmp(cur -> ifr_name, "lo") == 0)
			continue;
		// fetch interface name, ip addr, sub mask
		strcpy(nxt -> name, cur -> ifr_name);

		ioctl(sock, SIOCGIFADDR, cur);
		nxt -> ip = ((struct sockaddr_in *)&(cur -> ifr_addr)) -> sin_addr.s_addr;

		ioctl(sock, SIOCGIFNETMASK, cur);
		nxt -> mask = ((struct sockaddr_in *)&(cur -> ifr_netmask)) -> sin_addr.s_addr;

		// update route id
		if(ntohl(rt_id) < ntohl(nxt -> ip))
			rt_id = nxt -> ip;

		// turn on promisc mode (need super mode, so be careful)
		ioctl(sock, SIOCGIFFLAGS, cur);
		cur -> ifr_flags |= IFF_PROMISC;
		ioctl(sock, SIOCSIFFLAGS, cur);

		// bind socket
		nxt -> sock = socket(AF_INET, SOCK_RAW, IP_PROTO_OSPF);
		setsockopt(nxt -> sock, SOL_SOCKET, SO_BINDTODEVICE, cur, sizeof(struct ifreq));

		++num_if;
	}
	printf("%d interfaces are turned on.\n", num_if);
	for(int i = 0; i < num_if; ++i) {
		interface *cur = ifs + i;
		printf("interface #%d: %s\n", i + 1, cur -> name);

		// (user) set area id
		uint32_t area_id;
		char cmd[257];
		do {
			printf("set its area id (%d-%d): ", 0, AREA_MAX - 1);
		} while(!(scanf("%s", cmd) == 1 && sscanf(cmd, "%u", &area_id) == 1 && area_id < AREA_MAX));
		area *a = areas + area_id;
		cur -> area_id = htonl(area_id);
		cur -> a = a;
		a -> ifs[a -> num_if] = cur;
		++(a -> num_if);

		// init other attr (could be modified for user)
		cur -> state = 0;
		cur -> hello_interval = 10;
		cur -> dead_interval = 40;
		cur -> rxmt_interval = 5;
		cur -> hello_timer = 0;
		cur -> wait_timer = 0;
		cur -> cost = htons(5);
		cur -> inf_trans_delay = 1;
		cur -> num_nbr = 0;
		cur -> nbrs = NULL;
	}
}

// run loops about recv, updt, send
void ospf_run() {
	int pth_arg = 1;
	pthread_t pth_recv, pth_send;
	pthread_create(&pth_recv, NULL, recv_loop, &pth_arg);
	pthread_create(&pth_send, NULL, send_loop, &pth_arg);
	// updt_loop (implemented at here)
	while(1) {
		// inet_ntoa() returns a pointer in static buffer, so be careful with inet_ntoa()
		char cmd[257], ip[33], mask[33], next[33];
		for(int i = 0; i < num_if; ++i) {
			rt_lsa = gen_router_lsa(ifs[i].a);
			if(!ifs[i].state)
				continue;
			area *a = ifs[i].a;
			// delete old routes at service
			for(int i = 0; i < a -> num_route; ++i) {
				route *cur = (a -> routes) + i;
				if(!(cur -> next))
					continue;
				strcpy(ip, inet_ntoa((struct in_addr){cur -> ip}));
				strcpy(mask, inet_ntoa((struct in_addr){cur -> mask}));
				sprintf(cmd, "route del -net %s netmask %s", ip, mask);
				puts(cmd);
				if(system(cmd)) {
					fprintf(stderr, "error: can not delete this route");
					exit(-1);
				}
			}

			update_route(a);

			// add new routes at service
			for(int i = 0; i < a -> num_route; ++i) {
				route *cur = (a -> routes) + i;
				if(!(cur -> next))
					continue;
				strcpy(ip, inet_ntoa((struct in_addr){cur -> ip}));
				strcpy(mask, inet_ntoa((struct in_addr){cur -> mask}));
				strcpy(next, inet_ntoa((struct in_addr){cur -> next}));
				sprintf(cmd, "route add -net %s netmask %s gw %s metric %hu dev %s", ip, mask, next, cur -> metric, cur -> iface);
				puts(cmd);
				if(system(cmd)) {
					fprintf(stderr, "error: can not add this route");
					exit(-1);
				}
			}

			// print current route table
			puts("--------------------------------");
			printf("Area %u Route Table:\n", ntohl(ifs[i].area_id));
			printf("IP\t\tMASK\t\tNext\t\tDistance\tInterface\n");
			for(int i = 0; i < a -> num_route; ++i) {
				route *cur = (a -> routes) + i;
				if(!(cur -> next))
					continue;
				printf("%s\t", inet_ntoa((struct in_addr){cur -> ip}));
				printf("%s\t", inet_ntoa((struct in_addr){cur -> mask}));
				printf("%s\t", inet_ntoa((struct in_addr){cur -> next}));
				printf("%hu\t%s\n", cur -> metric, cur -> iface);
			}
			puts("--------------------------------");
		}
	}
	pthread_join(pth_recv, NULL);
	pthread_join(pth_send, NULL);
}

int main(int argc, char *argv[]) {
	// DEBUG
	puts("What's the hell!");
	//
	if(geteuid() != 0) {
		fprintf(stderr, "This program should run as root.\n"); 
		fprintf(stderr, "Usage: sudo ");
		for(int i = 0; i < argc; ++i)
			fprintf(stderr, "%s%c", argv[i], " \n"[i == argc - 1]);
		return -1;
	}
	puts("simple OSPF refer to RFC2328");
	ospf_init();
	ospf_run();
	return 0;
}
