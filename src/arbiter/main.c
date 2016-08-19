/*-
 *   BSD LICENSE
 * 
 *   Copyright(c) 2010-2012 Intel Corporation. All rights reserved.
 *   All rights reserved.
 * 
 *   Redistribution and use in source and binary forms, with or without 
 *   modification, are permitted provided that the following conditions 
 *   are met:
 * 
 *     * Redistributions of source code must retain the above copyright 
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright 
 *       notice, this list of conditions and the following disclaimer in 
 *       the documentation and/or other materials provided with the 
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its 
 *       contributors may be used to endorse or promote products derived 
 *       from this software without specific prior written permission.
 * 
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 *  version: DPDK.L.1.2.3-3
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_tailq.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_string_fns.h>
#include <rte_errno.h>
#include <rte_malloc.h>
#include <rte_power.h>

#include "main.h"
#include "port_alloc.h"

#include "control.h"

//#define MAIN_C_VERBOSE


#define DO_RFC_1812_CHECKS

#define MEMPOOL_CACHE_SIZE 256

/*
 * This expression is used to calculate the number of mbufs needed depending on user input, taking
 *  into account memory for rx and tx hardware rings, cache per lcore and mtable per port per lcore.
 *  RTE_MAX is used to ensure that NB_MBUF never goes below a minimum value of 8192
 */
#define NB_MBUF RTE_MAX	(																	\
				(unsigned)(n_enabled_port*n_enabled_lcore*RTE_TEST_RX_DESC_DEFAULT +							\
				n_enabled_port*n_enabled_lcore*MAX_PKT_BURST +											\
				n_enabled_port*n_enabled_lcore*RTE_TEST_TX_DESC_DEFAULT +								\
				n_enabled_lcore*MEMPOOL_CACHE_SIZE),												\
				(unsigned)8192)


#define RX_MBUF_SIZE (2048 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define NB_RX_MBUF   NB_MBUF
#define TX_MBUF_SIZE (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM \
					   + ETHER_HDR_LEN + ETHER_CRC_LEN \
					   + sizeof(struct ipv4_hdr) + 1600)
#define NB_TX_MBUF   NB_MBUF

/*
 * RX and TX Prefetch, Host, and Write-back threshold values should be
 * carefully set for optimal performance. Consult the network
 * controller's datasheet and supporting DPDK documentation for guidance
 * on how these parameters should be set.
 */
#define RX_PTHRESH 16 /**< Default values of RX prefetch threshold reg. */
#define RX_HTHRESH 4 /**< Default values of RX host threshold reg. */
#define RX_WTHRESH 1 /**< Default values of RX write-back threshold reg. */


#define TX_PTHRESH 16 /**< Default values of TX prefetch threshold reg. */
#define TX_HTHRESH 8  /**< Default values of TX host threshold reg. */
#define TX_WTHRESH 0  /**< Default values of TX write-back threshold reg. */

#define BURST_TX_DRAIN 200000ULL /* around 100us at 2 Ghz */

#define SOCKET0 0


/*
 * Configurable number of RX/TX ring descriptors
 */
#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 512
static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

static struct rte_eth_conf port_conf = {
	.rxmode = {
		.max_rx_pkt_len = ETHER_MAX_LEN,
		.split_hdr_size = 0,
		.header_split   = 0, /**< Header Split disabled */
		.hw_ip_checksum = 1, /**< IP checksum offload enabled */
		.hw_vlan_filter = 0, /**< VLAN filtering disabled */
		.jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
		.hw_strip_crc   = 0, /**< CRC stripped by hardware */
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			.rss_hf = ETH_RSS_IPV4,
		},
	},
	.txmode = {
		.mq_mode = ETH_DCB_NONE,
	},
};

static const struct rte_eth_rxconf rx_conf = {
	.rx_thresh = {
		.pthresh = RX_PTHRESH,
		.hthresh = RX_HTHRESH,
		.wthresh = RX_WTHRESH,
	},
	.rx_free_thresh = 32,
};

static const struct rte_eth_txconf tx_conf = {
	.tx_thresh = {
		.pthresh = TX_PTHRESH,
		.hthresh = TX_HTHRESH,
		.wthresh = TX_WTHRESH,
	},
	.tx_free_thresh = 0, /* Use PMD default values */
	.tx_rs_thresh = 0, /* Use PMD default values */
	.txq_flags = 0x0,
};

