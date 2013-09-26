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
 
void tcp_receiver_init(struct tcp_receiver *receiver, uint32_t id, uint32_t num_machines)
{
  receiver->id = id;
  receiver->num_machines = num_machines;
}

int run_tcp_receiver(struct tcp_receiver *receiver)
{
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
 
  // Accept incoming connections
  for(;;)
  {
    int connect_fd = accept(sock_fd, NULL, NULL);

    if(connect_fd < 0)
    {
      printf("error accept failed\n");
      close(sock_fd);
      return 0;
    }
 
    /* perform read write operations ... */
    struct packet incoming;
    int bytes = read(connect_fd, &incoming, sizeof(struct packet));
    printf("%d bytes intially\n", bytes);
    assert(bytes == sizeof(struct packet));
    int bytes_to_read = incoming.size * MTU_SIZE - sizeof(struct packet);

    char buf[MTU_SIZE];
    while(bytes_to_read > 0)
      {
	int count = bytes_to_read < MTU_SIZE ? bytes_to_read : MTU_SIZE;
	bytes = read(connect_fd, buf, count);
	bytes_to_read -= bytes;
	printf("%d more bytes read\n", bytes);
      }
    assert(bytes_to_read == 0);
    printf("received data of size %d from %d to %d. times:%"PRIu64", %"PRIu64"\n", incoming.size, incoming.sender, incoming.receiver, incoming.send_time, get_time());
 
    if (shutdown(connect_fd, SHUT_RDWR) == -1)
    {
      printf("can not shutdown socket\n");
      close(connect_fd);
      close(sock_fd);
      return 0;
    }
    close(connect_fd);
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
