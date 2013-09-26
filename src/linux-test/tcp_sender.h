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
  uint64_t start_time;
  uint64_t duration;  // How long to send for
};

// Inits a tcp sender.
void tcp_sender_init(struct tcp_sender *sender, struct generator *gen, uint32_t id, uint64_t start_time, uint64_t duration);

// Runs one TCP sender.
void *run_tcp_sender(void *sender);

#endif /* TCP_SENDER_H_ */
