#include <string.h>

#include "global.h"
#include "lsdb.h"


#define MODX						4102
#define LSA_CHECKSUM_OFFSET			15

uint16_t fletcher16(const uint8_t *data, size_t len) {
    const uint8_t *sp, *ep, *p, *q;
	int c0 = 0, c1 = 0;
	int x, y;

	for(sp = data, ep = sp + len; sp < ep; sp = q) {
		q = sp + MODX < ep ? sp + MODX : ep;
		for(p = sp; p < q; p++) {
			c0 += *p;
			c1 += c0;
		}
		c0 %= 255;
		c1 %= 255;
	}
	x = ((len - LSA_CHECKSUM_OFFSET) * c0 - c1) % 255;
	if(x <= 0) x += 255;
	y = 510 - c0 - x;
	if(y > 255) y -= 255;
	return (x << 8) + y;
}

int lsah_eql(const lsa_header *a, const lsa_header *b) {
	return a -> type == b -> type
	&& a -> id == b -> id
	&& a -> ad_router == b -> ad_router;
}

int cmp_lsah(const lsa_header *a, const lsa_header *b) {
	// DEBUG
	return -1;
	// TODO Aging Database
	if(a -> seq_num != b -> seq_num)
		return ntohl(a -> seq_num) - ntohl(b -> seq_num);
	if(a -> checksum != b -> checksum)
		return ntohs(a -> checksum) - ntohs(b -> checksum);
	if(ntohs(a -> age) == max_age)
		return 1;
	if(ntohs(b -> age) == max_age)
		return -1;
	if(abs(ntohs(a -> age) - ntohs(b -> age)) < max_age_diff)
		return ntohs(b -> age) - ntohs(a -> age);
	return 0;
}

void add_lsah(neighbor *nbr, const lsa_header *lsah) {
	int i;
	for(i = 0; i < nbr -> num_lsah; ++i)
		if(lsah_eql((nbr -> lsahs) + i, lsah)) {
			if(cmp_lsah((nbr -> lsahs) + i, lsah) < 0)
				break;
			return;
		}
	nbr -> lsahs[i] = *lsah;
	nbr -> num_lsah += i == nbr -> num_lsah;
}

lsa_header *find_lsa(const area *a, const lsa_header *lsah) {
	for(int i = 0; i < a -> num_lsa; ++i)
		if(lsah_eql(a -> lsas[i], lsah))
			return a -> lsas[i];
	return NULL;
}

lsa_header *insert_lsa(area *a, const lsa_header *lsah) {
	int i;
	for(i = 0; i < a -> num_lsa; ++i)
		if(lsah_eql(a -> lsas[i], lsah)) {
			if(cmp_lsah(a -> lsas[i], lsah) < 0) break;
			return NULL;
		}
	size_t len = ntohs(lsah -> length);
	a -> lsas[i] = realloc(a -> lsas[i], len);
	memcpy(a -> lsas[i], lsah, len);
	a -> num_lsa += i == a -> num_lsa;
	return a -> lsas[i];
}

uint32_t get_lsa_seq() {
	static uint32_t lsa_seq = 0x80000001;
	return htonl(lsa_seq++);
}

lsa_header *gen_router_lsa(area *a) {
	uint8_t buf[BUF_SIZE];
	lsa_header *lsah = (lsa_header *)buf;
	router_lsa *rlsa = (router_lsa *)(buf + sizeof(lsa_header));
	struct link *iter = rlsa -> links;

	lsah -> age = 0;
	lsah -> options = OPTIONS;
	lsah -> type = OSPF_ROUTER_LSA;
	lsah -> id = rt_id;
	lsah -> ad_router = rt_id;
	lsah -> seq_num = get_lsa_seq();

	rlsa -> flags = 0x00;
	rlsa -> zero = 0x00;

	for (int i = 0; i < a -> num_if; ++i) {
		interface *cur = a -> ifs[i];
		if (cur -> num_nbr == 0) { // StubNet
			iter -> type = ROUTERLSA_STUB;
			iter -> id = cur -> ip & cur -> mask;
			iter -> data = cur -> mask;
		} else { // Broadcast
			if (cur -> state == 1) { // TransNet
				iter -> type = ROUTERLSA_TRANSIT;
				iter -> id = cur -> dr;
				iter -> data = cur -> ip;
			} else { // StubNet
				iter -> type = ROUTERLSA_STUB;
				iter -> id = cur -> ip & cur -> mask;
				iter -> data = cur -> mask;
			}
		}
		iter -> tos = 0;
		iter -> metric = cur -> cost;
		++iter;
	}
	int num_link = iter - (rlsa -> links);
	rlsa -> num_link = htons(num_link);
	lsah -> length = htons(sizeof(lsa_header) + sizeof(router_lsa) + num_link * sizeof(struct link));
	// lsah -> checksum = 0;
	lsah -> checksum = htons(fletcher16(buf + sizeof(lsah -> age), ntohs(lsah -> length) - sizeof(lsah -> age)));
	return insert_lsa(a, lsah);
}