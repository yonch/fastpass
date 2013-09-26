/*
 * generate_packets.c
 *
 *  Created on: September 25, 2013
 *      Author: aousterh
 */

#include "generate_packets.h"

#include <assert.h>
#include <math.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Helper methods
uint64_t generateExponentialVariate(uint64_t mean_t_btwn_requests);

void gen_init(struct generator *generator, enum time_distribution time_dist,
	      enum size_distribution size_dist, uint64_t time_param,
	      uint32_t size_param, uint32_t id, uint32_t num_machines)
{
  assert(time_dist == POISSON);
  assert(size_dist == ONE_SIZE || size_dist == UNIFORM);

  generator->time_dist = time_dist;
  generator->size_dist = size_dist;
  generator->time_param = time_param;
  generator->size_param = size_param;
  generator->t_last_request = 0;
  generator->id = id;
  generator->num_machines = num_machines;
}

void gen_next_packet(struct generator *generator, struct gen_packet *out)
{
  generator->t_last_request += generateExponentialVariate(generator->time_param);
  out->time = generator->t_last_request;
  out->dest = (rand() >> 10) % (generator->num_machines - 1);
  if (out->dest >= generator->id) // skip my id
    out->dest++;

  switch (generator->size_dist) {
    case ONE_SIZE:
      out->size = generator->size_param;
      break;
    case UNIFORM:
      out->size = 1 + rand() / ((double) RAND_MAX) * (generator->size_param - 1);
      break;
    default:
      assert(0);  // Invalid size distribution
  }
}

// Based on a method suggested by wikipedia
// http://en.wikipedia.org/wiki/Exponential_distribution
// We can consider the Ziggurat Algorithm if necessary, which is supposed to be fast
uint64_t generateExponentialVariate(uint64_t mean_t_btwn_requests)
{
  assert(mean_t_btwn_requests > 0);

  double u = rand() / ((double) RAND_MAX);
  return -log(u) * mean_t_btwn_requests;
}
