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
 
void tcp_receiver_init(struct tcp_receiver *receiver, uint32_t id, uint32_t num_machines)
{
  receiver->id = id;
  receiver->num_machines = num_machines;
}

int run_tcp_receiver(struct tcp_receiver *receiver)
{
  int i;
  struct sockaddr_in sock_addr;

  // Create a socket
  int sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if(sock_fd == -1)
  {
    printf("can not create socket\n");
    return 0;
  }
 
  // Initialize socket address
  memset(&sock_addr, 0, sizeof(sock_addr));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(PORT);
  sock_addr.sin_addr.s_addr = htonl(INADDR_ANY);
 
  // Bind the address to the socket
  if(bind(sock_fd,(struct sockaddr *)&sock_addr, sizeof(sock_addr)) == -1)
  {
    printf("error bind failed\n");
    close(sock_fd);
    return 0;
  }
 
  // Listen for incoming connections
  if(listen(sock_fd, 10) == -1)
  {
    printf("error listen failed\n");
    close(sock_fd);
    return 0;
  }

  const int MAX_CONNECTIONS = 10;
  int connected_fds[MAX_CONNECTIONS];
  int bytes_left[MAX_CONNECTIONS];
  struct packet packets[MAX_CONNECTIONS];
  for (i = 0; i < MAX_CONNECTIONS; i++)
    connected_fds[i] = -1;

  fd_set rfds;
  FD_ZERO(&rfds);
  printf("sock fd: %d\n", sock_fd);

  for(;;)
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
    int retval = select(max + 1, &rfds, NULL, NULL, NULL);
    assert(retval > 0);
    
    if (FD_ISSET(sock_fd, &rfds))
      {
	// Accept a new connection
	int accept_fd = accept(sock_fd, NULL, NULL);
	printf("ACCEPTING new connection for fd: %d\n", accept_fd);
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
	  printf("READING new connection for fd: %d\n", ready_fd);

	  // Read first part of flow
	  struct packet *incoming = &packets[ready_index];
	  int bytes = read(ready_fd, incoming, sizeof(struct packet));
	  printf("%d bytes intially\n", bytes);
	  assert(bytes == sizeof(struct packet));
	  bytes_left[ready_index] = incoming->size * MTU_SIZE - sizeof(struct packet);
	}
	else {
	  // Read in data
	  int count = bytes_left[ready_index] < MTU_SIZE ? bytes_left[ready_index] : MTU_SIZE;
	  int bytes = read(ready_fd, buf, count);
	  bytes_left[ready_index] -= bytes;
	  printf("READ %d more bytes read for fd %d\n", bytes, ready_fd);

	  if (bytes_left[ready_index] == 0)
	    {
	      // This flow is done!
	      struct packet *incoming = &packets[ready_index];
	      printf("received data of size %d from %d to %d. times:%"PRIu64", %"PRIu64". fd: %d\n",
		     incoming->size, incoming->sender, incoming->receiver,
		     incoming->send_time, get_time(), ready_fd);
 
	      if (shutdown(ready_fd, SHUT_RDWR) == -1)
		{
		  printf("can not shutdown socket\n");
		  close(ready_fd);
	          close(sock_fd);
		  return 0;
		}
	      close(ready_fd);
	  
	      // Remove from set
	      FD_CLR(ready_fd, &rfds);
	      connected_fds[ready_index] = -1;
	    }
	}
      }

  }
      
 
  close(sock_fd);
  return 1;  
}

int main(void)
{
  struct tcp_receiver receiver;

  tcp_receiver_init(&receiver, 2, 8);
  run_tcp_receiver(&receiver);
}
