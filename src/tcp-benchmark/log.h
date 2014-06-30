/*
 * log.h
 *
 *  Created on: January 5, 2014
 *      Author: aousterh
 */

#ifndef LOG_H_
#define LOG_H_

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define LATENCY_BIN_DURATION (1 * 1000) // 50 microseconds in nanoseconds
#define MAX_SENDERS 1
#define NUM_LATENCY_BINS 5000

// Information logged per sending node per interval
// All times are in nanoseconds
struct node_info {
  uint32_t num_start_packets;  // number of first packets received
  uint32_t num_fcs;  // number of flows completed
  uint64_t sum_of_latencies;  // sum of first packet latencies
  uint64_t sum_of_fcs;  // sum of flow completion times
  uint64_t bytes_received;
};

// Information logged per interval on each server
// All times are in nanoseconds
struct interval_info {
  uint64_t start_time;
  uint64_t end_time;
  struct node_info nodes[MAX_SENDERS];
  uint32_t latency_bins[NUM_LATENCY_BINS];
};

struct log {
  struct interval_info *current;
  struct interval_info *log_intervals;
};

static inline
void init_log(struct log *log, uint32_t num_intervals) {
  assert(log != NULL);
  
  log->log_intervals = calloc(num_intervals,
			      sizeof(struct interval_info));
  assert(log->log_intervals != NULL);

  log->current = log->log_intervals;
}

// Logs the arrival of a packet indicating the start of a flow
static inline
void log_flow_start(struct log *log, uint16_t node_id,
		    uint32_t bytes, uint64_t latency) {
  assert(log != NULL);
  assert(node_id < MAX_SENDERS);

  struct node_info *node = &log->current->nodes[node_id];
  node->num_start_packets++;
  node->sum_of_latencies += latency;
  node->bytes_received += bytes;

  // Tally this latency in the appropriate bin
  //  uint16_t latency_bin_index = latency / LATENCY_BIN_DURATION;
  //if (latency_bin_index > NUM_LATENCY_BINS - 1)
  //  latency_bin_index = NUM_LATENCY_BINS - 1;
  //log->current->latency_bins[latency_bin_index]++;
}

// Logs more bytes received
static inline
void log_data_received(struct log *log, uint16_t node_id,
		       uint32_t bytes) {
  assert(log != NULL);
  assert(node_id < MAX_SENDERS);

  struct node_info *node = &log->current->nodes[node_id];
  node->bytes_received += bytes;
}

// Logs flow completed
static inline
void log_flow_completed(struct log *log, uint16_t node_id,
			uint64_t fc_time) {
  assert(log != NULL);
  assert(node_id < MAX_SENDERS);

  struct node_info *node = &log->current->nodes[node_id];
  node->num_fcs++;
  node->sum_of_fcs += fc_time;

  // Tally this fct in the appropriate bin
  uint16_t latency_bin_index = fc_time / LATENCY_BIN_DURATION;
  if (latency_bin_index > NUM_LATENCY_BINS - 1)
    latency_bin_index = NUM_LATENCY_BINS - 1;
  log->current->latency_bins[latency_bin_index]++;
}

// Prints the log contents in CSV format to stdout to be piped to a file
static inline
void write_out_log(struct log *log) {
  assert(log != NULL);

  // Print column headers
  printf("start_time, end_time, ");
  uint16_t i;
  for (i = 0; i < MAX_SENDERS; i++)
    printf("num_start_packets_%d, num_flows_%d, latency_sum_%d, fc_sum_%d, bytes_%d, ", i, i, i, i, i);

  for (i = 0; i < NUM_LATENCY_BINS - 1; i++)
    printf("bin_%d, ", i);
  printf("bin_%d\n", NUM_LATENCY_BINS - 1);

  // Output data
  struct interval_info *interval;
  for (interval = log->log_intervals; interval < log->current; interval++) {
    printf("%"PRIu64", %"PRIu64", ", interval->start_time, interval->end_time);

    for (i = 0; i < MAX_SENDERS; i++) {
      struct node_info *node = &interval->nodes[i];
      printf("%u, %u, %"PRIu64", %"PRIu64", %"PRIu64", ",
	     node->num_start_packets, node->num_fcs,
	     node->sum_of_latencies, node->sum_of_fcs, node->bytes_received);
    }

    for (i = 0; i < NUM_LATENCY_BINS - 1; i++)
      printf("%u, ", interval->latency_bins[i]);
    printf("%u\n", interval->latency_bins[NUM_LATENCY_BINS - 1]);
  }
}

#endif /* LOG_H_ */