/* mask of enabled ports */
static uint32_t enabled_port_mask = 0;
static int promiscuous_on = 0; /**< Ports set in promiscuous mode off by default. */
static int numa_on = 1; /**< NUMA is enabled by default. */

/* mbuf pool for RX packets */
static struct rte_mempool* rx_pktmbuf_pool[NB_SOCKETS];

/* mbuf pool for TX packets */
struct rte_mempool* tx_pktmbuf_pool[NB_SOCKETS];


/* The size of each per-lcore ring used to send commands to lcore */
#define CMD_RING_SIZE 64

// Allocated configuration structs
struct lcore_conf lcore_conf[RTE_MAX_LCORE];

// The number of enabled cores
uint8_t n_enabled_lcore;
// The lcore index of each enabled core
uint8_t enabled_lcore[RTE_MAX_LCORE];

// Allocated port configuration structs
struct port_info port_info[MAX_PORTS];

// The number of enabled ports
uint8_t n_enabled_port;
// The port index of each enabled port
uint8_t enabled_port[MAX_PORTS];

static uint32_t port_pci_reg_read(uint8_t port, uint32_t reg_off);

uint64_t sec_to_hpet(double secs) {
	return (uint64_t)(secs * rte_get_timer_hz());
}

/**
 * \brief initializes global conf structures
 *
 * Updates lcore_conf and port_info
 */
static int conf_init(uint32_t enabled_port_mask, int numa_on)
{
	unsigned lcore_id;
	uint8_t portid;
	int i;

	// Init lcores
	n_enabled_lcore = 0;
	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		lcore_conf[lcore_id].n_rx_queue = 0;

		if (rte_lcore_is_enabled(lcore_id) != 0) {
			// Update enabled list
			lcore_conf[lcore_id].enabled_ind = n_enabled_lcore;
			enabled_lcore[n_enabled_lcore] = lcore_id;
			n_enabled_lcore++;

			// set TX port array to all invalid entries
			for (i = 0; i < MAX_PORTS; i++) {
				lcore_conf[lcore_id].tx_queue_id[i] = INVALID_PORT_QUEUEID;
			}

			if ((rte_lcore_to_socket_id(lcore_id) != 0) && (numa_on == 0))
			{
				printf("warning: lcore %hhu is on socket!=0 with numa off \n",
							lcore_id);
			}
		}
	}

	// Init ports
	n_enabled_port = 0;
	for (portid = 0; portid < MAX_PORTS; portid++) {
		// Init fields
		port_info[portid].is_enabled =
				((enabled_port_mask & (1 << portid)) != 0);
		port_info[portid].n_rx_queue = 0;
		port_info[portid].n_tx_queue = 0;

		/* For enabled ports, update enabled_port[] and print */
		if ((enabled_port_mask & (1 << portid)) != 0) {
			// Add port to enabled_port list
			enabled_port[n_enabled_port] = portid;
			n_enabled_port++;

			/* Print port status*/
			printf("Port %d is enabled\n", portid);
		}
	}

	return 0;
}

int conf_alloc_rx_queue(uint16_t lcore, uint8_t portid)
{
	uint16_t lcore_n_rx, port_n_rx;

	lcore_n_rx = lcore_conf[lcore].n_rx_queue;
	port_n_rx = port_info[portid].n_rx_queue;

	/* Enforce lcore max RX queues */
	if (lcore_n_rx >= MAX_RX_QUEUE_PER_LCORE) {
		printf("error: too many queues (%u) for lcore: %u\n",
			(unsigned)lcore_n_rx + 1, (unsigned)lcore);
		return -1;
	}
	/* Check port is enabled */
	if (port_info[portid].is_enabled == 0) {
		printf("error: port %u is not enabled, cannot alloc rx for lcore: %u\n",
			(unsigned)portid, (unsigned)lcore);
		return -2;
	}
	/* Enforce port max RX queues */
	if (port_n_rx >= MAX_RX_QUEUE_PER_PORT) {
		printf("error: too many RX queues (%u) for port %u to alloc lcore %u)\n",
			(unsigned)port_n_rx + 1, (unsigned)portid, (unsigned)lcore);
		return -3;
	}
	/* Check lcore is enabled */
	if (!rte_lcore_is_enabled(lcore)) {
		printf("error: lcore %hu is not enabled in lcore mask\n", lcore);
		return -4;
	}

	// Allocate next RX queue of portid to lcore
	lcore_conf[lcore].rx_queue_list[lcore_n_rx].port_id = portid;
	lcore_conf[lcore].rx_queue_list[lcore_n_rx].queue_id = port_n_rx;

	// Increment queue counts
	lcore_conf[lcore].n_rx_queue++;
	port_info[portid].n_rx_queue++;

	return 0;
}

