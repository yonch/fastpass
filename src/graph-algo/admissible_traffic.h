/*
 * admissible_traffic.h
 *
 *  Created on: November 19, 2013
 *      Author: aousterh
 */

#ifndef ADMISSIBLE_TRAFFIC_H_
#define ADMISSIBLE_TRAFFIC_H_

#include "admissible_structures.h"

#include <inttypes.h>

// Updates the total requested timeslots from src to dst to demand_tslots
void request_timeslots(struct admissible_status *status,
                       uint16_t src, uint16_t dst,
                       uint16_t demand_tslots);

// Determine admissible traffic for one timeslot from queue_in
void get_admissible_traffic(struct backlog_queue *queue_in,
                            struct backlog_queue *queue_out,
                            struct admissible_status *status);

// Reset state of all flows for which src is the sender
void reset_sender(struct admissible_status *status, uint16_t src);

#ifndef likely
#define likely(x)  __builtin_expect((x),1)
#endif /* likely */

#ifndef unlikely
#define unlikely(x)  __builtin_expect((x),0)
#endif /* unlikely */

#endif /* ADMISSIBLE_TRAFFIC_H_ */
