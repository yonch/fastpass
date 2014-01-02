/*
 * comm_log.h
 *
 *  Created on: Dec 29, 2013
 *      Author: yonch
 */

#ifndef COMM_LOG_H_
#define COMM_LOG_H_

#include <rte_log.h>
#include <rte_lcore.h>
#include <rte_byteorder.h>
#include <rte_cycles.h>

#include "control.h"

/**
 * logged information for a core
 */
struct comm_log {
	uint64_t rx_pkts;
	uint64_t rx_batches;
	uint64_t rx_non_empty_batches;
	uint64_t tx_cannot_alloc_mbuf;
	uint64_t rx_non_ipv4_pkts;
	uint64_t rx_ipv4_non_fastpss_pkts;
	uint64_t tx_pkt;
	uint64_t pktdesc_alloc_failed;
	uint64_t rx_truncated_pkt;
	uint64_t areq_invalid_dst;
	uint64_t demand_increased;
	uint64_t demand_remained;
	uint64_t triggered_send;
	uint64_t dequeue_admitted_failed;
	uint64_t processed_tslots;
	uint64_t non_empty_tslots;
	uint64_t occupied_node_tslots;
};

extern struct comm_log comm_core_logs[RTE_MAX_LCORE];

#define RTE_LOGTYPE_COMM RTE_LOGTYPE_USER1
#define COMM_DEBUG(a...) RTE_LOG(DEBUG, COMM, ##a)

/* current comm log */
#define CL		(&comm_core_logs[rte_lcore_id()])

static inline void comm_log_init(struct comm_log *cl)
{
	memset(cl, 0, sizeof(*cl));
}

static inline void comm_log_processed_batch(int nb_rx, uint64_t rx_time) {
	CL->rx_pkts += nb_rx;
	CL->rx_batches++;
	if (nb_rx > 0) {
		COMM_DEBUG("at %lu: RX batch of %d packets, totals %lu batches, %lu packets\n",
				rx_time, nb_rx, CL->rx_batches, CL->rx_pkts);
		CL->rx_non_empty_batches++;
	}
}

static inline void comm_log_tx_cannot_allocate_mbuf(uint32_t dst_ip) {
	CL->tx_cannot_alloc_mbuf++;
	COMM_DEBUG("core %d could not allocate TX mbuf for packet to IP "
			"0x%Xu\n",rte_lcore_id(), rte_be_to_cpu_32(dst_ip));
}

static inline void comm_log_rx_non_ipv4_packet(uint8_t portid) {
	CL->rx_non_ipv4_pkts++;
	COMM_DEBUG("got non-IPv4 packet on portid %d\n", portid);
}

static inline void comm_log_rx_ip_non_fastpass_pkt(uint8_t portid) {
	CL->rx_ipv4_non_fastpss_pkts++;
	COMM_DEBUG("got an IPv4 non-fastpass packet on portid %d\n", portid);
}

static inline void comm_log_tx_pkt(uint32_t node_id, uint64_t when) {
	CL->tx_pkt++;
	COMM_DEBUG("sending a packet to node ID %u at time %lu\n", node_id, when);
}

static inline void comm_log_pktdesc_alloc_failed(uint32_t node_id) {
	CL->pktdesc_alloc_failed++;
	COMM_DEBUG("failed to allocate a pktdesc for node ID %u\n", node_id);
}

static inline void comm_log_rx_truncated_pkt(uint32_t ip_total_len,
		uint32_t mbuf_len, uint32_t src_ip) {
	CL->rx_truncated_pkt++;
	COMM_DEBUG("packet from IP %08X is %u bytes, too short for its IP length %u\n",
			src_ip, mbuf_len, ip_total_len);
}

static inline void comm_log_areq_invalid_dst(uint32_t requesting_node,
		uint16_t dest) {
	CL->areq_invalid_dst++;
	COMM_DEBUG("received A-REQ from node %u for invalid dst %u\n",
			requesting_node, dest);
}

static inline void comm_log_demand_increased(uint32_t node_id,
		uint32_t dst, uint32_t orig_demand, uint32_t demand, int32_t demand_diff) {
	CL->demand_increased++;
	COMM_DEBUG("demand for flow src %u dst %u increased by %d (from %u to %u)\n",
			node_id, dst, demand_diff, orig_demand, demand);
}

static inline void comm_log_demand_remained(uint32_t node_id, uint32_t dst,
		uint32_t orig_demand, uint32_t demand) {
	CL->demand_remained++;
	COMM_DEBUG("for flow src %u dst %u got demand 0x%X lower than current 0x%X\n",
			node_id, dst, demand, orig_demand);
}

static inline void comm_log_triggered_send(uint32_t node_id) {
	CL->triggered_send++;
	COMM_DEBUG("triggered send to node %u\n", node_id);
}

static inline void comm_log_dequeue_admitted_failed(int rc) {
	CL->dequeue_admitted_failed++;
	COMM_DEBUG("failed to dequeue admitted flows, got error %d\n", rc);
}

static inline void comm_log_got_admitted_tslot(uint16_t size) {
	CL->processed_tslots++;
	if (size > 0) {
		CL->non_empty_tslots++;
		CL->occupied_node_tslots += size;

		COMM_DEBUG("admitted_traffic for %d nodes (tslot %lu, cycle timer %lu)\n",
				size, CL->processed_tslots, rte_get_tsc_cycles());
	}
}


#undef CL

#endif /* COMM_LOG_H_ */
