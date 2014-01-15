/*
 * main.h
 *
 *  Created on: Sep 26, 2013
 *      Author: yonch
 */

#ifndef MAIN_H_
#define MAIN_H_

#include <rte_config.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>

#define RTE_LOGTYPE_BENCHAPP RTE_LOGTYPE_USER1

#define NB_SOCKETS 8

#define MAX_PORTS 32

/* limits on number of queues per entity */
#define MAX_RX_QUEUE_PER_LCORE 16
#define MAX_TX_QUEUE_PER_PORT MAX_PORTS
#define MAX_RX_QUEUE_PER_PORT 128

#define MAX_PKT_BURST 32

#define INVALID_PORT_QUEUEID 0xFF

/* Configure how many packets ahead to prefetch, when reading packets */
#define PREFETCH_OFFSET		3

uint64_t sec_to_hpet(double secs);

void print_xon_xoff_statistics(void);

extern struct rte_mempool* tx_pktmbuf_pool[NB_SOCKETS];

/* specification of an RX queue for an lcore */
struct lcore_rx_queue {
	uint8_t port_id;
	uint8_t queue_id;
} __rte_cache_aligned;

/* single table that holds packets before bursting on an lcore_conf TX queue. */
struct mbuf_table {
	uint16_t len;
	struct rte_mbuf *m_table[MAX_PKT_BURST];
};

/**
 * \brief Configuration information for a core.
 *
 * Each active core should have an allocated lcore_conf. The struct contains
 *    information about input and output queues, core-specific transmit buffers,
 *    and other required structures.
 */
struct lcore_conf {
	// Number of receive queues in rx_queue_list
	uint16_t n_rx_queue;
	// Receive queues (each struct contains port id and queue id)
	struct lcore_rx_queue rx_queue_list[MAX_RX_QUEUE_PER_LCORE];

	uint16_t enabled_ind; /**< Index of this core among enabled cores */

	uint8_t tx_queue_id[MAX_PORTS]; /**< The TX queue id to send on a port */
	struct mbuf_table tx_mbufs[MAX_PORTS];	/**< TX buffers */
} __rte_cache_aligned;

extern struct lcore_conf lcore_conf[RTE_MAX_LCORE];

/**
 * \brief Configuration for a port
 */
struct port_info {
	uint8_t is_enabled;  /**< True if this port is enabled */
	uint8_t n_rx_queue; /**< Number of RX queues */
	uint8_t n_tx_queue; /**< Number of TX queues */
	struct ether_addr eth_addr; /**< MAC address of the port */
} __rte_cache_aligned;

// Allocated port configuration structs
extern struct port_info port_info[MAX_PORTS];

// The number of enabled cores
extern uint8_t n_enabled_lcore;
// The lcore index of each enabled core
extern uint8_t enabled_lcore[RTE_MAX_LCORE];
// The number of enabled ports
extern uint8_t n_enabled_port;
// The port index of each enabled port
extern uint8_t enabled_port[MAX_PORTS];

/* Immediately sends given packet */
static inline int
burst_single_packet(struct rte_mbuf *m, uint8_t port)
{
	uint16_t queueid;
	int ret;

	queueid = lcore_conf[rte_lcore_id()].enabled_ind;
	ret = rte_eth_tx_burst(port, queueid, &m, 1);

	if (unlikely(ret < 1)) {
		rte_pktmbuf_free(m);
		return -1;
	}

	return 0;
}

/**
 * \brief Send burst of packets on an output interface
 *
 * Packets that are not sent successfully are dropped (their memory is freed)
 */
static inline int send_queued_packets(uint8_t port) {
	uint32_t lcore_id = rte_lcore_id();
	struct lcore_conf *qconf = &lcore_conf[lcore_id];
	uint16_t queueid = qconf->enabled_ind;
	struct rte_mbuf **m_table = (struct rte_mbuf **) qconf->tx_mbufs[port].m_table;;
	uint16_t len = qconf->tx_mbufs[port].len;
	int ret;

	if (unlikely(len == 0)) {
		/* No packets queued - easy! */
		return 0;
	}

//	while (len > 0) {
//		ret = rte_eth_tx_burst(port, queueid, m_table, len);
//		len -= ret;
//		m_table += ret;
//	}
	qconf->tx_mbufs[port].len = 0;

	ret = rte_eth_tx_burst(port, queueid, m_table, len);

	if (unlikely(ret < len)) {
		int n_unsent = len - ret;
		/* free failed packets */
		do {
			rte_pktmbuf_free(m_table[ret]);
		} while (++ret < len);
		return -n_unsent;
	}

	return 0;
}

/* Enqueue a single packet, and send burst if queue is filled */
static inline int send_packet_via_queue(struct rte_mbuf *m, uint8_t port)
{
	uint32_t lcore_id;
	uint16_t len;
	struct lcore_conf *qconf;

	lcore_id = rte_lcore_id();

	qconf = &lcore_conf[lcore_id];
	len = qconf->tx_mbufs[port].len;
	qconf->tx_mbufs[port].m_table[len] = m;
	len++;
	qconf->tx_mbufs[port].len = len;

	/* enough pkts to be sent */
	if (unlikely(len == MAX_PKT_BURST)) {
		return send_queued_packets(port);
	}

	return 0;
}

#endif /* MAIN_H_ */
