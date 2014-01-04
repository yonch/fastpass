#include "comm_core.h"

#include <rte_log.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_byteorder.h>
#include <rte_ip.h>
#include <rte_memcpy.h>
#include <rte_string_fns.h>
#include <rte_errno.h>
#include <rte_mempool.h>
#include <rte_timer.h>
#include "control.h"
#include "comm_log.h"
#include "main.h"
#include "arp.h"
#include "node.h"
#include "../protocol/fpproto.h"
#include "../protocol/pacer.h"
#include "../graph-algo/admissible_structures.h"
#include "../graph-algo/admissible_traffic.h"
#include "dpdk-platform.h"
#include "bigmap.h"
#include "admission_core.h"

/* number of elements to keep in the pktdesc local core cache */
#define PKTDESC_MEMPOOL_CACHE_SIZE		256
/* should have as many pktdesc objs as number of in-flight packets */
#define PKTDESC_MEMPOOL_SIZE			(FASTPASS_WND_LEN * MAX_NODES + (N_COMM_CORES - 1) * PKTDESC_MEMPOOL_CACHE_SIZE)

/**
 * Information about an end node
 * @conn: connection state (ACKs, RESET, retransmission, etc)
 * @dst_port: the port where outgoing packets should go to
 * @dst_ether: the destination ethernet address for outgoing packets
 * @dst_ip: the destination IP for outgoing packets
 * @controller_ip: the controller IP outgoing packets should use
 * @pending: a windowed bitmask of which timeslots have allocations not yet sent out
 * @allocs: the destinations of the allocations
 */
struct end_node_state {
	struct fpproto_conn conn;
	uint8_t dst_port;
	struct ether_addr dst_ether;
	uint32_t dst_ip;
	uint32_t controller_ip;

	/* pending allocations */
	struct fp_window pending;
	uint16_t allocs[MAX_NODES];

	/* demands */
	uint32_t demands[MAX_NODES];

	/* timeout timer */
	struct rte_timer timeout_timer;

	/* egress packet timer and pacer */
	struct rte_timer tx_timer;
	struct fp_pacer tx_pacer;
};

/*
 * Per-comm-core state
 * @alloc_enc_space: space used to encode ALLOCs, set to zeros when not inside
 *    the ALLOC code.
 */
struct comm_core_state {
	uint8_t alloc_enc_space[MAX_NODES * MAX_PATHS];
	uint64_t latest_timeslot;
};

/* whether we should output verbose debugging */
bool fastpass_debug;

/* logs */
struct comm_log comm_core_logs[RTE_MAX_LCORE];

/* per-end-node information */
static struct end_node_state end_nodes[MAX_NODES];

/* per-core information */
static struct comm_core_state core_state[RTE_MAX_LCORE];

/* fpproto_pktdesc pool */
struct rte_mempool* pktdesc_pool[NB_SOCKETS];

static void handle_reset(void *param);
static void trigger_request(struct end_node_state *en);
static void trigger_request_voidp(void *param);
static void handle_areq(void *param, u16 *dst_and_count, int n);
static void set_retrans_timer(void *param, u64 when);
static int cancel_retrans_timer(void *param);
static void handle_neg_ack(void *param, struct fpproto_pktdesc *pd);
static inline void tx_end_node(struct rte_timer *timer, void *param);

struct fpproto_ops proto_ops = {
	.handle_reset	= &handle_reset,
	.handle_areq	= &handle_areq,
	//.handle_ack		= &handle_ack,
	.handle_neg_ack	= &handle_neg_ack,
	.trigger_request= &trigger_request_voidp,
	.set_timer		= &set_retrans_timer,
	.cancel_timer	= &cancel_retrans_timer,
};

