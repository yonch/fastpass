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

struct igmp_ipv4_hdr {
    uint8_t type;
    uint8_t max_resp_time;
    uin16_t checksum;
    uint32_t group_addr;
} __attribute__((__packed__));


#define TYPE_MEMBERSHIP_REPORT 0x16
#define CONTROLLER_GROUP_ADDR 0xEF010101

#define IGMP_IPV4_HDR_LEN (sizeof(struct igmp_ipv4_hdr))

#ifdef CONFIG_IP_FASTPASS_DEBUG
#define IGMP_INFO(a...) RTE_LOG(INFO, IGMP, ##a)
#else
#define IGMP_INFO(a...)
#endif


static inline struct rte_mbuf *
make_igmp(uint8_t src_port, uint32_t controller_ip)
{
	const unsigned int socket_id = rte_socket_id();
	struct ether_addr group_mac = { // 01:00:5E:01:01:01
			.addr_bytes = {0x01, 0x00, 0x5E, 0x01, 0x01, 0x01}};
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

	igmp_hdr = (struct igmp_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
                                       + sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));

	rte_pktmbuf_append(m, ETHER_HDR_LEN + sizeof(struct ipv4_hdr) + IGMP_IPV4_HDR_LEN);

        /* Ethernet header */
	/* dst addr according to destination */
	ether_addr_copy(&group_mac, &eth_hdr->d_addr);
	/* src addr according to output port*/
	ether_addr_copy(&port_info[src_port].eth_addr, &eth_hdr->s_addr);
	/* ethernet payload is IPv4 */
	eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

        /* ipv4 header */
        ipv4_hdr->version_ihl = 0x45; // Version=4, IHL=5
	ipv4_hdr->type_of_service = 0;
        ipv4_hdr->total_length = rte_cpu_to_be_16(sizeof(struct ipv4_hdr) + IGMP_IPV4_HDR_LEN);
	ipv4_hdr->packet_id = 0;
	ipv4_hdr->fragment_offset = 0;
	ipv4_hdr->time_to_live = 1;
	ipv4_hdr->next_proto_id = IPPROTO_IGMP;
	// ipv4_hdr->hdr_checksum will be calculated in HW
	ipv4_hdr->src_addr = rte_cpu_to_be_32(controller_ip);
	ipv4_hdr->dst_addr = rte_cpu_to_be_32(CONTROLLER_GROUP_ADDR);

	// Activate IP checksum offload for packet
	m->ol_flags |= PKT_TX_IP_CKSUM;
	m->pkt.vlan_macip.f.l2_len = sizeof(struct ether_hdr);
	m->pkt.vlan_macip.f.l3_len = sizeof(struct ipv4_hdr);
	ipv4_hdr->hdr_checksum = 0;

	/* IGMP header */
        igmp_hdr->type = TYPE_MEMBERSHIP_REPORT;
        igmp_hdr->max_resp_time = 0;
        igmp_hdr->checksum = 0;
        igmp_hdr->group_addr = rte_cpu_to_be_32(CONTROLLER_GROUP_ADDR);

        igmp_hdr->checksum = fp_fold((uint64_t) *igmp_hdr);

	return m;
}

#endif /* IGMP_H_ */
