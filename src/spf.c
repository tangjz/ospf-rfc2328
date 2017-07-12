#include <stdio.h>

#include "global.h"
#include "spf.h"

#define NODE_MAX		256
#define DISTANCE_INF	0x7FFF

typedef struct {
	in_addr_t id, mask, next;
	uint16_t dist;
	const lsa_header *lsa; // edges
} node;

int num_node;
node nodes[NODE_MAX];

int find_node_idx(in_addr_t id) {
	for(int i = 0; i < num_node; ++i)
		if(nodes[i].id == id)
			return i;
	return -1;
}

in_addr_t find_nbr_ip(const area *a, in_addr_t id) {
	for(int i = 0; i < a -> num_if; ++i)
		for(neighbor *iter = (a -> ifs[i]) -> nbrs; iter != NULL; iter = iter -> next)
			if(iter -> router_id == id)
				return iter -> ip;
	return 0;
}

const char *find_if_name(const area *a, in_addr_t ip) {
	for(int i = 0; i < a -> num_if; ++i) {
		interface *cur = a -> ifs[i];
		if((cur -> ip & cur -> mask) == (ip & cur -> mask))
			return cur -> name;
	}
	return NULL;
}

void dijkstra() {
	int root = find_node_idx(rt_id), pre[NODE_MAX], vis[NODE_MAX] = {}; // use vis as bool[]
	node *leaf = nodes + num_node;

	nodes[root].dist = 0;
	pre[root] = root;
	while(1) {
		int u = -1, v;
		// find the nearest node not been visited
		for(int i = 0; i < num_node; ++i)
			if(!vis[i] && (u == -1 || nodes[i].dist < nodes[u].dist))
				u = i;
		if(u == -1 || nodes[u].dist == DISTANCE_INF)
			break;
		// node u is extendable now
		vis[u] = 1;
		if(nodes[u].lsa -> type == OSPF_ROUTER_LSA) {
			router_lsa *rlsa = (router_lsa *)((uint8_t *)nodes[u].lsa + sizeof(lsa_header));
			int ctr = ntohs(rlsa -> num_link);
			nodes[u].mask = 0;
			for(const struct link *iter = rlsa -> links; ctr--; ++iter) {
				switch(iter -> type) {
					case ROUTERLSA_ROUTER:
					case ROUTERLSA_TRANSIT:
						v = find_node_idx(iter -> id);
						break;
					case ROUTERLSA_STUB:
						printf("leaf: %s\n", inet_ntoa((struct in_addr){iter -> id}));
						leaf -> id = iter -> id;
						leaf -> mask = iter -> data;
						leaf -> next = nodes[u].next;
						leaf -> dist = nodes[u].dist + ntohs(iter -> metric);
						leaf -> lsa = nodes[u].lsa;
						pre[leaf - nodes] = u;
						++leaf;
					default:
						continue;
				}
				if(v < 0 || vis[v])
					continue;
				int new_dist = nodes[u].dist + ntohs(iter -> metric);
				if(new_dist < nodes[v].dist) {
					nodes[v].dist = new_dist;
					pre[v] = u;
				}
			}
		} else if(nodes[u].lsa -> type == OSPF_NETWORK_LSA) {
			network_lsa *nlsa = (network_lsa *)((uint8_t *)nodes[u].lsa + sizeof(lsa_header));
			int ctr = (ntohs(nodes[u].lsa->length) - sizeof(lsa_header) -
					sizeof(nlsa->mask)) / sizeof(in_addr_t);
			nodes[u].mask = nlsa -> mask;
			for(const in_addr_t *iter = nlsa -> routers; ctr--; ++iter) {
				v = find_node_idx(*iter);
				if(v < 0 || vis[v])
					continue;
				router_lsa *rlsa = (router_lsa *)((uint8_t *)nodes[v].lsa + sizeof(lsa_header));
				int i, num_link = ntohs(rlsa -> num_link);
				for(i = 0; i < num_link; ++i)
					if((rlsa -> links[i].type == ROUTERLSA_TRANSIT && rlsa -> links[i].id == nodes[u].id)
					|| (rlsa -> links[i].type == ROUTERLSA_STUB && rlsa -> links[i].id == (nodes[u].id & nlsa -> mask)))
						break;
				if(i == num_link)
					continue;
				int new_dist = nodes[u].dist + ntohs(rlsa -> links[i].metric);
				if(new_dist < nodes[v].dist) {
					nodes[v].dist = new_dist;
					pre[v] = u;
				}
			}
		}
		// calculate the next router id
		for(v = u; v != root; v = pre[v])
			if(!nodes[v].mask)
				nodes[u].next = nodes[v].id;
	}
	num_node = leaf - nodes;
}

void update_route(area *a) {
	// get info from lsa type 1 and lsa type 2
	num_node = 0;
	for(int i = 0 ; i < a -> num_lsa; ++i) {
		lsa_header *cur = a -> lsas[i];
		node *nxt = nodes + num_node;
		if(cur -> type != OSPF_ROUTER_LSA
		&& cur -> type != OSPF_NETWORK_LSA)
			continue;
		nxt -> id = cur -> id;
		nxt -> lsa = cur;
		nxt -> dist = DISTANCE_INF;
		++num_node;
	}

	dijkstra();

	// gen info of lsa type 3 and lsa type 4
	for(int i = 0 ; i < a -> num_lsa; ++i) {
		lsa_header *cur = a -> lsas[i];
		node *nxt = nodes + num_node;
		if(cur -> type != OSPF_SUMMARY_LSA
		&& cur -> type != OSPF_ASBR_SUMMARY_LSA)
			continue;
		summary_lsa *slsa = (summary_lsa *)((uint8_t *)cur + sizeof(lsa_header));
		if(find_node_idx(cur -> id) < 0)
			continue;
		int j = find_node_idx(cur -> ad_router);
		if(j < 0 || nodes[j].dist == DISTANCE_INF) continue;
		nxt -> id = cur -> id;
		nxt -> mask = slsa -> mask;
		nxt -> next = nodes[j].next;
		nxt -> dist = nodes[j].dist + ntohl(slsa -> metric & 0xF0);
		nxt -> lsa = cur;
		++num_node;
	}
	// TODO gen info of lsa type 5
	//

	// DEBUG
	for(int i = 0; i < num_node; ++i) {
		printf("node: %s\n", inet_ntoa((struct in_addr){nodes[i].id}));
		printf("next: %s\n", inet_ntoa((struct in_addr){nodes[i].next}));
		printf("dist: %hu\n", nodes[i].dist);
	}
	//

	// transfrom info into route
	a -> num_route = 0;
	for(int i = 0; i < num_node; ++i) {
		node *cur = nodes + i;
		route *nxt = (a -> routes) + (a -> num_route);
		if(!(cur -> mask) || cur -> dist == DISTANCE_INF)
				continue;
		nxt -> mask = cur -> mask;
		nxt -> ip = cur -> id & nxt -> mask;
		nxt -> next = find_nbr_ip(a, cur -> next);
		nxt -> iface = find_if_name(a, nxt -> next ? nxt -> next : nxt -> ip);
		nxt -> metric = cur -> dist;
		++(a -> num_route);
	}
}