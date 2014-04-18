/*
 * arp.h
 *
 *  Created on: Sep 26, 2013
 *      Author: yonch
 */

#ifndef ARP_H_
#define ARP_H_

#include "main.h"
#include <rte_ether.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_byteorder.h>

struct arp_ipv4_hdr {
	uint16_t htype;			/**< Hardware type (HTYPE) */
	uint16_t ptype;			/**< Protocol type (PTYPE) */
	uint8_t hlen;			/**< Hardware address length (HLEN) */
	uint8_t plen;			/**< Protocol address length (PLEN) */
	uint16_t oper;			/**< Operation (OPER) */
	struct ether_addr sha;	/**< Sender hardware address (SHA) */
	uint32_t spa;			/**< Sender protocol address (SPA) */
	struct ether_addr tha;	/**< Target hardware address (THA) */
	uint32_t tpa;			/**< Target protocol address (TPA) */
} __attribute__((__packed__));


#define ARP_HTYPE_ETHERNET 1
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

#define ARP_IPV4_HDR_LEN (sizeof(struct arp_ipv4_hdr))

#define RTE_LOGTYPE_ARP RTE_LOGTYPE_USER1

#ifdef CONFIG_IP_FASTPASS_DEBUG
#define ARP_INFO(a...) RTE_LOG(INFO, ARP, ##a)
#else
#define ARP_INFO(a...)
#endif


static inline struct rte_mbuf *
make_arp(uint8_t src_port, uint8_t oper, struct ether_addr *sha, uint32_t spa,
		struct ether_addr *tha, uint32_t tpa)
{
	const unsigned int socket_id = rte_socket_id();
	struct ether_addr broadcast_mac = { // FF:FF:FF:FF:FF:FF
			.addr_bytes = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
	struct rte_mbuf *m;
	struct ether_hdr *eth_hdr;
	struct arp_ipv4_hdr *arp_hdr;

	// Allocate packet on the current socket
	m = rte_pktmbuf_alloc(tx_pktmbuf_pool[socket_id]);
	if(m == NULL) {
		RTE_LOG(ERR, BENCHAPP, "core %d could not allocate TX mbuf for ARP SPA="
				"0x%"PRIx32" TPA=0x%"PRIx32"\n",rte_lcore_id(), spa, tpa);
		return NULL;
	}

	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);

	arp_hdr = (struct arp_ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
			     + sizeof(struct ether_hdr));

	rte_pktmbuf_append(m, ETHER_HDR_LEN + ARP_IPV4_HDR_LEN);

	/* dst addr according to destination */
	ether_addr_copy(&broadcast_mac, &eth_hdr->d_addr);
	/* src addr according to output port*/
	ether_addr_copy(&port_info[src_port].eth_addr, &eth_hdr->s_addr);
	/* ethernet payload is IPv4 */
	eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_ARP);

	/* ARP header */
	arp_hdr->htype = rte_cpu_to_be_16(ARP_HTYPE_ETHERNET);
	arp_hdr->ptype = rte_cpu_to_be_16(ETHER_TYPE_IPv4);
	arp_hdr->hlen = ETHER_ADDR_LEN;
	arp_hdr->plen = 4;
	arp_hdr->oper = rte_cpu_to_be_16(oper);
	ether_addr_copy(sha, &arp_hdr->sha);
	arp_hdr->spa = rte_cpu_to_be_32(spa);
	ether_addr_copy(tha, &arp_hdr->tha);
	arp_hdr->tpa = rte_cpu_to_be_32(tpa);

	return m;
}

/**
 * Sends a gratuitous arp for the given address out the given port
 * @param src_port: the port index of the interface where the packet will go
 * 		out (will set the packet's source MAC address based on this value)
 * @param src_ip: the IP originating the reply (IP of the controller),
 * 		in network byte order
 */
static inline struct rte_mbuf *
make_gratuitous_arp(uint8_t src_port, uint32_t src_ip)
{
	/* SPA=TPA=src_ip, SPA=src_mac, TPA=zeros */
	struct ether_addr zero_mac = {.addr_bytes = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
	return make_arp(src_port, ARP_OP_REQUEST,
			&port_info[src_port].eth_addr, src_ip,
			&zero_mac, src_ip);
}

static void send_gratuitous_arp(uint16_t port, uint32_t ip) {
	struct rte_mbuf *mbuf;
	int res;
	do {
		mbuf = make_gratuitous_arp(port, ip);
		res = burst_single_packet(mbuf, port);
	} while (res != 0);

	ARP_INFO("core %u sent gratuitous ARP for IP 0x%"PRIx32" on port %u\n",
			rte_lcore_id(), ip, port);
}

//static bool is_valid_arp_request(struct rte_mbuf *m)
//{
//	struct ether_hdr *eth_hdr;
//	struct arp_ipv4_hdr *arp_hdr;
//
//	/* Check valid length */
//	if (rte_pktmbuf_data_len(m) < sizeof(struct eth_hdr)
//								+ sizeof(struct arp_ipv4_hdr))
//		return false;
//
//	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
//
//	/* Check ether type */
//	if (eth_hdr->ether_type != rte_cpu_to_be_16(ETHER_TYPE_ARP))
//		return false;
//
//	arp_hdr = (struct arp_ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
//			     + sizeof(struct ether_hdr));
//
//	if (arp_hdr->htype != rte_cpu_to_be_16(ARP_HTYPE_ETHERNET))
//		return false;
//	if (arp_hdr->ptype != rte_cpu_to_be_16(ETHER_TYPE_IPv4))
//		return false;
//	if (arp_hdr->hlen != ETHER_ADDR_LEN)
//		return false;
//	if (arp_hdr->plen != 4)
//		return false;
//
//	return true;
//}

///* return true if the MAC packet is a request for given IP */
//static bool is_arp_request_for_addr(struct rte_mbuf *m, uint32_t ip)
//{
//	struct ether_hdr *eth_hdr;
//	struct arp_ipv4_hdr *arp_hdr;
//
//	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
//	arp_hdr = (struct arp_ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
//			     + sizeof(struct ether_hdr));
//
//	if (arp_hdr->oper != rte_cpu_to_be_16(ARP_OP_REQUEST))
//		return false;
//
//	if
//}


static void print_arp(struct rte_mbuf *m, uint16_t portid) {
	struct arp_ipv4_hdr *arp_hdr;

	arp_hdr = (struct arp_ipv4_hdr *)(rte_pktmbuf_mtod(m, unsigned char *)
			     + sizeof(struct ether_hdr));

	uint8_t *sha = &arp_hdr->sha.addr_bytes[0];
	uint8_t *tha = &arp_hdr->tha.addr_bytes[0];

	ARP_INFO("core %u got ARP: port=%u sha=%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx spa=0x%"PRIx32
			" tha=%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx tpa=0x%"PRIx32"\n",
			rte_lcore_id(), portid,
			sha[0], sha[1], sha[2], sha[3], sha[4], sha[5], rte_be_to_cpu_32(arp_hdr->spa),
			tha[0], tha[1], tha[2], tha[3], tha[4], tha[5], rte_be_to_cpu_32(arp_hdr->tpa));
}

#endif /* ARP_H_ */
