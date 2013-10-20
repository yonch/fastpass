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

void tcp_receiver_init(struct tcp_receiver *receiver, uint64_t start_time, uint64_t duration)
{
  int i;

  receiver->start_time = start_time;
  receiver->duration = duration;
}

void *run_tcp_receiver(void *arg)
{
  int i;
  struct sockaddr_in sock_addr;
  struct tcp_receiver *receiver = (struct tcp_receiver *) arg;
  uint32_t flows_received = 0;
  uint32_t bytes_received = 0;
  uint64_t total_latency = 0;
  struct timeval tv;

  tv.tv_sec = 1;
  tv.tv_usec = 0;

  // Create a socket
  int sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  assert(sock_fd > 0);
 
  // Initialize socket address
  memset(&sock_addr, 0, sizeof(sock_addr));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(PORT);
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
	  bytes_left[ready_index] = incoming->size * MTU_SIZE - sizeof(struct packet);
	}
	else {
	  // Read in data
	  int count = bytes_left[ready_index] < MTU_SIZE ? bytes_left[ready_index] : MTU_SIZE;
	  int bytes = read(ready_fd, buf, count);
	  bytes_left[ready_index] -= bytes;
	  uint64_t time_now = current_time();

	  if (bytes_left[ready_index] == 0)
	    {
	      // This flow is done!
	      struct packet *incoming = &packets[ready_index];
	      printf("received,\t%d, %d, %d, %"PRIu64", %d, %"PRIu64"\n",
		     incoming->sender, incoming->receiver, incoming->size,
		     incoming->send_time, incoming->id, time_now);

	      if (incoming->send_time < end_time) {
		total_latency += (time_now - incoming->send_time);
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

  }

  printf("receiver finished at %"PRIu64"\n", current_time());
  if (flows_received == 0)
     printf("received 0 flows\n");
  else {
    uint64_t avg_flow_time = total_latency / flows_received;
    printf("received %d flows (%d total bytes) with average flow completion time %"PRIu64"\n",
	   flows_received, bytes_received, avg_flow_time);
  }
  close(sock_fd);
}
