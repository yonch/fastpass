/*
 * id_map.h
 *
 *  Created on: January 15, 2013
 *      Author: aousterh
 */

#include "ccan/hash/hash.h"
#include "ccan/htable/htable.h"
#include "../graph-algo/admissible_structures.h"

#include <inttypes.h>

#ifndef ID_MAP_H_
#define ID_MAP_H_

#define SWITCH_ADDR_1 0x00005e000101
#define SWITCH_ADDR_2 0x00005e000102
#define SWITCH_ADDR_3 0xa4934c801381

#define MAC_MASK 0xFFFFFFFFFFFF

// mapping = 64 bits (16 id + 48 mac)
#define MAC_ADDR(mapping)      ((uint64_t) mapping & MAC_MASK)
#define ID(mapping)            ((uint16_t) (mapping >> 48))
#define MAKE_MAPPING(id, addr) ((uint64_t) id << 48 | MAC_ADDR(addr))

struct mac_to_id_mapping {
    struct htable ht;
    uint16_t next_id;
    uint64_t mappings[MAX_NODES];
};

static inline
size_t hash_func(const void *e, void *unused) {    
    return hash64(e, 1, 0);
}

static inline
bool comp(const void *e, void *key) {
    uint64_t *p_to_e = (uint64_t *) e;
    uint64_t *p_to_key = (uint64_t *) key;
    return MAC_ADDR(*p_to_e) == MAC_ADDR(*p_to_key);
}

static inline
void init_mapping(struct mac_to_id_mapping *map) {
    assert(map != NULL);

    htable_init(&map->ht, hash_func, NULL);
    map->next_id = 0;
}

static inline
uint16_t get_node_id(struct mac_to_id_mapping *map, uint64_t mac_addr) {
    assert(mac_addr == (mac_addr & MAC_MASK));

    if (mac_addr == SWITCH_ADDR_1 ||
        mac_addr == SWITCH_ADDR_2 ||
        mac_addr == SWITCH_ADDR_3)
        return OUT_OF_BOUNDARY_NODE_ID;

    // check for existing mapping
    uint64_t *mapping = (uint64_t *) htable_get(&map->ht, hash_func(&mac_addr, NULL),
                                                comp, &mac_addr);

    if (mapping != NULL) {
        // already exists - return the id
        uint16_t id = ID(*mapping);
        return id;
    }
    else {
        // create a new id mapping
        uint16_t new_id = map->next_id++;
        map->mappings[new_id] = MAKE_MAPPING(new_id, mac_addr);
        htable_add(&map->ht, hash_func(&mac_addr, NULL), &map->mappings[new_id]);
        return new_id;
    }
}

#endif /* ID_MAP_H_ */