void comm_init_global_structs(uint64_t first_time_slot)
{
	u32 i;

	fastpass_debug = true;
	uint64_t hz = rte_get_timer_hz();
	uint64_t send_timeout = (uint64_t)((double)hz * CONTROLLER_SEND_TIMEOUT_SECS);
	uint32_t send_cost = hz / NODE_MAX_PKTS_PER_SEC;
	uint32_t max_burst = (uint32_t)(NODE_MAX_BURST * send_cost);
	uint32_t min_trigger_gap = (uint32_t)((double)hz * NODE_MIN_TRIGGER_GAP_SEC);
	uint64_t now = rte_get_timer_cycles();

	COMM_DEBUG("Configuring send timeout to %f seconds: %lu TSC cycles\n",
			CONTROLLER_SEND_TIMEOUT_SECS, send_timeout);

	for (i = 0; i < MAX_NODES; i++) {
		fpproto_init_conn(&end_nodes[i].conn, &proto_ops,&end_nodes[i],
						FASTPASS_RESET_WINDOW_NS, send_timeout);
		wnd_reset(&end_nodes[i].pending, first_time_slot - 1);
		rte_timer_init(&end_nodes[i].timeout_timer);
		rte_timer_init(&end_nodes[i].tx_timer);
		pacer_init_full(&end_nodes[i].tx_pacer, now, send_cost, max_burst,
				min_trigger_gap);
	}
}

/* based on init_mem in main.c */
void comm_init_core(uint16_t lcore_id, uint64_t first_time_slot)
{
	int socketid;
	char s[64];

	socketid = rte_lcore_to_socket_id(lcore_id);

	/* initialize the space for encoding ALLOCs */
	memset(&core_state[lcore_id].alloc_enc_space, 0,
			sizeof(core_state[lcore_id].alloc_enc_space));

	core_state[lcore_id].latest_timeslot = first_time_slot - 1;

	/* initialize mempool for pktdescs */
	if (pktdesc_pool[socketid] == NULL) {
		rte_snprintf(s, sizeof(s), "pktdesc_pool_%d", socketid);
		pktdesc_pool[socketid] =
			rte_mempool_create(s,
				PKTDESC_MEMPOOL_SIZE, /* num elements */
				sizeof(struct fpproto_pktdesc), /* element size */
				PKTDESC_MEMPOOL_CACHE_SIZE, /* cache size */
				0, NULL, NULL, NULL, NULL, /* custom initialization, disabled */
				socketid, 0);
		if (pktdesc_pool[socketid] == NULL)
			rte_exit(EXIT_FAILURE,
					"Cannot init pktdesc pool on socket %d: %s\n", socketid,
					rte_strerror(rte_errno));
		else
			printf("Allocated pktdesc pool on socket %d - %llu bufs\n",
					socketid, (u64)PKTDESC_MEMPOOL_SIZE);
	}
}

void benchmark_cost_of_get_time(void)
{
	uint32_t i;
	uint64_t a,b,c,d;

	/** Timer tests */
	a = fp_get_time_ns();
	c = rte_rdtsc();
	for(i = 0; i < 1000; i++) {
		b = fp_get_time_ns();
		rte_pause();
	}
	d = rte_rdtsc();
	RTE_LOG(INFO, BENCHAPP, "1000 fp_get_time_ns caused %"PRIu64" difference"
			" which is %"PRIu64" TSC cycles\n",
			b - a, d - c);

	a = rte_rdtsc();
	for(i = 0; i < 1000; i++) {
		b = rte_rdtsc();
		rte_pause();
	}
	RTE_LOG(INFO, BENCHAPP, "1000 rte_rdtsc caused %"PRIu64" difference\n",
			b - a);
}

static int cancel_retrans_timer(void *param)
{
	struct end_node_state *en = (struct end_node_state *)param;
	uint16_t node_id = en - end_nodes;

	comm_log_cancel_timer(node_id);
	rte_timer_stop(&en->timeout_timer);
	return 0;
}

static void retrans_timer_func(struct rte_timer *timer, void *param) {
	struct end_node_state *en = (struct end_node_state *)param;
	uint16_t node_id = en - end_nodes;
	uint64_t now = rte_get_timer_cycles();
	(void)timer;

	comm_log_retrans_timer_expired(node_id, now);
	fpproto_handle_timeout(&en->conn, now);
}

static void set_retrans_timer(void *param, u64 when)
{
	struct end_node_state *en = (struct end_node_state *)param;
	uint16_t node_id = en - end_nodes;
	uint64_t now = rte_get_timer_cycles();

	rte_timer_reset_sync(&en->timeout_timer, when - now, SINGLE, rte_lcore_id(),
			retrans_timer_func, param);

	comm_log_set_timer(node_id, when, when - now);
}

