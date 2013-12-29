#include "comm_core.h"

#include <rte_log.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_byteorder.h>
#include <rte_ip.h>
#include <rte_memcpy.h>
#include "comm_log.h"
#include "main.h"
#include "make_packet.h"
#include "arp.h"
#include "node.h"

/**
 * \brief Performs an allocation for a single request packet, sends
 * 		a reply to the requester
 *
 * Takes ownership of mbuf memory - either sends it or frees it.
 * @param portid: the port out of which to send the packet
 */
static inline void
comm_rx(struct rte_mbuf *m, uint8_t portid)
{
	struct ether_hdr *eth_hdr;
	struct ipv4_hdr *ipv4_hdr;
	uint32_t req_src;

	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
	ipv4_hdr = (struct ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
			     + sizeof(struct ether_hdr));
//	req_pkt = (struct fast_req_pkt *)(rte_pktmbuf_mtod(m, unsigned char *)
//			     + sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));

	if (rte_be_to_cpu_16(eth_hdr->ether_type) == ETHER_TYPE_ARP) {
		print_arp(m, portid);
		send_gratuitous_arp(portid, controller_ip(portid));
		return; // Disregard ARP
	}

	req_src = node_from_node_ip(rte_be_to_cpu_32(ipv4_hdr->src_addr));

	RTE_LOG(INFO, BENCHAPP, "at %lu controller got packet src=0x%"PRIx32
			" dst=0x%"PRIx32"\n", rte_get_timer_cycles(), req_src,
			ipv4_hdr->dst_addr);

	/* free the request packet */
	rte_pktmbuf_free(m);
}

void exec_comm_core(struct comm_core_cmd * cmd)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int i, j, nb_rx;
	uint8_t portid, queueid;
	uint64_t rx_time;
	struct lcore_conf *qconf;
	struct comm_log c_log;

	comm_log_init(&c_log);

	qconf = &lcore_conf[rte_lcore_id()];

	if (qconf->n_rx_queue == 0) {
		RTE_LOG(INFO, BENCHAPP, "lcore %u has nothing to do\n", rte_lcore_id());
		while(1);
	}

	for (i = 0; i < qconf->n_rx_queue; i++) {
		portid = qconf->rx_queue_list[i].port_id;
		queueid = qconf->rx_queue_list[i].queue_id;
		RTE_LOG(INFO, BENCHAPP, "comm_core -- lcoreid=%u portid=%hhu rxqueueid=%hhu\n",
				rte_lcore_id(), portid, queueid);
		send_gratuitous_arp(portid, controller_ip(i));
	}

	while (rte_get_timer_cycles() < cmd->start_time);

	for (i = 0; i < qconf->n_rx_queue; i++) {
		portid = qconf->rx_queue_list[i].port_id;
		send_gratuitous_arp(portid, controller_ip(i));
	}

	while (rte_get_timer_cycles() < cmd->end_time) {
		/*
		 * Read packet from RX queues
		 */
		for (i = 0; i < qconf->n_rx_queue; ++i) {

			portid = qconf->rx_queue_list[i].port_id;
			queueid = qconf->rx_queue_list[i].queue_id;
			nb_rx = rte_eth_rx_burst(portid, queueid, pkts_burst, MAX_PKT_BURST);
			rx_time = rte_get_timer_cycles();

			/* Prefetch first packets */
			for (j = 0; j < PREFETCH_OFFSET && j < nb_rx; j++) {
				rte_prefetch0(rte_pktmbuf_mtod(
						pkts_burst[j], void *));
			}

			/* Prefetch and handle already prefetched packets */
			for (j = 0; j < (nb_rx - PREFETCH_OFFSET); j++) {
				rte_prefetch0(rte_pktmbuf_mtod(pkts_burst[
						j + PREFETCH_OFFSET], void *));
				comm_rx(pkts_burst[j], portid);
			}

			/* handle remaining prefetched packets */
			for (; j < nb_rx; j++) {
				comm_rx(pkts_burst[j], portid);
			}

			comm_log_processed_batch(&c_log, nb_rx, rx_time);
		}

		/* TODO: Process allocated timeslots and send allocation packets */
//		// Make reply packet
//		reply_m = make_packet(portid,
//				ipv4_hdr->dst_addr,
//				ipv4_hdr->src_addr,
//				&eth_hdr->s_addr, &grant_pkt, grant_n_valid_bytes, 0, IPPROTO_FAST_REQ_GRANT);
//
//		/* send reply packet */
//		send_packet_via_queue(reply_m, portid);

		/* Flush queued packets */
		//send_queued_packets(portid);

	}
}

