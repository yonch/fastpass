/*
 * igmp.h
 *
 *  Created on: January 17, 2014
 *      Author: aousterh
 */

#ifndef FASTPASS_IGMP_H_
#define FASTPASS_IGMP_H_

#include "../protocol/platform/generic.h"

#include <rte_ether.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_byteorder.h>

struct igmp_ipv4_hdr {
    uint8_t type;
    uint8_t max_resp_time;
    uint16_t checksum;
    uint32_t group_addr;
} __attribute__((__packed__));


#define TYPE_MEMBERSHIP_REPORT 0x16
/* 239.1.1.1 */
#define CONTROLLER_GROUP_ADDR 0xEF010101

static struct ether_addr fp_igmp_group_mac = { // 01:00:5E:01:01:01
		.addr_bytes = {0x01, 0x00, 0x5E, 0x01, 0x01, 0x01}};

#define IGMP_IPV4_HDR_LEN (sizeof(struct igmp_ipv4_hdr))

#define RTE_LOGTYPE_IGMP RTE_LOGTYPE_USER1

#ifdef CONFIG_IP_FASTPASS_DEBUG
#define IGMP_INFO(a...) RTE_LOG(INFO, IGMP, ##a)
#else
#define IGMP_INFO(a...)
#endif


static inline struct rte_mbuf *
make_igmp(uint8_t src_port, uint32_t controller_ip)
{
	const unsigned int socket_id = rte_socket_id();
	struct rte_mbuf *m;
	struct ether_hdr *eth_hdr;
        struct ipv4_hdr *ipv4_hdr;
	struct igmp_ipv4_hdr *igmp_hdr;

	// Allocate packet on the current socket
	m = rte_pktmbuf_alloc(tx_pktmbuf_pool[socket_id]);
	if(m == NULL) {
		RTE_LOG(ERR, BENCHAPP, "core %d could not allocate TX mbuf for IGMP\n",
                        rte_lcore_id());
		return NULL;
	}

	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);

        ipv4_hdr = (struct ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
                                       + sizeof(struct ether_hdr));

	igmp_hdr = (struct igmp_ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
                                       + sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));

	rte_pktmbuf_append(m, ETHER_HDR_LEN + sizeof(struct ipv4_hdr) + IGMP_IPV4_HDR_LEN);

        /* Ethernet header */
	/* dst addr according to destination */
	ether_addr_copy(&fp_igmp_group_mac, &eth_hdr->d_addr);
	/* src addr according to output port*/
	ether_addr_copy(&port_info[src_port].eth_addr, &eth_hdr->s_addr);
	/* ethernet payload is IPv4 */
	eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

        /* ipv4 header */
        ipv4_hdr->version_ihl = 0x45; // Version=4, IHL=5
	ipv4_hdr->type_of_service = 0;
        ipv4_hdr->total_length = rte_cpu_to_be_16(sizeof(struct ipv4_hdr) + IGMP_IPV4_HDR_LEN);
	ipv4_hdr->packet_id = 0;
	ipv4_hdr->fragment_offset = htons(0x4000);
	ipv4_hdr->time_to_live = 1;
	ipv4_hdr->next_proto_id = IPPROTO_IGMP;
	// ipv4_hdr->hdr_checksum will be calculated in HW
	ipv4_hdr->src_addr = rte_cpu_to_be_32(controller_ip);
	ipv4_hdr->dst_addr = rte_cpu_to_be_32(CONTROLLER_GROUP_ADDR);

	// Activate IP checksum offload for packet
	m->ol_flags |= PKT_TX_IP_CKSUM;
	m->l2_len = sizeof(struct ether_hdr);
	m->l3_len = sizeof(struct ipv4_hdr);
	ipv4_hdr->hdr_checksum = 0;

	/* IGMP header */
        igmp_hdr->type = TYPE_MEMBERSHIP_REPORT;
        igmp_hdr->max_resp_time = 0;
        igmp_hdr->checksum = 0;
        igmp_hdr->group_addr = rte_cpu_to_be_32(CONTROLLER_GROUP_ADDR);

        igmp_hdr->checksum = fp_fold(fp_csum_partial((void *)igmp_hdr,
        		sizeof(*igmp_hdr), 0));

	return m;
}

static void send_igmp(uint8_t port, uint32_t controller_ip) {
	struct rte_mbuf *mbuf;
	int res;
	do {
		mbuf = make_igmp(port, controller_ip);
		res = burst_single_packet(mbuf, port);
	} while (res != 0);

	IGMP_INFO("core %u sent igmp from IP 0x%"PRIx32" on port %u\n",
			rte_lcore_id(), controller_ip, port);
}


#endif /* FASTPASS_IGMP_H_ */