static void handle_areq(void *param, u16 *dst_and_count, int n)
{
	int i;
	struct end_node_state *en = (struct end_node_state *)param;
	u16 dst, count;
	u32 demand;
	u32 orig_demand;
	u32 node_id = en - end_nodes;
	s32 demand_diff;
	u32 num_increases = 0;

	for (i = 0; i < n; i++) {
		dst = rte_be_to_cpu_16(dst_and_count[2*i]);
		count = rte_be_to_cpu_16(dst_and_count[2*i + 1]);
		if (unlikely(dst > MAX_NODES)) {
			comm_log_areq_invalid_dst(node_id, dst);
			return;
		}

		orig_demand = en->demands[dst];
		demand = orig_demand - (1UL << 15);
		demand += (count - demand) & 0xFFFF;
		demand_diff = (s32)demand - (s32)orig_demand;
		if (demand_diff > 0) {
			comm_log_demand_increased(node_id, dst, orig_demand, demand, demand_diff);
			add_backlog(&g_admissible_status, node_id, dst, demand_diff);
			en->demands[dst] = demand;
			num_increases++;
		} else {
			comm_log_demand_remained(node_id, dst, orig_demand, demand);
		}
	}

	if (num_increases > 0)
		trigger_request(en);
}

static void handle_reset(void *param)
{
	struct end_node_state *en = (struct end_node_state *)param;
	uint16_t node_id = en - end_nodes;

	comm_log_handle_reset(node_id, en->conn.in_sync);

	reset_sender(&g_admissible_status, node_id);
	memset(&en->demands[0], 0, MAX_NODES * sizeof(uint32_t));
}

static void handle_neg_ack(void *param, struct fpproto_pktdesc *pd)
{
	struct end_node_state *en = (struct end_node_state *)param;
	uint16_t node_id = en - end_nodes;
	uint32_t total_timeslots = 0;
	uint16_t dst;
	uint16_t dst_count;
	int i;

	for (i = 0; i < pd->n_dsts; i++) {
		dst = pd->dsts[i] % NUM_NODES;
		dst_count = pd->dst_counts[i];
		add_backlog(&g_admissible_status, node_id, dst, dst_count);
		total_timeslots += dst_count;
		comm_log_neg_ack_increased_backlog(node_id, dst, dst_count, pd->seqno);
	}

	comm_log_neg_ack(node_id, pd->n_dsts, total_timeslots, pd->seqno);

	fpproto_pktdesc_free(pd);
}

static void trigger_request_voidp(void *param)
{
	struct end_node_state *en = (struct end_node_state *)param;
	trigger_request(en);
}


static void trigger_request(struct end_node_state *en)
{
	uint64_t now = rte_get_timer_cycles();
	u32 node_id = en - end_nodes;

	if (pacer_trigger(&en->tx_pacer, now)) {
		rte_timer_reset_sync(&en->tx_timer,
				pacer_next_event(&en->tx_pacer) - now, SINGLE, rte_lcore_id(),
				tx_end_node, (void *)en);

		comm_log_triggered_send(node_id);
	}
}

