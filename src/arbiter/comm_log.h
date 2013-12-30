/*
 * comm_log.h
 *
 *  Created on: Dec 29, 2013
 *      Author: yonch
 */

#ifndef COMM_LOG_H_
#define COMM_LOG_H_

#include <rte_log.h>

/**
 * logged information for a core
 */
struct comm_log {
	uint64_t rx_pkts;
	uint64_t rx_batches;
	uint64_t rx_non_empty_batches;
};

#define RTE_LOGTYPE_COMM RTE_LOGTYPE_USER1
#define COMM_DEBUG(...) RTE_LOG(DEBUG, COMM, __VA_ARGS__)

static inline void comm_log_init(struct comm_log *cl)
{
	memset(cl, 0, sizeof(*cl));
}

static inline void comm_log_processed_batch(struct comm_log *cl,
		int nb_rx, uint64_t rx_time) {
	cl->rx_pkts += nb_rx;
	cl->rx_batches++;
	if (nb_rx > 0) {
		COMM_DEBUG("at %lu: RX batch of %d packets, totals %lu batches, %lu packets\n",
				rx_time, nb_rx, cl->rx_batches, cl->rx_pkts);
		cl->rx_non_empty_batches++;
	}
}

#endif /* COMM_LOG_H_ */
