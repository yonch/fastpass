/*
 * common.h
 *
 *  Created on: September 25, 2013
 *      Author: aousterh
 */

#include <stdint.h>

#define PORT 1100
#define MTU_SIZE 1500

#ifndef COMMON_H_
#define COMMON_H_

// Struct for storing packet data
struct packet {
  uint32_t sender;
  uint32_t receiver;
  uint64_t send_time;
  uint32_t size;
};

// Get the current time
// Copied from www.kerrywong.com/2009/05/28/timing-methods-in-c-under-linux/
uint64_t get_time(void)
{
  uint32_t a, d;

  __asm__ volatile("rdtsc" : "=a" (a), "=d" (d));

  return ((uint64_t) a | (((uint64_t) d) << 32));
}

#endif /* COMMON_H_ */