static inline struct rte_mbuf *
make_packet(struct end_node_state *en, struct fpproto_pktdesc *pd)
{
	const unsigned int socket_id = rte_socket_id();
	struct rte_mbuf *m;
	struct ether_hdr *eth_hdr;
	struct ipv4_hdr *ipv4_hdr;
	unsigned char *payload_ptr;
	uint32_t ipv4_length;
	uint32_t data_len;

	// Allocate packet on the current socket
	m = rte_pktmbuf_alloc(tx_pktmbuf_pool[socket_id]);
	if(m == NULL) {
		comm_log_tx_cannot_allocate_mbuf(en->dst_ip);
		return NULL;
	}

	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);

	ipv4_hdr = (struct ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
			     + sizeof(struct ether_hdr));

	payload_ptr = (rte_pktmbuf_mtod(m, unsigned char *)
			     + sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));

	/* dst addr according to destination */
	ether_addr_copy(&en->dst_ether, &eth_hdr->d_addr);
	/* src addr according to output port*/
	ether_addr_copy(&port_info[en->dst_port].eth_addr, &eth_hdr->s_addr);
	/* ethernet payload is IPv4 */
	eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

	/* ipv4 header */
	ipv4_hdr->version_ihl = 0x45; // Version=4, IHL=5
	ipv4_hdr->type_of_service = 0;
	ipv4_hdr->packet_id = 0;
	ipv4_hdr->fragment_offset = 0;
	ipv4_hdr->time_to_live = 77;
	ipv4_hdr->next_proto_id = IPPROTO_FASTPASS;
	// ipv4_hdr->hdr_checksum will be calculated in HW
	ipv4_hdr->src_addr = en->controller_ip;
	ipv4_hdr->dst_addr = en->dst_ip;

	/* encode fastpass payload */
	data_len = fpproto_encode_packet(&en->conn, pd, payload_ptr,
			FASTPASS_MAX_PAYLOAD, en->controller_ip, en->dst_ip);

	/* adjust packet size */
	ipv4_length = sizeof(struct ipv4_hdr) + data_len;
	// ipv4_length = RTE_MAX(46u, ipv4_length);
	rte_pktmbuf_append(m, ETHER_HDR_LEN + ipv4_length);
	ipv4_hdr->total_length = rte_cpu_to_be_16(ipv4_length);

	// Activate IP checksum offload for packet
	m->ol_flags |= PKT_TX_IP_CKSUM;
	m->pkt.vlan_macip.f.l2_len = sizeof(struct ether_hdr);
	m->pkt.vlan_macip.f.l3_len = sizeof(struct ipv4_hdr);
	ipv4_hdr->hdr_checksum = 0;

	return m;
}

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
	u8 *req_pkt;
	uint32_t req_src;
	struct end_node_state *en;
	uint16_t ether_type;
	uint16_t ip_total_len;

	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
	ipv4_hdr = (struct ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
			     + sizeof(struct ether_hdr));
	req_pkt = (rte_pktmbuf_mtod(m, unsigned char *)
			     + sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));

	ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);

	if (unlikely(ether_type == ETHER_TYPE_ARP)) {
		print_arp(m, portid);
		send_gratuitous_arp(portid, controller_ip(portid));
		goto cleanup; // Disregard ARP
	}

	if (unlikely(ether_type != ETHER_TYPE_IPv4)) {
		comm_log_rx_non_ipv4_packet(portid);
		goto cleanup;
	}

	if (unlikely(ipv4_hdr->next_proto_id != IPPROTO_FASTPASS)) {
		comm_log_rx_ip_non_fastpass_pkt(portid);
		goto cleanup;
	}

	ip_total_len = rte_be_to_cpu_16(ipv4_hdr->total_length);

	if (unlikely(sizeof(struct ether_hdr) + ip_total_len > rte_pktmbuf_data_len(m))) {
		comm_log_rx_truncated_pkt(ip_total_len, rte_pktmbuf_data_len(m),
				ipv4_hdr->src_addr);
		goto cleanup;
	}


	req_src = node_from_node_ip(rte_be_to_cpu_32(ipv4_hdr->src_addr));
	en = &end_nodes[req_src];

	/* copy most recent ethernet and IP addresses, for return packets */
	ether_addr_copy(&eth_hdr->s_addr, &en->dst_ether);
	en->dst_ip = ipv4_hdr->src_addr;
	en->controller_ip = ipv4_hdr->dst_addr;


	COMM_DEBUG("at %lu controller got packet src_ip=0x%"PRIx32
			" src_node=%u dst=0x%"PRIx32" ip_len=%u\n", rte_get_timer_cycles(),
			ipv4_hdr->src_addr, req_src, ipv4_hdr->dst_addr, ip_total_len);


	if (req_src < MAX_NODES) {
		fpproto_handle_rx_packet(&end_nodes[req_src].conn, req_pkt,
				ip_total_len - 4 * (ipv4_hdr->version_ihl & 0xF),
				ipv4_hdr->src_addr, ipv4_hdr->dst_addr);
	}

cleanup:
	/* free the request packet */
	rte_pktmbuf_free(m);
}

/*
 * Read packets from RX queues
 */
static inline void do_rx_burst(struct lcore_conf* qconf)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int i, j, nb_rx;
	uint8_t portid;
	uint8_t queueid;
	uint64_t rx_time;

	for (i = 0; i < qconf->n_rx_queue; ++i) {
		portid = qconf->rx_queue_list[i].port_id;
		queueid = qconf->rx_queue_list[i].queue_id;
		nb_rx = rte_eth_rx_burst(portid, queueid, pkts_burst, MAX_PKT_BURST);
		rx_time = fp_get_time_ns();

		/* Prefetch first packets */
		for (j = 0; j < PREFETCH_OFFSET && j < nb_rx; j++) {
			rte_prefetch0(rte_pktmbuf_mtod(pkts_burst[j], void *));
		}

		/* Prefetch and handle already prefetched packets */
		for (j = 0; j < (nb_rx - PREFETCH_OFFSET); j++) {
			rte_prefetch0(
					rte_pktmbuf_mtod(pkts_burst[ j + PREFETCH_OFFSET], void *));
			comm_rx(pkts_burst[j], portid);
		}

		/* handle remaining prefetched packets */
		for (; j < nb_rx; j++) {
			comm_rx(pkts_burst[j], portid);
		}

		comm_log_processed_batch(nb_rx, rx_time);
	}
}

