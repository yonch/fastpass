/*
 * igmp.h
 *
 *  Created on: January 17, 2014
 *      Author: aousterh
 */

#ifndef IGMP_H_
#define IGMP_H_

#include "../protocol/platform/generic.h"

#include <rte_ether.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_byteorder.h>

#include "igmp.h"
#include "comm_log.h"

struct fp_watchdog_hdr {
    uint64_t timestamp;
} __attribute__((__packed__));

#define IPPROTO_FASTPASS_WATCHDOG	223

#define FP_WATCHDOG_HDR_LEN (sizeof(struct fp_watchdog_hdr))

#define RTE_LOGTYPE_WATCHDOG RTE_LOGTYPE_USER1

#ifdef CONFIG_IP_FASTPASS_DEBUG
#define WATCHDOG_INFO(a...) RTE_LOG(INFO, WATCHDOG, ##a)
#else
#define WATCHDOG_INFO(a...)
#endif


static inline struct rte_mbuf *
make_watchdog(uint8_t port, uint32_t our_ip)
{
	const unsigned int socket_id = rte_socket_id();
	struct rte_mbuf *m;
	struct ether_hdr *eth_hdr;
        struct ipv4_hdr *ipv4_hdr;
	struct fp_watchdog_hdr *watchdog_hdr;

	// Allocate packet on the current socket
	m = rte_pktmbuf_alloc(tx_pktmbuf_pool[socket_id]);
	if(m == NULL) {
		WATCHDOG_INFO("core %d could not allocate TX mbuf for watchdog!\n",
                        rte_lcore_id());
		return NULL;
	}

	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);

	ipv4_hdr = (struct ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
                                       + sizeof(struct ether_hdr));

	watchdog_hdr = (struct fp_watchdog_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
                                       + sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));

	rte_pktmbuf_append(m, ETHER_HDR_LEN + sizeof(struct ipv4_hdr) + FP_WATCHDOG_HDR_LEN);

	/* Ethernet header */
	/* dst addr according to destination */
	ether_addr_copy(&fp_igmp_group_mac, &eth_hdr->d_addr);
	/* src addr according to output port*/
	ether_addr_copy(&port_info[port].eth_addr, &eth_hdr->s_addr);
	/* ethernet payload is IPv4 */
	eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

        /* ipv4 header */
	ipv4_hdr->version_ihl = 0x45; // Version=4, IHL=5
	ipv4_hdr->type_of_service = 0;
	ipv4_hdr->total_length = rte_cpu_to_be_16(sizeof(struct ipv4_hdr) + FP_WATCHDOG_HDR_LEN);
	ipv4_hdr->packet_id = 0;
	ipv4_hdr->fragment_offset = htons(0x4000);
	ipv4_hdr->time_to_live = 77;
	ipv4_hdr->next_proto_id = IPPROTO_FASTPASS_WATCHDOG;
	// ipv4_hdr->hdr_checksum will be calculated in HW
	ipv4_hdr->src_addr = rte_cpu_to_be_32(our_ip);
	ipv4_hdr->dst_addr = rte_cpu_to_be_32(CONTROLLER_GROUP_ADDR);

	// Activate IP checksum offload for packet
	m->ol_flags |= PKT_TX_IP_CKSUM;
	m->l2_len = sizeof(struct ether_hdr);
	m->l3_len = sizeof(struct ipv4_hdr);
	ipv4_hdr->hdr_checksum = 0;

	/* Watchdog header */
	watchdog_hdr->timestamp = fp_get_time_ns();

	return m;
}

static void send_watchdog(uint8_t port, uint32_t our_ip) {
	struct rte_mbuf *mbuf;
	int res;

try_sending:
	/* make the packet */
	mbuf = make_watchdog(port, our_ip);
	if (mbuf == NULL) {
		comm_log_failed_to_allocate_watchdog();
		goto try_sending;
	}

	/* send to NIC queues */
	res = burst_single_packet(mbuf, port);
	if (res != 0) {
		 comm_log_failed_to_burst_watchdog();
		 goto try_sending;
	}

	WATCHDOG_INFO("core %u sent watchdog from IP 0x%"PRIx32" on port %u\n",
			rte_lcore_id(), our_ip, port);
}


#endif /* IGMP_H_ */
