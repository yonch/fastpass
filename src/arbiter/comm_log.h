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
#include "../protocol/platform.h"
#include "dpdk-time.h"

/**
 * logged information for a core
 */
struct comm_log {
	uint64_t rx_pkts;
	uint64_t rx_bytes;
	uint64_t rx_batches;
	uint64_t rx_non_empty_batches;
	uint64_t tx_cannot_alloc_mbuf;
	uint64_t rx_non_ipv4_pkts;
	uint64_t rx_ipv4_non_fastpss_pkts;
	uint64_t rx_watchdog_pkts;
	uint64_t tx_watchdog_pkts;
	uint64_t tx_pkt;
	uint64_t tx_bytes;
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
	uint64_t alloc_fell_off_window;
	uint64_t handle_reset;
	uint64_t timer_cancel;
	uint64_t timer_set;
	uint64_t retrans_timer_expired;
	uint64_t neg_acks_without_alloc;
	uint64_t neg_acks_with_alloc;
	uint64_t neg_ack_destinations;
	uint64_t neg_ack_timeslots;
	uint64_t error_encoding_packet;
	uint64_t flush_buffer_in_add_backlog;
	uint64_t neg_ack_triggered_reports;
	uint64_t reports_triggered;
	uint64_t total_demand;
	uint64_t acks_without_alloc;
	uint64_t acks_with_alloc;
	uint64_t total_acked_timeslots;
	uint64_t dropped_rx_due_to_deadline;
	uint64_t failed_to_allocate_watchdog;
	uint64_t failed_to_burst_watchdog;
};

extern struct comm_log comm_core_logs[RTE_MAX_LCORE];

#define RTE_LOGTYPE_COMM RTE_LOGTYPE_USER1

#ifdef CONFIG_IP_FASTPASS_DEBUG
#define COMM_DEBUG(a...) RTE_LOG(DEBUG, COMM, ##a)
#else
#define COMM_DEBUG(a...)
#endif


/* current comm log */
#define CL		(&comm_core_logs[rte_lcore_id()])

static inline void comm_log_init(struct comm_log *cl)
{
	memset(cl, 0, sizeof(*cl));
}

static inline void comm_log_processed_batch(int nb_rx, uint64_t rx_time) {
	(void)rx_time;
	CL->rx_pkts += nb_rx;
	CL->rx_batches++;
	if (nb_rx > 0) {
		COMM_DEBUG("at %lu: RX batch of %d packets, totals %lu batches, %lu packets\n",
				rx_time, nb_rx, CL->rx_batches, CL->rx_pkts);
		CL->rx_non_empty_batches++;
	}
}

static inline void comm_log_rx_pkt(uint32_t size) {
	CL->rx_bytes += size;
}

static inline void comm_log_tx_cannot_allocate_mbuf(uint32_t dst_ip) {
	(void)dst_ip;
	CL->tx_cannot_alloc_mbuf++;
	COMM_DEBUG("core %d could not allocate TX mbuf for packet to IP "
			"0x%Xu\n",rte_lcore_id(), rte_be_to_cpu_32(dst_ip));
}

static inline void comm_log_error_encoding_packet(uint32_t dst_ip,
		uint16_t node_id, int32_t error_code) {
	(void)dst_ip; (void)node_id; (void)error_code;
	CL->error_encoding_packet++;
	COMM_DEBUG("error encoding packet for node_id %d got error %d\n",
			node_id, error_code);
}

static inline void comm_log_rx_non_ipv4_packet(uint8_t portid) {
	(void)portid;
	CL->rx_non_ipv4_pkts++;
	COMM_DEBUG("got non-IPv4 packet on portid %d\n", portid);
}

static inline void comm_log_rx_watchdog_packet(uint8_t portid) {
	(void)portid;
	CL->rx_watchdog_pkts++;
	COMM_DEBUG("got watchdog packet on portid %d\n", portid);
}