static inline void process_allocated_traffic(struct comm_core_state *core,
		struct rte_ring *q_admitted)
{
	int rc;
	int i, j;
	struct admitted_traffic* admitted[MAX_ADMITTED_PER_LOOP];
	uint64_t current_timeslot;
	struct end_node_state *en;
	struct fp_window *wnd;
	uint16_t src;
	uint16_t dst;
	u64 tslot;
	int32_t gap;

	/* Process newly allocated timeslots */
	rc = rte_ring_dequeue_burst(q_admitted, (void **) &admitted[0],
								MAX_ADMITTED_PER_LOOP);
	if (unlikely(rc < 0)) {
		/* error in dequeuing.. should never happen?? */
		comm_log_dequeue_admitted_failed(rc);
		return;
	}

	for (i = 0; i < rc; i++) {
		current_timeslot = ++core->latest_timeslot;
		comm_log_got_admitted_tslot(admitted[i]->size, current_timeslot);
		for (j = 0; j < admitted[i]->size; j++) {
			/* process this node's allocation */
			src = admitted[i]->edges[j].src;
			dst = admitted[i]->edges[j].dst;

			/* get the node's structure */
			en = &end_nodes[src];
			wnd = &en->pending;

			/* are there timeslots sliding out of the window? */
			tslot = current_timeslot - FASTPASS_WND_LEN;
			tslot = time_before64(wnd_head(wnd), tslot) ? wnd_head(wnd) : tslot;
			while ((gap = wnd_at_or_before(wnd, tslot)) >= 0) {
				tslot -= gap;
				uint16_t thrown_alloc = en->allocs[wnd_pos(tslot)];
				/* throw away that timeslot, but make sure we reallocate */
				add_backlog(&g_admissible_status, src,
						thrown_alloc % MAX_NODES, 1);
				wnd_clear(wnd, tslot);

				/* also log them */
				comm_log_alloc_fell_off_window(tslot, current_timeslot, src,
						thrown_alloc);
			}

			/* advance the window */
			wnd_advance(wnd, current_timeslot - wnd_head(wnd));
			COMM_DEBUG("advanced window flow %lu. current %lu head %llu\n",
					en - end_nodes, current_timeslot, wnd_head(wnd));

			/* add the allocation */
			wnd_mark(wnd, current_timeslot);
			en->allocs[wnd_pos(current_timeslot)] = dst;

			/* trigger a packet */
			trigger_request(en);
		}
	}
	/* free memory */
	rte_mempool_put_bulk(admitted_traffic_pool[0], (void **) admitted, rc);
}

/* check statically that the window is not too long, because fill_packet_alloc
 * cannot handle gaps larger than 256 */
struct __static_check_wnd_size {
	uint8_t check_FASTPASS_WND_is_not_too_big_for__fill_packet_alloc[256 - FASTPASS_WND_LEN];
};

/**
 * Extracts allocations from the end-node @en into the packet desc @pd
 */
static inline void fill_packet_alloc(struct comm_core_state *core,
		struct fpproto_pktdesc *pd, struct end_node_state *en)
{
	uint16_t n_dsts = 0;
	uint16_t n_tslot = 0;
	struct fp_window *wnd = &en->pending;
	uint64_t prev_tslot;
	uint64_t cur_tslot;
	uint16_t gap;
	uint16_t skip16;
	uint16_t index;
	uint16_t dst;
	uint16_t i;

	if (wnd_empty(wnd))
		goto out;

	cur_tslot = wnd_earliest_marked(wnd);
	prev_tslot = (cur_tslot - 1) & (~0ULL << 4);
	pd->base_tslot = (prev_tslot >> 4) & 0xFFFF;

next_alloc:
	gap = cur_tslot - prev_tslot;

	/* do we need to insert a skip byte? */
	if (gap > 16) {
		skip16 = (gap - 1) / 16;
		pd->tslot_desc[n_tslot++] = skip16  - 1;
		gap -= 16 * skip16;
	}

