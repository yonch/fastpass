#ifndef PARTITIONING_H_
#define PARTITIONING_H_

#include "../protocol/topology.h"

#define PARTITION_N_NODES	128
#define N_PARTITIONS			((MAX_NODES + PARTITION_N_NODES - 1) / PARTITION_N_NODES)

#if (PARTITION_N_NODES >= MAX_NODES)
#define PARTITION_OF(node)		(0)
#define PARTITION_IDX(node)		(node)
#else
#define PARTITION_OF(node)		(node / PARTITION_N_NODES)
#define PARTITION_IDX(node)		(node % PARTITION_N_NODES)
#endif

#endif /* PARTITIONING_H_ */
