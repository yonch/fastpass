/*
 * tcp_sender.c
 *
 *  Created on: September 24, 2013
 *      Author: aousterh
 */

#include "common.h"
#include "generate_packets.h"
#include "tcp_sender.h" 

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <sys/unistd.h>
#include <sys/fcntl.h>

#define NUM_CORES 4

const int MAX_SOCKETS = 128;
 
void tcp_sender_init(struct tcp_sender *sender, struct generator *gen, uint32_t id, uint64_t start_time, uint64_t duration, float clock_freq)
{
  sender->gen = gen;
  sender->id = id;
  sender->start_time = start_time;
  sender->duration = duration;
  sender->clock_freq = clock_freq;
}

void choose_IP(uint32_t receiver_id, char *ip_addr) {
  int index = 0;

  int core = rand() / ((double) RAND_MAX) * NUM_CORES + 1;
  switch(core)
    {
    case (1):
      ip_addr[index++] = '1';
      break;
    case (2):
      ip_addr[index++] = '2';
      break;
    case (3):
      ip_addr[index++] = '3';
      break;
    case (4):
      ip_addr[index++] = '4';
      break;
    default:
      assert(0);
  }
  
  ip_addr[index++] = '.';
  ip_addr[index++] = '1';
  ip_addr[index++] = '.';

  switch(receiver_id >> 1)
    {
    case (0):
      ip_addr[index++] = '1';
      break;
    case (1):
      ip_addr[index++] = '2';
      break;
    case (2):
      ip_addr[index++] = '3';
      break;
    case (3):
      ip_addr[index++] = '4';
      break;
    default:
      assert(0);
  }

  ip_addr[index++] = '.';

  switch(receiver_id % 2)
    {
    case (0):
      ip_addr[index++] = '1';
      break;
    case (1):
      ip_addr[index++] = '2';
      break;
    default:
      assert(0);
  }
  
  ip_addr[index++] = '\0';
}

