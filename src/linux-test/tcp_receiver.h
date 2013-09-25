/*
 * tcp_receiver.h
 *
 *  Created on: September 24, 2013
 *      Author: aousterh
 */

#include <stdint.h>

#ifndef TCP_RECEIVER_H_
#define TCP_RECEIVER_H_

struct tcp_receiver {
  uint32_t id;
  uint32_t num_machines;
};

// Inits a tcp receiver.
void tcp_receiver_init(struct tcp_receiver *receiver, uint32_t id, uint32_t num_machines);

// Runs one TCP receiver. Returns 1 on success or 0 on failure.
int run_tcp_receiver(struct tcp_receiver *receiver);

#endif /* TCP_RECEIVER_H_ */
