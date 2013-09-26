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
 
void tcp_sender_init(struct tcp_sender *sender, struct generator *gen, uint32_t id)
{
  sender->gen = gen;
  sender->id = id;
}

int send_flow(struct packet *outgoing) {
  struct sockaddr_in sock_addr;
  struct sockaddr_in src_addr;
  int result;

  // Create a socket
  int sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock_fd == -1)
  {
    printf("cannot create socket\n");
    return 0;
  }
 
  // Initialize destination address
  memset(&sock_addr, 0, sizeof(sock_addr));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(PORT);
  // TODO: choose IP that correspond to a randomly chosen core router
  result = inet_pton(AF_INET, "10.0.2.15", &sock_addr.sin_addr);
  if (result <= 0)
  {
    printf("error: invalid address\n");
    close(sock_fd);
    return 0;
  }
 
  // Connect to the receiver
  if (connect(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) == -1)
  {
    printf("connect failed\n");
    close(sock_fd);
    return 0;
  }

  char *buffer = malloc(outgoing->size);
  bcopy((void *) outgoing, buffer, sizeof(struct packet));
 
  int ret = send(sock_fd, buffer, outgoing->size, 0);
  printf("sent data at time %"PRIu64"\n", outgoing->send_time);
  
  /* shutdown when done */
  (void) shutdown(sock_fd, SHUT_RDWR);
 
  close(sock_fd);

  return 1;
}

int run_tcp_sender(struct tcp_sender *sender, uint64_t duration)
{
  struct packet outgoing;
  struct gen_packet packet;

  uint64_t start_time = get_time();
  uint64_t end_time = start_time + duration;
  uint64_t next_send_time = start_time;
  printf("start time: %"PRIu64"\n", start_time);
  printf("end time: %"PRIu64"\n", end_time);

  while (get_time() < end_time)
    {
      gen_next_packet(sender->gen, &packet);
      next_send_time += packet.time;

      outgoing.sender = sender->id;
      outgoing.receiver = packet.dest;
      outgoing.send_time = next_send_time;
      outgoing.size = packet.size;

      while (get_time() < next_send_time);

      if (send_flow(&outgoing) == 0)
	return 0;
    }

  return 1;
}

int main(void)
{
  struct generator gen;
  struct tcp_sender sender;

  printf("time: %"PRIu64"\n", get_time());

  gen_init(&gen, POISSON, ONE_SIZE, 100000, 152, 5, 8);
  tcp_sender_init(&sender, &gen, 5);
  
  if (run_tcp_sender(&sender, 10000000000LL) == 0)
    printf("error\n");
}
