/*
 * tcp_sender.h
 *
 *  Created on: September 24, 2013
 *      Author: aousterh
 */

#include "generate_packets.h"

#include <stdint.h>

#ifndef TCP_SENDER_H_
#define TCP_SENDER_H_

struct tcp_sender {
  struct generator *gen;
  uint32_t id;
};

// Inits a tcp sender.
void tcp_sender_init(struct tcp_sender *sender, struct generator *gen, uint32_t id);

// Runs one TCP sender. Returns 1 on success or 0 on failure.
int run_tcp_sender(struct tcp_sender *sender, uint64_t duration);

#endif /* TCP_SENDER_H_ */