int conf_alloc_tx_queue(uint16_t lcore, uint8_t portid)
{
	uint16_t port_n_tx;

	port_n_tx = port_info[portid].n_tx_queue;

	/* Enforce lcore has not already allocated this TX port */
	if (lcore_conf[lcore].tx_queue_id[portid] != INVALID_PORT_QUEUEID) {
		printf("error: lcore %u already allocated queue for port %u\n",
			(unsigned)lcore, portid);
		return -1;
	}
	/* Check port is enabled */
	if (port_info[portid].is_enabled == 0) {
		printf("error: port %u is not enabled, cannot alloc tx for lcore: %u\n",
			(unsigned)portid, (unsigned)lcore);
		return -2;
	}
	/* Enforce port max TX queues */
	if (port_n_tx >= MAX_TX_QUEUE_PER_PORT) {
		printf("error: too many TX queues (%u) for port %u to alloc lcore %u)\n",
			(unsigned)port_n_tx + 1, (unsigned)portid, (unsigned)lcore);
		return -3;
	}
	/* Check lcore is enabled */
	if (!rte_lcore_is_enabled(lcore)) {
		printf("error: lcore %hu is not enabled in lcore mask\n", lcore);
		return -4;
	}

	// Allocate next TX queue of portid to lcore
	lcore_conf[lcore].tx_queue_id[portid] = port_n_tx;

	// Increment queue counts
	port_info[portid].n_tx_queue++;

	return 0;
}


static void
print_ethaddr(const char *name, const struct ether_addr *eth_addr)
{
	printf ("%s%02X:%02X:%02X:%02X:%02X:%02X", name,
		eth_addr->addr_bytes[0],
		eth_addr->addr_bytes[1],
		eth_addr->addr_bytes[2],
		eth_addr->addr_bytes[3],
		eth_addr->addr_bytes[4],
		eth_addr->addr_bytes[5]);
}

/* Check the link status of all ports in up to 9s, and print them finally */
static void
check_all_ports_link_status(void)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
	uint8_t portid, count, all_ports_up, print_flag = 0;
	uint8_t portind;
	struct rte_eth_link link;

	printf("\nChecking link status");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		all_ports_up = 1;
		for (portind = 0; portind < n_enabled_port; portind++) {
			portid = enabled_port[portind];
			memset(&link, 0, sizeof(link));
			rte_eth_link_get_nowait(portid, &link);
			/* print link status if flag set */
			if (print_flag == 1) {
				if (link.link_status)
					printf("Port %d Link Up - speed %u "
						"Mbps - %s\n", (uint8_t)portid,
						(unsigned)link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					("full-duplex") : ("half-duplex\n"));
				else
					printf("Port %d Link Down\n",
						(uint8_t)portid);
				continue;
			}
			/* clear all_ports_up flag if any link down */
			if (link.link_status == 0) {
				all_ports_up = 0;
				break;
			}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
			print_flag = 1;
			printf("done\n");
		}
	}
}



static inline void
port_pci_reg_write(uint8_t port, uint32_t reg_off, uint32_t reg_v)
{
	struct rte_eth_dev_info dev_info;
	void *reg_addr;
	uint32_t before, after;

	rte_eth_dev_info_get(port, &dev_info);

	reg_addr = (void *)((char *)dev_info.pci_dev->mem_resource[0].addr +
			    reg_off);
	before = rte_le_to_cpu_32(*((volatile uint32_t *)reg_addr));
	*((volatile uint32_t *)reg_addr) = rte_cpu_to_le_32(reg_v);
	after = rte_le_to_cpu_32(*((volatile uint32_t *)reg_addr));

	printf("Port %hhd wrote %u to addr %u, value before %u, after %u\n",
			port, reg_v, reg_off, before, after);
}

