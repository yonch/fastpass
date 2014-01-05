/*
 * tcp_receiver.c
 *
 *  Created on: September 24, 2013
 *      Author: aousterh
 */

#include "common.h"
#include "tcp_receiver.h" 
 
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/time.h>
#include <stdbool.h>
#include <fcntl.h>

#define BITS_PER_BYTE 8
#define NUM_INTERVALS 100

void tcp_receiver_init(struct tcp_receiver *receiver, uint64_t start_time,
		       uint64_t duration, float clock_freq, uint16_t port_num)
{
  int i;

  receiver->start_time = start_time;
  receiver->duration = duration;
  receiver->clock_freq = clock_freq;
  receiver->port_num = port_num;
}

void *run_tcp_receiver(void *arg)
{
  int i;
  struct sockaddr_in sock_addr;
  struct tcp_receiver *receiver = (struct tcp_receiver *) arg;
  uint32_t flows_received = 0;
  uint32_t bytes_received = 0;
  uint64_t total_latency = 0;
  uint64_t total_first_packet_latency = 0;
  struct timeval tv;

  tv.tv_sec = 1;
  tv.tv_usec = 0;

  // Create a socket
  int sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  assert(sock_fd > 0);
 
  // Initialize socket address
  memset(&sock_addr, 0, sizeof(sock_addr));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(receiver->port_num);
  sock_addr.sin_addr.s_addr = htonl(INADDR_ANY);
 
  // Bind the address to the socket
  assert(bind(sock_fd,(struct sockaddr *)&sock_addr, sizeof(sock_addr)) != -1);
 
  // Listen for incoming connections
  assert(listen(sock_fd, MAX_CONNECTIONS) != -1);

  int connected_fds[MAX_CONNECTIONS];
  int bytes_left[MAX_CONNECTIONS];
  struct packet packets[MAX_CONNECTIONS];
  for (i = 0; i < MAX_CONNECTIONS; i++)
    connected_fds[i] = -1;

  fd_set rfds;
  FD_ZERO(&rfds);

  uint64_t end_time = receiver->start_time + receiver->duration;

  // Set up info to track per-interval throughputs
  uint64_t interval_duration = receiver->duration / NUM_INTERVALS;
  uint64_t interval_end_time = receiver->start_time + interval_duration;
  uint32_t interval_bytes_received = 0;
  while(current_time() < end_time + 1*1000*1000*1000uLL)
  {
    int i;
    char buf[MTU_SIZE];

    // Add fds to set and compute max
    int max = sock_fd;
    FD_SET(sock_fd, &rfds);
    for (i = 0; i < MAX_CONNECTIONS; i++) {
      if (connected_fds[i] == -1)
	continue;

      FD_SET(connected_fds[i], &rfds);
      if (connected_fds[i] > max)
	max = connected_fds[i];
    }

    // Wait for a socket to have data to read or a new connection
    int retval = select(max + 1, &rfds, NULL, NULL, &tv);
    if (retval < 0)
      break;
    
    if (FD_ISSET(sock_fd, &rfds))
      {
	// Accept a new connection
	int accept_fd = accept(sock_fd, NULL, NULL);
	assert(accept_fd > 0);

	// Add to list of fds
	bool success = false;
	for (i = 0; i < MAX_CONNECTIONS; i++) {
	  if (connected_fds[i] == -1)
	    {
	      connected_fds[i] = accept_fd;
	      bytes_left[i] = -1;
	      success = true;
	      break;
	    }
	}
	assert(success);  // Otherwise, we have too many connections

      }
      
    // Read from all ready connections
    for (i = 0; i < MAX_CONNECTIONS; i++)
      {
	if (connected_fds[i] == -1 ||
	    !FD_ISSET(connected_fds[i], &rfds))
	  continue;  // This fd is invalid or not ready

	int ready_fd = connected_fds[i];
	int ready_index = i;

	if (bytes_left[ready_index] == -1) {
	  // Read first part of flow
	  struct packet *incoming = &packets[ready_index];
	  int bytes = read(ready_fd, incoming, sizeof(struct packet));
	  assert(bytes == sizeof(struct packet));
	  interval_bytes_received += bytes;
	  bytes_left[ready_index] = incoming->size * MTU_SIZE - sizeof(struct packet);
	  uint64_t time_now = current_time();
	  total_first_packet_latency += (time_now - incoming->packet_send_time);
	}
	else {
	  // Read in data
	  int count = bytes_left[ready_index] < MTU_SIZE ? bytes_left[ready_index] : MTU_SIZE;
	  int bytes = read(ready_fd, buf, count);
	  interval_bytes_received += bytes;
	  bytes_left[ready_index] -= bytes;
	  uint64_t time_now = current_time();

	  if (bytes_left[ready_index] == 0)
	    {
	      // This flow is done!
	      struct packet *incoming = &packets[ready_index];
	      /*printf("received,\t%d, %d, %d, %"PRIu64", %d, %"PRIu64"\n",
		     incoming->sender, incoming->receiver, incoming->size,
		     incoming->flow_start_time, incoming->id, time_now);*/

	      if (incoming->flow_start_time < end_time) {
		total_latency += (time_now - incoming->flow_start_time);
		flows_received++;
		bytes_received += incoming->size * MTU_SIZE;
	      }
 
	      assert(shutdown(ready_fd, SHUT_RDWR) != -1);
	      close(ready_fd);
	  
	      // Remove from set
	      FD_CLR(ready_fd, &rfds);
	      connected_fds[ready_index] = -1;
	    }
	}
      }

    uint64_t now = current_time();
    if (now > interval_end_time && now < end_time) {
      double interval_throughput = ((double) interval_bytes_received) *
	BITS_PER_BYTE / (1000 * 1000 * 1000) * NUM_INTERVALS;
      printf("throughput (Gbps): %f\n", interval_throughput);
      interval_end_time += interval_duration;
      interval_bytes_received = 0;
    }

  }

  if (flows_received == 0)
     printf("received 0 flows\n");
  else {
    double avg_flow_time = total_latency / (receiver->clock_freq * 1000 * flows_received);
    printf("received %d flows\n", flows_received);
    printf("\taverage flow completion time (microseconds): %f\n", avg_flow_time);
    double throughput = ((double) bytes_received) * BITS_PER_BYTE / (1000 * 1000 * 1000);
    printf("\taverage application throughput (Gbps): %f\n", throughput);
    double avg_first_packet_latency = total_first_packet_latency /
      (receiver->clock_freq * 1000 * flows_received);
    printf("\taverage packet latency (microseconds): %f\n", avg_first_packet_latency);
  }
  close(sock_fd);
}
