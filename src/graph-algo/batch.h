/*
 * batch.h
 *
 *  Created on: Apr 28, 2014
 *      Author: yonch
 */

#ifndef BATCH_H_
#define BATCH_H_

#include "../protocol/topology.h"

#define BATCH_SIZE 16  // must be consistent with bitmaps in batch_state
#define BATCH_SHIFT 4  // 2^BATCH_SHIFT = BATCH_SIZE

#define SUPPORTS_OVERSUBSCRIPTION		0

/* packing of bitmasks into 64 bit words */
#if 0
#define BITMASKS_PER_64_BIT 	(64 >> BATCH_SHIFT)
#define BITMASK_WORD(node)		(node >> (6 - BATCH_SHIFT))
#define BITMASK_SHIFT(node)		((node << BATCH_SHIFT) & (64 - 1))
#else
#define BITMASKS_PER_64_BIT 	1
#define BITMASK_WORD(node)		node
#define BITMASK_SHIFT(node)		0
#endif

#define MAX_DSTS MAX_NODES  // include dst == out of boundary
#define MAX_SRCS MAX_NODES

// Tracks which srcs/dsts and src/dst racks are available for this batch
struct batch_state {
    bool oversubscribed;
    uint64_t allowed_mask;
    uint64_t src_endnodes [MAX_SRCS / BITMASKS_PER_64_BIT];
    uint64_t dst_endnodes [MAX_DSTS / BITMASKS_PER_64_BIT];
    uint64_t src_rack_bitmaps [MAX_RACKS];
    uint64_t dst_rack_bitmaps [MAX_RACKS];
    uint16_t src_rack_counts [MAX_RACKS * BATCH_SIZE];  // rows are racks
    uint16_t dst_rack_counts [MAX_RACKS * BATCH_SIZE];
    uint16_t out_of_boundary_counts [BATCH_SIZE];
};

// Initialize an admitted bitmap
static inline
void init_batch_state(struct batch_state *state, bool oversubscribed,
                      uint16_t inter_rack_capacity, uint16_t out_of_boundary_capacity,
                      uint16_t num_nodes) {
    assert(state != NULL);
    assert(num_nodes <= MAX_NODES);

    state->oversubscribed = oversubscribed;
    state->allowed_mask = (1ULL << BATCH_SIZE) - 1;

    uint16_t i;
    for (i = 0; i < num_nodes / BITMASKS_PER_64_BIT; i++) {
        state->src_endnodes[i] = ~0ULL;
        state->dst_endnodes[i] = ~0ULL;
    }
    state->dst_endnodes[BITMASK_WORD(OUT_OF_BOUNDARY_NODE_ID)] = ~0ULL;

    if (oversubscribed) {
        for (i = 0; i < (num_nodes >> TOR_SHIFT); i++) {
            state->src_rack_bitmaps[i] = ~0ULL;
            state->dst_rack_bitmaps[i] = ~0ULL;
        }

        for (i = 0; i < MAX_RACKS * BATCH_SIZE; i++) {
            state->src_rack_counts[i] = inter_rack_capacity;
            state->dst_rack_counts[i] = inter_rack_capacity;
        }
    }

    // init out of boundary counts
    for (i = 0; i < BATCH_SIZE; i++)
        state->out_of_boundary_counts[i] = out_of_boundary_capacity;
}

// Returns the available timeslot bitmap for src and dst (lsb is closest timeslot)
static inline
uint64_t get_available_timeslot_bitmap(struct batch_state *state,
		uint16_t src, uint16_t dst)
{
    assert(state != NULL);
    assert(src < MAX_SRCS);
    assert(dst < MAX_DSTS);

    uint64_t endnode_bitmap =
    		  state->allowed_mask
    		& (state->src_endnodes[BITMASK_WORD(src)] >> BITMASK_SHIFT(src))
    		& (state->dst_endnodes[BITMASK_WORD(dst)] >> BITMASK_SHIFT(dst));

    if (SUPPORTS_OVERSUBSCRIPTION && state->oversubscribed) {
        uint64_t rack_bitmap;
        if (dst == OUT_OF_BOUNDARY_NODE_ID)
            rack_bitmap = state->src_rack_bitmaps[fp_rack_from_node_id(src)];
        else
            rack_bitmap = state->src_rack_bitmaps[fp_rack_from_node_id(src)] &
                state->dst_rack_bitmaps[fp_rack_from_node_id(dst)];

        return endnode_bitmap & rack_bitmap;
    }

    return endnode_bitmap;
}

// Sets a timeslot as occupied for src and dst
static inline
void set_timeslot_occupied(struct batch_state *state, uint16_t src,
                           uint16_t dst, uint8_t timeslot) {
    assert(state != NULL);
    assert(src < MAX_SRCS);
    assert(dst < MAX_DSTS);
    assert(timeslot <= BATCH_SIZE);

    state->src_endnodes[BITMASK_WORD(src)] &=
    		~(0x1ULL << (timeslot + BITMASK_SHIFT(src)));

    if (dst == OUT_OF_BOUNDARY_NODE_ID) {
        // destination is outside of scheduling boundary
        state->out_of_boundary_counts[timeslot]--;
        if (state->out_of_boundary_counts[timeslot] == 0)
            state->dst_endnodes[dst] = state->dst_endnodes[dst] & ~(0x1ULL << timeslot);
    }
    else
        state->dst_endnodes[BITMASK_WORD(dst)] &=
        		~(0x1ULL << (timeslot + BITMASK_SHIFT(dst)));

    if (SUPPORTS_OVERSUBSCRIPTION && state->oversubscribed) {
        uint16_t src_rack = fp_rack_from_node_id(src);
        state->src_rack_counts[BATCH_SIZE * src_rack + timeslot] -= 1;
        if (state->src_rack_counts[BATCH_SIZE * src_rack + timeslot] == 0)
            state->src_rack_bitmaps[src_rack] &= ~(0x1ULL << timeslot);

        if (dst != OUT_OF_BOUNDARY_NODE_ID) {
            uint16_t dst_rack = fp_rack_from_node_id(dst);
            state->dst_rack_counts[BATCH_SIZE * dst_rack + timeslot] -= 1;
            if (state->dst_rack_counts[BATCH_SIZE * dst_rack + timeslot] == 0)
                state->dst_rack_bitmaps[dst_rack] &= ~(0x1ULL << timeslot);
        }
    }
}

static inline
void batch_disallow_lsb_timeslot(struct batch_state *state) {
	state->allowed_mask <<= 1;
	state->allowed_mask &= (1ULL << BATCH_SIZE) - 1;
}

#endif /* BATCH_H_ */
