/*
 * tcp_sender.c
 *
 *  Created on: September 24, 2013
 *      Author: aousterh
 */

#include "tcp_sender.h" 

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
 
int run_tcp_sender()
{
  struct sockaddr_in sock_addr;
  int result;

  // Create a socket
  int sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  printf("created a socket\n");
  if (sock_fd == -1)
  {
    printf("cannot create socket");
    return 0;
  }
 
  // Initialize socket address
  memset(&sock_addr, 0, sizeof(sock_addr));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(1100);
  result = inet_pton(AF_INET, "10.0.2.15", &sock_addr.sin_addr);
  if (result <= 0)
  {
    printf("error: invalid address");
    close(sock_fd);
    return 0;
  }
 
  // Connect to the receiver
  if (connect(sock_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) == -1)
  {
    printf("connect failed");
    close(sock_fd);
    return 0;
  }
 
  printf("connected!\n");
 
  /* perform read write operations ... */
  send(sock_fd, "hello there!", 12, 0);
  printf("sent data\n");
  
  /* shutdown when done */
  (void) shutdown(sock_fd, SHUT_RDWR);
 
  close(sock_fd);

  return 1;
}

int main(void)
{
  run_tcp_sender();
}
