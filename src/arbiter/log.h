/*
 * log.h
 *
 *  Created on: Jun 23, 2013
 *      Author: yonch
 */

#ifndef LOG_H_
#define LOG_H_

#include <stdio.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_byteorder.h>

#include "../benchmark/benchmark_packet.h"

#define LOG_MAX_RX_ENTRIES (1<<23)

#define RTE_LOGTYPE_LOG_H RTE_LOGTYPE_USER1

struct rx_log_entry {
	uint64_t rx_time;		/**< Grant packet RX time */
	uint32_t tx_time_lo;	/**< Low 32-bits of TX time */
	uint16_t pkt_index; 	/**< Packet index (low 16 bits) */
	uint16_t src;   		/**< Transfer source */
	uint16_t dst;   		/**< Transfer destination */
	uint32_t start_tslot;	/**< allocation starting timeslot */
	uint64_t tx_mask;   	/**< The granted mask */
};

struct lcore_log {
	uint32_t n_rx_entries;
	struct rx_log_entry rx[LOG_MAX_RX_ENTRIES];
};

/**
 * \brief Initializes logging structure.
 */
static inline void log_init(struct lcore_log *log);

/**
 * \brief logs the receipt of a packet
 *
 * @param log: the struct lcore_log to update with log entry
 * @param other parameters: see struct rx_log_entry
 */
static inline void log_rx(struct lcore_log *log, uint64_t rx_time,
			uint32_t tx_time_lo, uint16_t pkt_index, uint16_t src,
			uint16_t dst, uint32_t start_tslot, uint64_t tx_mask);

/**
 * \brief dumps log to the given file, and resets the log to a clean state.
 *
 * @param log: the struct lcore_log to dump
 * @param filename: the filename to dump to
 */
static inline void log_dump(struct lcore_log *log, char* filename);

/********************
 * IMPLEMENTATION
 */

static inline void log_init(struct lcore_log *log) {
	log->n_rx_entries = 0;
}

static inline void log_rx(struct lcore_log *log, uint64_t rx_time,
		uint32_t tx_time_lo, uint16_t pkt_index, uint16_t src,
		uint16_t dst, uint32_t start_tslot, uint64_t tx_mask)
{
	uint32_t n_rx_entries;

#undef LOG_H_VERBOSE
#ifdef LOG_H_VERBOSE
	RTE_LOG(INFO, LOG_H, "got packet index %"PRIu16" at time %"PRIu64"\n",
			pkt_index, rx_time);
#endif

	n_rx_entries = log->n_rx_entries;

	if(likely(n_rx_entries < LOG_MAX_RX_ENTRIES)) {
		log->rx[n_rx_entries].rx_time = rx_time;
		log->rx[n_rx_entries].tx_time_lo = tx_time_lo;
		log->rx[n_rx_entries].pkt_index = pkt_index;
		log->rx[n_rx_entries].src = src;
		log->rx[n_rx_entries].dst = dst;
		log->rx[n_rx_entries].start_tslot = start_tslot;
		log->rx[n_rx_entries].tx_mask = tx_mask;

		log->n_rx_entries++;
	} else {
		RTE_LOG(ERR, LOG_H, "ran out of log entry memory (have %d entries)\n",
				n_rx_entries);
	}
}

static inline void log_dump(struct lcore_log *log, char* filename)
{
	FILE *fp;
	uint32_t i;
	int ret;

	// Open file
	fp = fopen(filename, "w");
	if(fp == NULL) {
		RTE_LOG(ERR, LOG_H, "lcore %d could not open file: %s\n",
				rte_lcore_id(), filename);
		return;
	}

	// Write clock frequency
	// Format is: "clock", clock_tick_hz
	ret = fprintf(fp, "clock,%"PRIu64"\n", rte_get_timer_hz());
	if(ret < 0) {
		RTE_LOG(ERR, LOG_H, "lcore %d could not write clock hz\n",
				rte_lcore_id());
	}

	// Write RX log entries
	// Format is: "rx",rx_time,tx_time_lo,pkt_index,src,dst,start_tslot,tx_mask_hex
	for(i = 0; i < log->n_rx_entries; i++) {
		ret = fprintf(fp,
				"rx,%"PRIu64",%"PRIu32",%"PRIu16",%"PRIu16",%"PRIu16
				",%"PRIu32",%"PRIx64"\n", log->rx[i].rx_time,
				log->rx[i].tx_time_lo, log->rx[i].pkt_index, log->rx[i].src,
				log->rx[i].dst, log->rx[i].start_tslot, log->rx[i].tx_mask);
		if(ret < 0) {
			RTE_LOG(ERR, LOG_H, "lcore %d could not dump RX entry %d\n",
					rte_lcore_id(), i);
		}
	}

	// Close file
	ret = fclose(fp);
	if(ret != 0) {
		RTE_LOG(ERR, LOG_H, "lcore %d could not close its log successfully\n",
				rte_lcore_id());
	}

	// reset log
	log_init(log);
}


#endif /* LOG_H_ */