static inline void comm_log_rx_ip_non_fastpass_pkt(uint8_t portid) {
	(void)portid;
	CL->rx_ipv4_non_fastpss_pkts++;
	COMM_DEBUG("got an IPv4 non-fastpass packet on portid %d\n", portid);
}

static inline void comm_log_tx_pkt(uint32_t node_id, uint64_t when,
		uint32_t n_bytes) {
	(void)node_id;
	(void)when;
	CL->tx_pkt++;
	CL->tx_bytes += n_bytes;
	COMM_DEBUG("sending a packet to node ID %u at time %lu\n", node_id, when);
}

static inline void comm_log_pktdesc_alloc_failed(uint32_t node_id) {
	(void)node_id;
	CL->pktdesc_alloc_failed++;
	COMM_DEBUG("failed to allocate a pktdesc for node ID %u\n", node_id);
}

static inline void comm_log_rx_truncated_pkt(uint32_t ip_total_len,
		uint32_t mbuf_len, uint32_t src_ip) {
	(void)ip_total_len;(void)mbuf_len;(void)src_ip;
	CL->rx_truncated_pkt++;
	COMM_DEBUG("packet from IP %08X is %u bytes, too short for its IP length %u\n",
			src_ip, mbuf_len, ip_total_len);
}

static inline void comm_log_areq_invalid_dst(uint32_t requesting_node,
		uint16_t dest) {
	(void)requesting_node;(void)dest;
	CL->areq_invalid_dst++;
	COMM_DEBUG("received A-REQ from node %u for invalid dst %u\n",
			requesting_node, dest);
}

static inline void comm_log_demand_increased(uint32_t node_id,
		uint32_t dst, uint32_t orig_demand, uint32_t demand, int32_t demand_diff) {
	(void)node_id;(void)dst;(void)orig_demand;(void)demand;(void)demand_diff;
	CL->demand_increased++;
	CL->total_demand += demand_diff;
	COMM_DEBUG("demand for flow src %u dst %u increased by %d (from %u to %u)\n",
			node_id, dst, demand_diff, orig_demand, demand);
}

static inline void comm_log_demand_remained(uint32_t node_id, uint32_t dst,
		uint32_t orig_demand, uint32_t demand) {
	(void)node_id;(void)dst;(void)orig_demand;(void)demand;
	CL->demand_remained++;
	COMM_DEBUG("for flow src %u dst %u got demand 0x%X lower than current 0x%X\n",
			node_id, dst, demand, orig_demand);
}

static inline void comm_log_triggered_send(uint32_t node_id) {
	(void)node_id;
	CL->triggered_send++;
	COMM_DEBUG("triggered send to node %u\n", node_id);
}

static inline void comm_log_dequeue_admitted_failed(int rc) {
	(void)rc;
	CL->dequeue_admitted_failed++;
	COMM_DEBUG("failed to dequeue admitted flows, got error %d\n", rc);
}

static inline void comm_log_got_admitted_tslot(uint16_t size, uint64_t timeslot,
                                               uint16_t partition) {
	(void)size;(void)timeslot;
        if (partition == 0)
                CL->processed_tslots++;
	if (size > 0) {
		CL->non_empty_tslots++;
		CL->occupied_node_tslots += size;

#ifdef CONFIG_IP_FASTPASS_DEBUG
		uint64_t now = fp_get_time_ns(); /* TODO: disable this */
		COMM_DEBUG("admitted_traffic for %d nodes (tslot %lu, now %lu, diff_tslots %ld, counter %lu)\n",
				size, timeslot, now,
				(int64_t)(timeslot - ((now * TIMESLOT_MUL) >> TIMESLOT_SHIFT)),
				CL->processed_tslots);
#endif
	}
}

