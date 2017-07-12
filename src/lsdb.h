#ifndef _LSDB_H
#define _LSDB_H

#include "packet.h"
#include "structure.h"

// compare functions
int lsah_eql(const lsa_header *a, const lsa_header *b);
int cmp_lsah(const lsa_header *a, const lsa_header *b);

// functions for dd like
void add_lsah(neighbor *nbr, const lsa_header *lsah);
lsa_header *find_lsa(const area *a, const lsa_header *lsah);

// functions for lsu like
uint16_t fletcher16(const uint8_t *data, size_t len);
lsa_header *insert_lsa(area *a, const lsa_header *lsah);

// initialize
lsa_header *gen_router_lsa(area *a);

#endif /* _LSDB_H */