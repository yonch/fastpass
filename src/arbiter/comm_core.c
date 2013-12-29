#include "controller.h"

#include <rte_log.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_byteorder.h>
#include <rte_ip.h>
#include <rte_memcpy.h>
#include "fast_packet.h"
#include "main.h"
#include "make_packet.h"
#include "arp.h"
#include "../allocator/fast_path_allocator.h"
#include "node.h"

/**
 * \brief Performs an allocation for a single request packet, sends
 * 		a reply to the requester
 *
 * Takes ownership of mbuf memory - either sends it or frees it.
 * @param portid: the port out of which to send the packet
 * @param tslot: current allocation time slot index
 */
static inline void
do_controller_allocation(struct rte_mbuf *m, uint8_t portid, uint32_t tslot)
{
	struct ether_hdr *eth_hdr;
	struct ipv4_hdr *ipv4_hdr;
	struct fast_req_pkt *req_pkt;
	struct rte_mbuf *reply_m; /**< Reply packet */
	uint32_t req_src;
	struct fast_grant_pkt grant_pkt;
	uint32_t grant_n_valid_bytes;
	struct allocation flow_alloc;

	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
	ipv4_hdr = (struct ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
			     + sizeof(struct ether_hdr));
	req_pkt = (struct fast_req_pkt *)(rte_pktmbuf_mtod(m, unsigned char *)
			     + sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));

	if (rte_be_to_cpu_16(eth_hdr->ether_type) == ETHER_TYPE_ARP) {
		print_arp(m, portid);
		send_gratuitous_arp(portid, controller_ip(portid));
		return; // Disregard ARP
	}

	req_src = node_from_node_ip(rte_be_to_cpu_32(ipv4_hdr->src_addr));

#ifdef MAIN_C_VERBOSE
	RTE_LOG(INFO, BENCHAPP, "at %lu controller got packet src=%"PRIu32" dst=%"PRIu32
			" cookie=0x%"PRIx64"\n", rte_get_timer_cycles(), req_src,
			req_pkt->dest, req_pkt->req_cookie);
#endif

	FastPathAllocator_allocateFlow(req_src, req_pkt->dest, req_pkt->data_size, &flow_alloc);

	grant_pkt.tx_mask = flow_alloc.tx_mask;
	rte_memcpy(&grant_pkt.paths[0], &flow_alloc.paths[0], flow_alloc.num_allocated);
	grant_pkt.req_cookie = req_pkt->req_cookie;
	grant_pkt.start_time = tslot;

	grant_n_valid_bytes =
		(uint32_t)(uint64_t)&((struct fast_grant_pkt *)0)->paths[flow_alloc.num_allocated];

	// Make reply packet
	reply_m = make_packet(portid,
			ipv4_hdr->dst_addr,
			ipv4_hdr->src_addr,
			&eth_hdr->s_addr, &grant_pkt, grant_n_valid_bytes, 0, IPPROTO_FAST_REQ_GRANT);

	/* send reply packet */
	send_packet_via_queue(reply_m, portid);

	/* free the request packet */
	rte_pktmbuf_free(m);
}

void exec_controller(struct expt_controller * cmd)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	uint64_t tslot_len = cmd->tslot_len;
	int i, j, nb_rx;
	uint8_t portid, queueid;
	uint64_t rx_time;
	struct lcore_conf *qconf;
	uint32_t cur_tslot = cmd->tslot_offset;
	uint64_t cur_tslot_time;
	int res;
	uint32_t n_processed = 0;

	qconf = &lcore_conf[rte_lcore_id()];

	/* Initialize allocator */
	res = FastPathAllocator_init(cmd->tor_n_machines, cmd->agg_n_ports,
			cmd->core_n_ports, cmd->alloc_n_paths);
	if (res != 1) {
		rte_exit(-1, "Allocator init failed! (n_machines,agg,core)="
				"(%d,%d,%d) n_paths=%d\n", cmd->tor_n_machines,
				cmd->agg_n_ports, cmd->core_n_ports,
				cmd->alloc_n_paths);
 	}

	cur_tslot_time = cmd->start_time;

	if (qconf->n_rx_queue == 0) {
		RTE_LOG(INFO, BENCHAPP, "lcore %u has nothing to do\n", rte_lcore_id());
		while(1);
	}

	for (i = 0; i < qconf->n_rx_queue; i++) {
		portid = qconf->rx_queue_list[i].port_id;
		queueid = qconf->rx_queue_list[i].queue_id;
		RTE_LOG(INFO, BENCHAPP, "controller -- lcoreid=%u portid=%hhu rxqueueid=%hhu\n",
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

			/* update current time slot if necessary */
			while (rx_time >= cur_tslot_time + tslot_len) {
				cur_tslot_time += tslot_len;
				cur_tslot++;
				FastPathAllocator_updateOnTimer();
			}

			/* Prefetch first packets */
			for (j = 0; j < PREFETCH_OFFSET && j < nb_rx; j++) {
				rte_prefetch0(rte_pktmbuf_mtod(
						pkts_burst[j], void *));
			}

			/* Prefetch and handle already prefetched packets */
			for (j = 0; j < (nb_rx - PREFETCH_OFFSET); j++) {
				rte_prefetch0(rte_pktmbuf_mtod(pkts_burst[
						j + PREFETCH_OFFSET], void *));
				do_controller_allocation(pkts_burst[j], portid, cur_tslot);
			}

			/* handle remaining prefetched packets */
			for (; j < nb_rx; j++) {
				do_controller_allocation(pkts_burst[j], portid, cur_tslot);
			}

//			for (j = 0; j < nb_rx; j++) {
//				log_rx(&lcore_log[rte_lcore_id()], rx_time, portid, pkts_burst[j]);
//			}

			/* Flush queued packets */
			send_queued_packets(portid);
			n_processed += nb_rx;
		}
	}

	//log_dump(&lcore_log[rte_lcore_id()], cmd->log_rx_cmd.filename);

	/* Before leaving, clean up allocator */
	FastPathAllocator_destroy();

	RTE_LOG(INFO, BENCHAPP, "Controller processed %"PRIu32" packets\n",
			n_processed);
}

