/*
 * generate_packets.h
 *
 *  Created on: September 25, 2013
 *      Author: aousterh
 */

#include <stdint.h>

#ifndef GENERATE_PACKETS_H_
#define GENERATE_PACKETS_H_

enum time_distribution {
  POISSON  // time_param is mean_t_btwn_requests
};

enum size_distribution {
  ONE_SIZE,  // size_param is size
  UNIFORM   // uniform in (1, size_param)
};

struct generator {
  enum time_distribution time_dist;
  enum size_distribution size_dist;
  uint64_t time_param;
  uint32_t size_param;
  uint64_t t_last_request;
  uint32_t id;
  uint32_t num_machines;
};

struct gen_packet {
  uint64_t time;
  uint32_t dest;
  uint32_t size;  /* request size in MTUs */
};

// Initialize an instance of a generator.
// Allows for different distributions of packet arrival time and
// packet size
void gen_init(struct generator *generator, enum time_distribution time_dist, enum size_distribution size_dist, uint64_t time_param, uint32_t size_param, uint32_t id, uint32_t num_machines);

// Generate a packet
void gen_next_packet(struct generator *generator, struct gen_packet *out);


#endif /* GENERATE_PACKETS_H_ */