static uint32_t port_pci_reg_read(uint8_t port, uint32_t reg_off)
{
	struct rte_eth_dev_info dev_info;
	void *reg_addr;

	rte_eth_dev_info_get(port, &dev_info);
	reg_addr = (void *)((char *)dev_info.pci_dev->mem_resource[0]	.addr +
			    reg_off);
	return rte_le_to_cpu_32(*((volatile uint32_t *)reg_addr));
}

static void print_short_diagnostic(uint8_t port)
{
	uint32_t i;
	uint64_t a,b,c,d;

	/** Timer tests */
	a = rte_get_timer_cycles();
	c = rte_rdtsc();
	for(i = 0; i < 1000; i++) {
		b = rte_get_timer_cycles();
		rte_pause();
	}
	d = rte_rdtsc();
	RTE_LOG(INFO, BENCHAPP, "1000 rte_get_timer_cycles caused %"PRIu64" difference"
			" which is %"PRIu64" TSC cycles\n",
			b - a, d - c);

	a = rte_rdtsc();
	for(i = 0; i < 1000; i++) {
		b = rte_rdtsc();
		rte_pause();
	}
	RTE_LOG(INFO, BENCHAPP, "1000 rte_rdtsc caused %"PRIu64" difference\n",
			b - a);

	/** Port readouts */
	//port_pci_reg_write(0, 0x5BB0, 1 + (0x2 << 10));
	//port_pci_reg_write(0, 0x5BB4, 1 + (0x2 << 10));
	//port_pci_reg_write(0, 0x01A0, 2 | port_pci_reg_read(0, 0x01A0));
	RTE_LOG(INFO, BENCHAPP, "Port %"PRIu8" CTRL register value 0x%"PRIx32"\n",
			port, port_pci_reg_read(port, 0x0));
	RTE_LOG(INFO, BENCHAPP, "Port %"PRIu8" RLPIC register value 0x%"PRIx32"\n",
			port, port_pci_reg_read(port, 0x414C));
	RTE_LOG(INFO, BENCHAPP, "Port %"PRIu8" TLPIC register value 0x%"PRIx32"\n",
			port, port_pci_reg_read(port, 0x4148));
	RTE_LOG(INFO, BENCHAPP, "Port %"PRIu8" EEE register: 0x%"PRIx32"\n",
			port, port_pci_reg_read(port, 0x0E30));
	RTE_LOG(INFO, BENCHAPP, "Port %"PRIu8" LTRMIN register: 0x%"PRIx32"\n",
			port, port_pci_reg_read(port, 0x5BB0));
	RTE_LOG(INFO, BENCHAPP, "Port %"PRIu8" LTRMAX register: 0x%"PRIx32"\n",
			port, port_pci_reg_read(port, 0x5BB4));
	RTE_LOG(INFO, BENCHAPP, "Port %"PRIu8" LTRC register: 0x%"PRIx32"\n",
			port, port_pci_reg_read(port, 0x01A0));
	RTE_LOG(INFO, BENCHAPP, "Port %"PRIu8" LTR Capabilities (0x1C4): 0x%"PRIx32"\n",
			port, port_pci_reg_read(port, 0x01C4));
}


/**
 * \brief Sets up queues and starts ports
 *
 * Sets up RX queues by configuration in lcore_conf.
 * On each lcore, one TX queue is setup for every port.
 * @note uses port_info, lcore_conf
 */
