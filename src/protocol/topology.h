
#ifndef FASTPASS_TOPOLOGY_H_
#define FASTPASS_TOPOLOGY_H_

#include "platform/generic.h"

//#define MAX_NODES 1024
//#define NODES_SHIFT 10  // 2^NODES_SHIFT = MAX_NODES
#define MAX_NODES 256
#define NODES_SHIFT 8  // 2^NODES_SHIFT = MAX_NODES
#define MAX_RACKS 16
#define TOR_SHIFT 8  // number of machines per rack is at most 2^TOR_SHIFT
#define MAX_NODES_PER_RACK 256  // = 2^TOR_SHIFT
#define OUT_OF_BOUNDARY_NODE_ID MAX_NODES  // highest node id

#define FB_RACK_PERFECT_HASH_CONST	0x33

#define MANUFACTURER_MAC_MASK		0xFFFFFF000000
#define VRRP_SWITCH_MAC_PREFIX 		0x00005e000000
#define CISCO_SWITCH_MAC_PREFIX		0xa4934c000000

/* translates IP address to short FastPass ID */
static inline u16 fp_map_ip_to_id(__be32 ipaddr) {
	return (u16)(ntohl(ipaddr) & ((1 << 8) - 1));
}

/* translates MAC address to short FastPass ID */
static inline u16 fp_map_mac_to_id(u64 mac) {
	u32 hash = fp_jhash_3words(mac & 0xFFFFFFFF, mac >> 32, 0,
			FB_RACK_PERFECT_HASH_CONST);

	return hash & ((1 << 8) - 1);
}


/* returns the destination node from the allocated dst */
static inline u16 fp_alloc_node(u16 alloc) {
	return alloc & 0x3FFF;
}

/* return the path from the allocation */
static inline u16 fp_alloc_path(u16 alloc) {
	return alloc >> 14;
}

#endif /* FASTPASS_TOPOLOGY_H_ */
