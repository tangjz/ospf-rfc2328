#ifndef _PACKET_H
#define _PACKET_H

#include <netinet/ip.h>

#define OSPF_TYPE_HELLO		1
#define OSPF_TYPE_DD		2
#define OSPF_TYPE_LSR		3
#define OSPF_TYPE_LSU		4
#define OSPF_TYPE_LSACK		5

/* OSPF packet header structure. */
typedef struct {
	uint8_t version;						/* OSPF Version. */
	uint8_t type;							/* Packet Type. */
	uint16_t length;						/* Packet Length. */
	in_addr_t router_id;					/* Router ID. */
	in_addr_t area_id;						/* Area ID. */
	uint16_t checksum;						/* Check Sum. */
	uint16_t auth_type;						/* Authentication Type. */
	/* Authentication Data. */
	union {
		/* Simple Authentication. */
		uint8_t auth_data[8];
		/* Cryptographic Authentication. */
		struct {
			uint16_t zero;					/* Should be 0. */
			uint8_t key_id;					/* Key ID. */
			uint8_t auth_data_len;			/* Auth Data Length. */
			uint32_t crypt_seqnum;			/* Cryptographic Sequence Number. */
		};
	};
} ospf_header;

/* OSPF Hello body format. */
typedef struct {
	in_addr_t network_mask;
	uint16_t hello_interval;
	uint8_t options;
	uint8_t priority;
	uint32_t dead_interval;
	in_addr_t d_router;
	in_addr_t bd_router;
	in_addr_t neighbors[];
} ospf_hello;

#define OSPF_ROUTER_LSA				1
#define OSPF_NETWORK_LSA			2
#define OSPF_SUMMARY_LSA			3
#define OSPF_ASBR_SUMMARY_LSA		4
#define OSPF_AS_EXTERNAL_LSA		5

/* OSPF LSA header. */
typedef struct {
	uint16_t age;
	uint8_t options;
	uint8_t type;
	in_addr_t id;
	in_addr_t ad_router;
	uint32_t seq_num;
	uint16_t checksum;
	uint16_t length;
} lsa_header;

#define ROUTERLSA_ROUTER			1
#define ROUTERLSA_TRANSIT			2
#define ROUTERLSA_STUB				3
#define ROUTERLSA_VIRTUAL			4

/* OSPF Router-LSAs structure. */
typedef struct {
	uint8_t flags;
	uint8_t zero;
	uint16_t num_link;
	struct link {
		in_addr_t id;
		in_addr_t data;
		uint8_t type;
		uint8_t tos;
		uint16_t metric;
	} links[];
} router_lsa;

/* OSPF Network-LSAs structure. */
typedef struct {
	in_addr_t mask;
	in_addr_t routers[];
} network_lsa;

/* OSPF Summary-LSAs structure. */
typedef struct {
	in_addr_t mask;
	union {
		uint8_t tos;
		uint32_t metric;
	};
} summary_lsa;

/* OSPF AS-external-LSAs structure. */
typedef struct {
	in_addr_t mask;
	struct {
		union {
			uint8_t tos;
			uint32_t metric;
		};
		in_addr_t fwd_addr;
		uint32_t route_tag;
	} e[];
} as_external_lsa;

/* Flags in Database Description packet */
#define DD_FLAG_MS					0x01
#define DD_FLAG_M					0x02
#define DD_FLAG_I					0x04

/* OSPF Database Description body format. */
typedef struct {
	uint16_t mtu;
	uint8_t options;
	uint8_t flags;
	uint32_t seq_num;
	lsa_header lsahs[];
} ospf_dd;

/* OSPF Link State Request */
typedef struct {
	uint32_t ls_type;					/* LS type */
	uint32_t ls_id;						/* Link State ID */
	uint32_t ad_router;					/* Advertising Router */
} ospf_lsr;

#endif /* _PACKET_H */