static int
conf_setup(void)
{
	unsigned lcore_id, enabled_ind;
	uint8_t portid, socketid, queue;
	int ret;
	uint16_t queueid;
	struct rte_eth_fc_conf fc_conf;
	uint32_t queue_count = 0;

	/** Count the number of active queues */
	for (enabled_ind = 0; enabled_ind < n_enabled_port; enabled_ind++) {
		portid = enabled_port[enabled_ind];
		queue_count += port_info[portid].n_rx_queue;
		queue_count += port_info[portid].n_tx_queue;
	}

	/** If no ports configured, we're done. */
	if (queue_count == 0) {
		printf("no ports configured, will not set up ports.\n");
		return 0;
	}

	/* initialize all ports, and TX queues */
	for (portid = 0; portid < MAX_PORTS; portid++) {
		if(port_info[portid].is_enabled == 0)
			continue;

		if (portid >= rte_eth_dev_count()) {
			printf("port %u is not present on the board\n", portid);
			return -1;
		}

		// Configure port
		ret = rte_eth_dev_configure(portid, port_info[portid].n_rx_queue,
				(uint16_t)port_info[portid].n_tx_queue, &port_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%d\n",
				ret, portid);

		/* get MAC address */
		rte_eth_macaddr_get(portid, &port_info[portid].eth_addr);
		print_ethaddr(" Address:", &port_info[portid].eth_addr);
		printf(", ");

		/* init one TX queue of port per lcore; set the lcore's struct */
		for (enabled_ind = 0; enabled_ind < n_enabled_lcore; enabled_ind++) {
			lcore_id = enabled_lcore[enabled_ind];
			queue = lcore_conf[lcore_id].tx_queue_id[portid];

			if (queue == INVALID_PORT_QUEUEID)
				continue;

			if (numa_on)
				socketid = (uint8_t)rte_lcore_to_socket_id(lcore_id);
			else
				socketid = 0;

			printf("txq=%u,%d,%d ", lcore_id, queue, socketid);
			fflush(stdout);
			ret = rte_eth_tx_queue_setup(portid, queue, nb_txd,
						     socketid, &tx_conf);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup: err=%d, "
					"port=%d\n", ret, portid);

			lcore_conf[lcore_id].tx_mbufs[portid].len = 0;
		}
	}

	printf("\n");

	/* For each enabled core, set up RX queues */
	for (enabled_ind = 0; enabled_ind < n_enabled_lcore; enabled_ind++) {
		lcore_id = enabled_lcore[enabled_ind];

		printf("\nInitializing rx queues on lcore %u ... ", lcore_id );
		fflush(stdout);

		/* init RX queues */
		for(queue = 0; queue < lcore_conf[lcore_id].n_rx_queue; ++queue) {
			portid = lcore_conf[lcore_id].rx_queue_list[queue].port_id;
			queueid = lcore_conf[lcore_id].rx_queue_list[queue].queue_id;

			if (numa_on)
				socketid = (uint8_t)rte_lcore_to_socket_id(lcore_id);
			else
				socketid = 0;

			printf("rxq=%d,%d,%d ", portid, queueid, socketid);
			fflush(stdout);

			ret = rte_eth_rx_queue_setup(portid, queueid, nb_rxd,
 				        socketid, &rx_conf, rx_pktmbuf_pool[socketid]);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup: err=%d,"
						"port=%d\n", ret, portid);
		}
	}

	printf("\n");

	/* start ports */
	for (portid = 0; portid < MAX_PORTS; portid++) {
		if ((enabled_port_mask & (1 << portid)) == 0) {
			continue;
		}
		/* Start device */
		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_start: err=%d, port=%d\n",
				ret, portid);

		/*
		 * If enabled, put device in promiscuous mode.
		 * This allows IO forwarding mode to forward packets
		 * to itself through 2 cross-connected  ports of the
		 * target machine.
		 */
		if (promiscuous_on)
			rte_eth_promiscuous_enable(portid);

		/* Disable flow control */
		fc_conf.mode = RTE_FC_NONE;
		fc_conf.high_water = 1024;
		fc_conf.low_water = 512;
		fc_conf.pause_time = 100;
		fc_conf.send_xon = 0;
		(void)fc_conf;
		//ret = rte_eth_dev_flow_ctrl_set(portid, &fc_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_flow_ctrl_set: err=%d, port=%d\n",
					ret, portid);

		print_short_diagnostic(portid);

		// Disable Energy Efficient Ethernet (EEE), for lower latencies
		port_pci_reg_write(portid, 0x0E30, 0);

		print_short_diagnostic(portid);
	}

	check_all_ports_link_status();

	return 0;
}

