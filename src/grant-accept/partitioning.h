#ifndef PARTITIONING_H_
#define PARTITIONING_H_

#include "../protocol/topology.h"

/* to use a non-multiple-of-8 PARTITION_N_NODES, modify the packing of
 * bitmasks specified by macros in pim.h */
#define PARTITION_N_NODES	128
#define N_PARTITIONS			((MAX_NODES + PARTITION_N_NODES - 1) / PARTITION_N_NODES)

#if (PARTITION_N_NODES >= MAX_NODES)
#define PARTITION_OF(node)		(0)
#define PARTITION_IDX(node)		(node)
#else
#define PARTITION_OF(node)		(node / PARTITION_N_NODES)
#define PARTITION_IDX(node)		(node % PARTITION_N_NODES)
#endif

/**
 * Returns the id of the first node in this partition
 */
static inline
uint16_t first_in_partition(uint16_t partition) {
        return partition * PARTITION_N_NODES;
}

/**
 * Returns the id of the last node in this partition
 */
static inline
uint16_t last_in_partition(uint16_t partition) {
        return (partition == N_PARTITIONS - 1) ? (MAX_NODES - 1)
                : (first_in_partition(partition + 1) - 1);
}

#endif /* PARTITIONING_H_ */