static inline void comm_log_alloc_fell_off_window(uint64_t thrown_tslot,
		uint64_t current_timeslot, uint16_t src, uint16_t thrown_alloc) {
	(void)thrown_tslot;(void)current_timeslot;(void) src;(void)thrown_alloc;
	CL->alloc_fell_off_window++;
	COMM_DEBUG("alloc at tslot %lu from 0x%X to 0x%X still not sent at timeslot %lu\n",
			thrown_tslot, src, thrown_alloc, current_timeslot);
}

static inline void comm_log_handle_reset(uint16_t node_id, int in_sync) {
	(void)node_id;(void)in_sync;
	CL->handle_reset++;
	COMM_DEBUG("applying reset for node %d in_sync=%d\n", node_id, in_sync);
}

static inline void comm_log_cancel_timer(uint16_t node_id) {
	(void)node_id;
	CL->timer_cancel++;
	COMM_DEBUG("cancel_timer node %d\n", node_id);
}

static inline void comm_log_set_timer(uint16_t node_id, uint64_t when,
		uint64_t gap) {
	(void)node_id;(void)when;(void)gap;
	CL->timer_set++;
	COMM_DEBUG("set_timer node %d at %lu (in %lu cycles)\n", node_id, when, gap);
}

static inline void comm_log_retrans_timer_expired(uint16_t node_id,
		uint64_t now) {
	(void)node_id;(void)now;
	CL->retrans_timer_expired++;
	COMM_DEBUG("retrans_timer for node %d expired at %lu\n", node_id, now);
}

static inline void comm_log_neg_ack_increased_backlog(uint16_t src,
		uint16_t dst, uint16_t amount, uint64_t seqno)
{
	(void)src;(void)dst;(void)amount;(void)seqno;
	COMM_DEBUG("increased backlog from node %d to %d by %d due to neg ack of seqno 0x%lX\n",
			src, dst, amount, seqno);
}

static inline void comm_log_neg_ack(uint16_t src, uint16_t n_dsts,
		uint32_t n_tslots, uint64_t seqno, uint32_t num_triggered) {
	(void)src;(void)n_dsts;(void)n_tslots;(void)seqno;(void)num_triggered;
	if (n_dsts == 0) {
		CL->neg_acks_without_alloc++;
		return;
	}
	CL->neg_acks_with_alloc++;
	CL->neg_ack_destinations += n_dsts;
	CL->neg_ack_timeslots += n_tslots;
	CL->neg_ack_triggered_reports += num_triggered;
	COMM_DEBUG("neg ack node %d seqno %lX triggered %u reports and affected %d dsts %u timeslots\n",
			src, seqno, n_dsts, n_tslots);
}

static inline void comm_log_ack(uint16_t src, uint16_t n_dsts,
		uint32_t n_tslots, uint64_t seqno) {
	(void)src;(void)n_dsts;(void)n_tslots;(void)seqno;
	if (n_dsts == 0) {
		CL->acks_without_alloc++;
		return;
	}
	CL->acks_with_alloc++;
	CL->total_acked_timeslots += n_tslots;
	COMM_DEBUG("ack node %d seqno %lX affected %d dsts %u timeslots\n",
			src, seqno, n_dsts, n_tslots);
}

static inline void comm_log_triggered_report(uint16_t src, uint16_t dst) {
	(void)src;(void)dst;
	CL->reports_triggered++;
	COMM_DEBUG("triggered report of total allocs from %d to %d\n", src, dst);
}

static inline void comm_log_flushed_buffer_in_add_backlog() {
	CL->flush_buffer_in_add_backlog++;
}

static inline void comm_log_dropped_rx_passed_deadline() {
	CL->dropped_rx_due_to_deadline++;
}

static inline void comm_log_failed_to_allocate_watchdog() {
	CL->failed_to_allocate_watchdog++;
}

static inline void comm_log_failed_to_burst_watchdog() {
	CL->failed_to_burst_watchdog++;
}

static inline void comm_log_sent_watchdog() {
	CL->tx_watchdog_pkts++;
}

#undef CL

#endif /* COMM_LOG_H_ */
