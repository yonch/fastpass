/*
 * make_packet.h
 *
 *  Created on: Sep 26, 2013
 *      Author: yonch
 */

#ifndef MAKE_PACKET_H_
#define MAKE_PACKET_H_

#include "main.h"

/**
 * Prepares a packet
 * @param src_port: the port index of the interface where the packet will go
 * 		out (will set the packet's source MAC address based on this value)
 * @param src_ip: the IP originating the reply (IP of the controller),
 * 		in network byte order
 * @param dst_ip: the IP to send reply to, in network byte order
 * @param dst_ether: the MAC address of the packet's next hop
 * @param data: the data of the packet
 * @param data_len: the length of payload data, in bytes.
 */
static inline struct rte_mbuf *
make_packet(uint8_t src_port, uint32_t src_ip, uint32_t dst_ip,
		struct ether_addr *dst_ether, void *data, uint32_t data_len,
		uint32_t padding_len, uint8_t ipproto)
{
	const unsigned int socket_id = rte_socket_id();
	struct rte_mbuf *m;
	struct ether_hdr *eth_hdr;
	struct ipv4_hdr *ipv4_hdr;
	unsigned char *payload_ptr;
	uint32_t ipv4_length;

	// Allocate packet on the current socket
	m = rte_pktmbuf_alloc(tx_pktmbuf_pool[socket_id]);
	if(m == NULL) {
		RTE_LOG(ERR, BENCHAPP, "core %d could not allocate TX mbuf for packet to IP "
				"0x%" PRIx32 "\n",rte_lcore_id(), rte_be_to_cpu_32(dst_ip));
		return NULL;
	}

	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);

	ipv4_hdr = (struct ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
			     + sizeof(struct ether_hdr));

	payload_ptr = (rte_pktmbuf_mtod(m, unsigned char *)
			     + sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr));

	ipv4_length = RTE_MAX(46u, sizeof(struct ipv4_hdr) + data_len + padding_len);

	rte_pktmbuf_append(m, ETHER_HDR_LEN + ipv4_length);

	/* dst addr according to destination */
	ether_addr_copy(dst_ether, &eth_hdr->d_addr);
	/* src addr according to output port*/
	ether_addr_copy(&port_info[src_port].eth_addr, &eth_hdr->s_addr);
	/* ethernet payload is IPv4 */
	eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

	/* ipv4 header */
	ipv4_hdr->version_ihl = 0x45; // Version=4, IHL=5
	ipv4_hdr->type_of_service = 0;
	ipv4_hdr->total_length = rte_cpu_to_be_16(ipv4_length);
	ipv4_hdr->packet_id = 0;
	ipv4_hdr->fragment_offset = 0;
	ipv4_hdr->time_to_live = 77;
	ipv4_hdr->next_proto_id = ipproto;
	// ipv4_hdr->hdr_checksum will be calculated in HW
	ipv4_hdr->src_addr = src_ip;
	ipv4_hdr->dst_addr = dst_ip;

	rte_memcpy(payload_ptr, data, data_len);

	// Activate IP checksum offload for packet
	m->ol_flags |= PKT_TX_IP_CKSUM;
	m->pkt.vlan_macip.f.l2_len = sizeof(struct ether_hdr);
	m->pkt.vlan_macip.f.l3_len = sizeof(struct ipv4_hdr);
	ipv4_hdr->hdr_checksum = 0;

	return m;
}


#endif /* MAKE_PACKET_H_ */
