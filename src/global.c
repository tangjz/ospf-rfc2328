#include "global.h"

const int max_age = 3600, max_age_diff = 900;
const char *ospf_msg_type[] = {"Unknown", "Hello", "DD", "LSR", "LSU", "LSAck", NULL};

int sock, num_area, num_if; // init at main.c
in_addr_t rt_id, all_spf_routers, all_d_routers; // init at main.c
area areas[AREA_MAX];
interface ifs[IF_MAX];
lsa_header *rt_lsa;