#ifdef DO_RFC_1812_CHECKS
static inline int
is_valid_ipv4_pkt(struct ipv4_hdr *pkt, uint32_t link_len)
{
	/* From http://www.rfc-editor.org/rfc/rfc1812.txt section 5.2.2 */
	/*
	 * 1. The packet length reported by the Link Layer must be large
	 * enough to hold the minimum length legal IP datagram (20 bytes).
	 */
	if (link_len < sizeof(struct ipv4_hdr))
		return -1;

	/* 2. The IP checksum must be correct. */
	/* this is checked in H/W */

	/*
	 * 3. The IP version number must be 4. If the version number is not 4
	 * then the packet may be another version of IP, such as IPng or
	 * ST-II.
	 */
	if (((pkt->version_ihl) >> 4) != 4)
		return -3;
	/*
	 * 4. The IP header length field must be large enough to hold the
	 * minimum length legal IP datagram (20 bytes = 5 words).
	 */
	if ((pkt->version_ihl & 0xf) < 5)
		return -4;

	/*
	 * 5. The IP total length field must be large enough to hold the IP
	 * datagram header, whose length is specified in the IP header length
	 * field.
	 */
	if (rte_cpu_to_be_16(pkt->total_length) < sizeof(struct ipv4_hdr))
		return -5;

	return 0;
}
#endif

void print_xon_xoff_statistics(void)
{
	int i;
	// Print XON/XOFF statistics for enabled ports
	for (i = 0; i < n_enabled_port; i++)
		RTE_LOG(INFO, BENCHAPP,
				"Port %d XONRXC=%u XONTXC=%u XOFFRXC=%u" " XOFFTXC=%u\n", i,
				port_pci_reg_read(i, 0x4048), port_pci_reg_read(i, 0x404C),
				port_pci_reg_read(i, 0x4050), port_pci_reg_read(i, 0x4054));
}


static int lcore_init_power(unsigned lcore_id)
{
	int ret;
	//int i;
	//uint32_t freqs[32];

	ret = rte_power_init(lcore_id);
	if (ret != 0) {
		RTE_LOG(INFO, BENCHAPP, "lcore %u problem initializing power\n", lcore_id);
		return ret;
	}

//	ret = rte_power_freqs(lcore_id, &freqs[0], 32);
//	for (i = 0; i < RTE_MIN(32, ret); i++) {
//		RTE_LOG(INFO, BENCHAPP, "lcore %u, freq %d is %u\n", lcore_id,
//				i, freqs[i]);
//	}

	ret = rte_power_get_freq(lcore_id);
	if (ret == ~0) { /* RTE_POWER_INVALID_FREQ_INDEX */
		RTE_LOG(INFO, BENCHAPP, "lcore %u failed to get current freq\n", lcore_id);
		return -EINVAL;
	} else {
		RTE_LOG(INFO, BENCHAPP, "lcore %u current freq is %d\n", lcore_id, ret);
	}

	ret = rte_power_freq_max(lcore_id);
	if (ret < 0) {
		RTE_LOG(INFO, BENCHAPP, "lcore %u failed to set max freq\n", lcore_id);
		return ret;
	} else {
		RTE_LOG(INFO, BENCHAPP, "lcore %u set max freq (ret=%d)\n", lcore_id, ret);
	}

	return 0;
}

/* display usage */
static void
print_usage(const char *prgname)
{
	printf ("%s [EAL options] -- -p PORTMASK -P"
		"  [--config (port,queue,lcore)[,(port,queue,lcore]]\n"
		"  -p PORTMASK: hexadecimal bitmask of ports to configure\n"
		"  --no-numa: optional, disable numa awareness\n",
		prgname);
}

static int
parse_portmask(const char *portmask)
{
	char *end = NULL;
	unsigned long pm;

	/* parse hexadecimal string */
	pm = strtoul(portmask, &end, 16);
	if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (pm == 0)
		return -1;

	return pm;
}

