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

#define NUM_CORES 4
 
void tcp_sender_init(struct tcp_sender *sender, struct generator *gen, uint32_t id, uint64_t start_time, uint64_t duration)
{
  sender->gen = gen;
  sender->id = id;
  sender->start_time = start_time;
  sender->duration = duration;
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

int send_flow(struct packet *outgoing) {
  struct sockaddr_in sock_addr;
  struct sockaddr_in src_addr;
  int result;

  // Create a socket
  int sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  assert(sock_fd != -1);
 
  // Initialize destination address
  memset(&sock_addr, 0, sizeof(sock_addr));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(PORT);
  // Choose the IP that corresponds to a randomly chosen core router
  char ip_addr[12];
  choose_IP(outgoing->receiver, ip_addr);
  printf("chosen IP %s for receiver %d\n", ip_addr, outgoing->receiver);
  result = inet_pton(AF_INET, ip_addr, &sock_addr.sin_addr);
  assert(result > 0);
 
  // Connect to the receiver
  assert(connect(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) != -1);

  int size_in_bytes = outgoing->size * MTU_SIZE;
  char *buffer = malloc(size_in_bytes);
  bcopy((void *) outgoing, buffer, sizeof(struct packet));
 
  int ret = send(sock_fd, buffer, size_in_bytes, 0);
  printf("sent, \t\t%d, %d, %d, %"PRIu64", %d\n", outgoing->size, outgoing->sender, outgoing->receiver, outgoing->send_time, outgoing->id);
  
  /* shutdown when done */
  (void) shutdown(sock_fd, SHUT_RDWR);
 
  close(sock_fd);

  return 1;
}

void *run_tcp_sender(void *arg)
{
  struct tcp_sender *sender = (struct tcp_sender *) arg;
  struct packet outgoing;
  struct gen_packet packet;
  int count = 0;

  uint64_t start_time = sender->start_time;
  uint64_t end_time = start_time + sender->duration;
  uint64_t next_send_time = start_time;

  while (current_time() < start_time);

  while (current_time() < end_time)
    {
      gen_next_packet(sender->gen, &packet);
      next_send_time += packet.time;

      outgoing.sender = sender->id;
      outgoing.receiver = packet.dest;
      outgoing.send_time = next_send_time;
      outgoing.size = packet.size;
      outgoing.id = count++;

      while (current_time() < next_send_time);

      assert(send_flow(&outgoing) != 0);
    }

}
