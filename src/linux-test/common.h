/*
 * common.h
 *
 *  Created on: September 25, 2013
 *      Author: aousterh
 */

#include <stdint.h>
#include <time.h>

#ifndef COMMON_H_
#define COMMON_H_

#define PORT 1100
#define MTU_SIZE 1456
#define MAX_CONNECTIONS 512

// Struct for storing packet data
struct packet {
  uint32_t sender;
  uint32_t receiver;
  uint64_t flow_start_time;
  uint64_t packet_send_time;
  uint32_t size;
  uint32_t id;
};

// Get the current time in nanoseconds
static inline
uint64_t current_time_nanoseconds(void) {

  struct timespec time;

  clock_gettime(CLOCK_REALTIME, &time);

  return time.tv_nsec + time.tv_sec * 1000 * 1000 * 1000;
}

// Get the current time
// Copied from www.kerrywong.com/2009/05/28/timing-methods-in-c-under-linux/
static inline uint64_t current_time(void)
{
  uint32_t a, d;

  __asm__ volatile("rdtsc" : "=a" (a), "=d" (d));

  return ((uint64_t) a | (((uint64_t) d) << 32));
}

#endif /* COMMON_H_ */