/* Parse the argument given in the command line of the application */
static int
parse_args(int argc, char **argv)
{
	int opt, ret;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];
	static struct option lgopts[] = {
		{"no-numa", 0, 0, 0},
		{NULL, 0, 0, 0}
	};

	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, "p:P",
				lgopts, &option_index)) != EOF) {

		switch (opt) {
		/* portmask */
		case 'p':
			enabled_port_mask = parse_portmask(optarg);
			if (enabled_port_mask == 0) {
				printf("invalid portmask\n");
				print_usage(prgname);
				return -1;
			}
			break;
		case 'P':
			printf("Promiscuous mode selected\n");
			promiscuous_on = 1;
			break;

		/* long options */
		case 0:
			if (!strcmp(lgopts[option_index].name, "no-numa")) {
				printf("numa is disabled \n");
				numa_on = 0;
			}
			break;

		default:
			print_usage(prgname);
			return -1;
		}
	}

	if (optind >= 0)
		argv[optind-1] = prgname;

	ret = optind-1;
	optind = 0; /* reset getopt lib */
	return ret;
}


static int
init_mem(void)
{
	int socketid;
	unsigned lcore_id;
	char s[64];

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		if (rte_lcore_is_enabled(lcore_id) == 0)
			continue;

		if (numa_on)
			socketid = rte_lcore_to_socket_id(lcore_id);
		else
			socketid = 0;

		if (socketid >= NB_SOCKETS) {
			rte_exit(EXIT_FAILURE, "Socket %d of lcore %u is out of range %d\n",
				socketid, lcore_id, NB_SOCKETS);
		}
		/** RX mbuf pool */
		if (rx_pktmbuf_pool[socketid] == NULL) {
			printf("Trying to allocat RX mbuf pool on socket %d: %u buffers of size %lu\n",
					socketid, NB_RX_MBUF, RX_MBUF_SIZE);
			snprintf(s, sizeof(s), "mbuf_pool_%d", socketid);
			rx_pktmbuf_pool[socketid] =
				rte_mempool_create(s, NB_RX_MBUF, RX_MBUF_SIZE, MEMPOOL_CACHE_SIZE,
					sizeof(struct rte_pktmbuf_pool_private),
					rte_pktmbuf_pool_init, NULL,
					rte_pktmbuf_init, NULL,
					socketid, 0);
			if (rx_pktmbuf_pool[socketid] == NULL)
				rte_exit(EXIT_FAILURE,
						"Cannot init RX mbuf pool on socket %d\n", socketid);
			else
				printf("Allocated RX mbuf pool on socket %d\n", socketid);
		}

		/** TX mbuf pool */
		if (tx_pktmbuf_pool[socketid] == NULL) {
			snprintf(s, sizeof(s), "tx_mbuf_pool_%d", socketid);
			tx_pktmbuf_pool[socketid] =
				rte_mempool_create(s, NB_TX_MBUF, TX_MBUF_SIZE, MEMPOOL_CACHE_SIZE,
					sizeof(struct rte_pktmbuf_pool_private),
					rte_pktmbuf_pool_init, NULL,
					rte_pktmbuf_init, NULL,
					socketid, 0);
			if (tx_pktmbuf_pool[socketid] == NULL)
				rte_exit(EXIT_FAILURE,
						"Cannot init TX mbuf pool on socket %d\n", socketid);
			else
				printf("Allocated TX mbuf pool on socket %d - %"PRIu32" bufs\n",
						socketid, NB_TX_MBUF);
		}
	}

	return 0;
}

static int setup_cores(void)
{
	int i;
	int ret;
	for (i = 0; i < n_enabled_lcore; i++) {
      		ret = lcore_init_power(enabled_lcore[i]);
		if (ret != 0)
			return ret;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int ret;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
	argc -= ret;
	argv += ret;

	/* parse application arguments (after the EAL ones) */
	ret = parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid BENCHAPP parameters\n");

	ret = conf_init(enabled_port_mask, numa_on);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "conf_init failed\n");

	ret = control_do_queue_allocation();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "init_lcore_rx_queues failed\n");

	if (N_CONTROLLER_PORTS > 0) {
		ret = init_mem();
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "init_mem failed\n");
	}

	printf("HPET clock runs at %"PRIu64"Hz\n", rte_get_timer_hz());

	ret = conf_setup();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "conf_setup failed\n");

	if (0)
	  ret = setup_cores();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "setup_cores() failed\n");

	/* execute experiments */
	launch_cores();

	return 0;
}