void *run_tcp_sender(void *arg)
{
  struct tcp_sender *sender = (struct tcp_sender *) arg;
  struct packet outgoing;
  struct gen_packet packet;
  int count = 0;
  int i;
  struct timespec ts;

  ts.tv_sec = 0;

  uint64_t start_time = sender->start_time;
  uint64_t end_time = start_time + sender->duration;
  uint64_t next_send_time = start_time;

  int socket_fds[MAX_SOCKETS];
  int return_vals[MAX_SOCKETS];
  int bytes_left[MAX_SOCKETS];
  char *buffers[MAX_SOCKETS];
  for (i = 0; i < MAX_SOCKETS; i++)
    socket_fds[i] = -1;

  fd_set wfds, efds;
  FD_ZERO(&wfds);
  FD_ZERO(&efds);

  // Generate the first outgoing packet
  gen_next_packet(sender->gen, &packet);
  next_send_time = start_time + packet.time;

  outgoing.sender = sender->id;
  outgoing.receiver = packet.dest;
  outgoing.send_time = next_send_time;
  outgoing.size = packet.size;
  outgoing.id = count++;

  while (current_time() < start_time);

  while (current_time() < end_time)
    {
      // Set timeout for when next packet should be sent
      uint64_t time_now = current_time();
      uint64_t time_diff = 0;
      printf("time now: %"PRIu64", next send time: %"PRIu64"\n", time_now, next_send_time);
      if (next_send_time > time_now)
	time_diff = (next_send_time < end_time ? next_send_time : end_time) - time_now;
      assert(time_diff / sender->clock_freq < 1000 * 1000 * 1000);
      ts.tv_nsec = time_diff / sender->clock_freq;
      printf("nsec time diff goal: %ld\n", ts.tv_nsec);

      // Add fds to set and compute max
      // Add connecting fds to both sets
      FD_ZERO(&wfds);
      FD_ZERO(&efds);
      int max = 0;
      for (i = 0; i < MAX_SOCKETS; i++) {
	if (socket_fds[i] == -1)
	  continue;

	FD_SET(socket_fds[i], &wfds);
	FD_SET(socket_fds[i], &efds);
	if (socket_fds[i] > max)
	  max = socket_fds[i];
      }

      // Wait for a socket to be ready or for it to be time to start a new connection
      int retval = pselect(max + 1, NULL, &wfds, &efds, &ts, NULL);
      if (retval < 0)
	break;

      printf("pselect returned: %"PRIu64"\n", current_time());

      if (current_time() >= next_send_time) {
	// Open a new connection

	// Find an index to use
	int index = -1;
	for (i = 0; i < MAX_SOCKETS; i++) {
	  if (socket_fds[i] == -1)
	    {
	      index = i;
	      bytes_left[index] = -1;
	      break;
	    }
	}
	assert(index != -1);  // Otherwise, we have too many connections

	struct sockaddr_in sock_addr;
	struct sockaddr_in src_addr;
	int result;

	// Create a socket, set to non-blocking
	socket_fds[index] = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	assert(socket_fds[index] != -1);
	assert(fcntl(socket_fds[index], F_SETFL, O_NONBLOCK) != -1);
 
	// Initialize destination address
	memset(&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(PORT);
	// Choose the IP that corresponds to a randomly chosen core router
	//     char ip_addr[12];
	//choose_IP(outgoing.receiver, ip_addr);
	//printf("chosen IP %s for receiver %d\n", ip_addr, outgoing.receiver);
	const char *ip_addr = "10.0.2.15";
	result = inet_pton(AF_INET, ip_addr, &sock_addr.sin_addr);
	assert(result > 0);
 
	// Connect to the receiver
	printf("about to connect fd %d packet %d\n", socket_fds[index], outgoing.id);
	return_vals[index] = connect(socket_fds[index], (struct sockaddr *)&sock_addr, sizeof(sock_addr));
	assert(return_vals[index] >= 0 || errno == EINPROGRESS);

	int size_in_bytes = outgoing.size * MTU_SIZE;
	buffers[index] = malloc(size_in_bytes);
	bcopy((void *) &outgoing, buffers[index], sizeof(struct packet));

	// Generate the next outgoing packet
	gen_next_packet(sender->gen, &packet);
	next_send_time = start_time + packet.time;

	outgoing.sender = sender->id;
	outgoing.receiver = packet.dest;
	outgoing.send_time = next_send_time;
	outgoing.size = packet.size;
	outgoing.id = count++;
      }
      
      // Handle all existing connections that are ready
      for (i = 0; i < MAX_SOCKETS; i++)
        {
	  int result;
	  socklen_t result_len = sizeof(result);

	  if (socket_fds[i] == -1 || !FD_ISSET(socket_fds[i], &wfds))
	    continue;  // This fd is invalid or not ready

	  if (bytes_left[i] == -1) {
	    // check that connect was successful
	    assert(getsockopt(socket_fds[i], SOL_SOCKET, SO_ERROR,
			      &result, &result_len) == 0);
	    assert(result == 0);

	    // send data
	    struct packet *outgoing_data = (struct packet *) buffers[i];
	    bytes_left[i] = outgoing_data->size * MTU_SIZE;
	    return_vals[i] = send(socket_fds[i], buffers[i], bytes_left[i], 0);
	    /*	    printf("sent, \t\t%d, %d, %d, %"PRIu64", %d\n", bytes_left[i],
		   outgoing_data->sender, outgoing_data->receiver,
		   outgoing_data->send_time, outgoing_data->id);*/

	    printf("sent on fd %d packet %d\n", socket_fds[i], outgoing_data->id);
	  }
	  else {
	    printf("about to shutdown fd %d\n", socket_fds[i]);
	    // check that send was succsessful
	    assert(return_vals[i] == bytes_left[i]);

	    // close socket
	    (void) shutdown(socket_fds[i], SHUT_RDWR);
	    close(socket_fds[i]);

	    // clean up state
	    socket_fds[i] = -1;
	    free(buffers[i]);
	  }
      }

    }

}