	/* find the destination for this flow */
	dst = en->allocs[wnd_pos(cur_tslot)];
	index = (dst % NUM_NODES) + NUM_NODES * (dst >> 14);

	if (core->alloc_enc_space[index] == 0) {
		/* this is the first time seeing dst, need to add it to pd->dsts */
		if (n_dsts == 15) {
			/* too many destinations already, we're done */
			goto cleanup;
		} else {
			/* get the next slot in the pd->dsts array */
			pd->dsts[n_dsts] = dst;
			pd->dst_counts[n_dsts] = 0;
			core->alloc_enc_space[index] = n_dsts;
			n_dsts++;
		}
	}

	/* encode the allocation byte */
	pd->tslot_desc[n_tslot++] =
			((core->alloc_enc_space[index] + 1) << 4) | (gap - 1);
	pd->dst_counts[core->alloc_enc_space[index]]++;

	/* unmark the timeslot */
	wnd_clear(wnd, cur_tslot);

	if (likely(!wnd_empty(wnd)
				&& (n_tslot <= FASTPASS_PKT_MAX_ALLOC_TSLOTS - 2))) {
		prev_tslot = cur_tslot;
		cur_tslot = wnd_earliest_marked(wnd);
		goto next_alloc;
	}
cleanup:
	/* we set core->alloc_enc_space back to zeros */
	for (i = 0; i < n_dsts; i++) {
		dst = pd->dsts[i];
		index = (dst % NUM_NODES) + NUM_NODES * (dst >> 14);
		core->alloc_enc_space[index] = 0;
	}

	/* pad to even n_tslot */
	if (n_tslot & 1)
		pd->tslot_desc[n_tslot++] = 0;

out:
	pd->n_dsts = n_dsts;
	pd->alloc_tslot = n_tslot;
	assert((pd->alloc_tslot & 1) == 0);
}

static inline void tx_end_node(struct rte_timer *timer, void *param)
{
	struct end_node_state *en = (struct end_node_state *)param;
	const unsigned lcore_id = rte_lcore_id();
	struct comm_core_state *core = &core_state[lcore_id];
	uint32_t node_ind = en - end_nodes;
	struct rte_mbuf *out_pkt;
	struct fpproto_pktdesc *pd;
	u64 now;

	(void)timer;

	/* clear the trigger */
	pacer_reset(&en->tx_pacer);

	/* prepare to send */
	fpproto_prepare_to_send(&en->conn);

	/* allocate pktdesc */
	pd = fpproto_pktdesc_alloc();
	if (unlikely(pd == NULL)) {
		comm_log_pktdesc_alloc_failed(node_ind);
		/* retry later */
		trigger_request(en);
		return;
	}

	/* fill in allocated timeslots */
	fill_packet_alloc(core, pd, en);

	/* we want this packet's reliability to be tracked */
	now = rte_get_timer_cycles();
	fpproto_commit_packet(&en->conn, pd, now);

	/* make the packet */
	out_pkt = make_packet(en, pd);
	if (unlikely(out_pkt == NULL))
		return; /* pd committed, will get retransmitted on timeout */

	/* send on port */
	send_packet_via_queue(out_pkt, en->dst_port);

	/* log sent packet */
	comm_log_tx_pkt(node_ind, now);
}

void exec_comm_core(struct comm_core_cmd * cmd)
{
	int i;
	uint8_t portid, queueid;
	struct lcore_conf *qconf;
	const unsigned lcore_id = rte_lcore_id();
	struct comm_core_state *core = &core_state[lcore_id];

	qconf = &lcore_conf[lcore_id];

	comm_log_init(&comm_core_logs[lcore_id]);

	if (qconf->n_rx_queue == 0) {
		RTE_LOG(INFO, BENCHAPP, "lcore %u has nothing to do\n", rte_lcore_id());
		while(1);
	}

	COMM_DEBUG("starting, current timeslot %lu\n", core->latest_timeslot);

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

	/* MAIN LOOP */
	while (rte_get_timer_cycles() < cmd->end_time) {
		/* read packets from RX queues */
		do_rx_burst(qconf);

		/* Process newly allocated timeslots */
		process_allocated_traffic(core, cmd->q_admitted);

		/* process timers */
		rte_timer_manage();

		/* Flush queued packets */
		for (i = 0; i < n_enabled_port; i++)
			send_queued_packets(enabled_port[i]);
	}
}

