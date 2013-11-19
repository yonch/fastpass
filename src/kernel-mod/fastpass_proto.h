/*
 * fastpass_proto.h
 *
 *  Created on: Nov 18, 2013
 *      Author: yonch
 */

#ifndef FASTPASS_PROTO_H_
#define FASTPASS_PROTO_H_

#define IPPROTO_FASTPASS 222

#define MAX_FASTPASS_ONLY_HEADER (sizeof(struct fastpass_req_hdr))
#define MAX_TOTAL_FASTPASS_HEADERS (MAX_FASTPASS_ONLY_HEADER + MAX_HEADER)

struct fastpass_sock {
	/* inet_sock has to be the first member */
	struct inet_sock inet;
	__u32 mss_cache;
};

static inline struct fastpass_sock *fastpass_sk(const struct sock *sk)
{
	return (struct fastpass_sock *)sk;
}

enum {
	FASTPASSF_OPEN		      = TCPF_ESTABLISHED,
	FASTPASSF_CLOSED	      = TCPF_CLOSE,
};

/**
 * struct fastpass_req_hdr - FastPass request packet header
 */
struct fastpass_req_hdr {
	__sum16	checksum;
	__u8	seq;
};

static inline struct fastpass_req_hdr *fastpass_req_hdr(
		const struct sk_buff *skb)
{
	return (struct fastpass_req_hdr *)skb_transport_header(skb);
}

#endif /* FASTPASS_PROTO_H_ */
