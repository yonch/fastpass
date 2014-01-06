/*
 * rdtsc.h
 *
 *  Created on: September 25, 2013
 *      Author: aousterh
 */

#include <stdint.h>

#ifndef RDTSC_H_
#define RDTSC_H_

// Get the current time
// Copied from www.kerrywong.com/2009/05/28/timing-methods-in-c-under-linux/
static inline uint64_t current_time(void)
{
  uint32_t a, d;

  __asm__ volatile("rdtsc" : "=a" (a), "=d" (d));

  return ((uint64_t) a | (((uint64_t) d) << 32));
}

#endif /* RDTSC_H_ */
