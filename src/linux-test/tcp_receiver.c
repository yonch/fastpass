/*
 * tcp_receiver.c
 *
 *  Created on: September 24, 2013
 *      Author: aousterh
 */

#include "tcp_receiver.h" 
 
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
 
int run_tcp_receiver()
{
  struct sockaddr_in sock_addr;

  // Create a socket
  int sock_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  printf("created a socket!\n");
  if(sock_fd == -1)
  {
    printf("can not create socket");
    return 0;
  }
 
  // Initialize socket address
  memset(&sock_addr, 0, sizeof(sock_addr));
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(1100);
  sock_addr.sin_addr.s_addr = htonl(INADDR_ANY);
 
  // Bind the address to the socket
  if(bind(sock_fd,(struct sockaddr *)&sock_addr, sizeof(sock_addr)) == -1)
  {
    printf("error bind failed");
    close(sock_fd);
    return 0;
  }
  printf("bound\n");
 
  // Listen for incoming connections
  if(listen(sock_fd, 10) == -1)
  {
    printf("error listen failed");
    close(sock_fd);
    return 0;
  }
  printf("listen\n");
 
  // Accept incoming connections
  for(;;)
  {
    int connect_fd = accept(sock_fd, NULL, NULL);
    printf("accepted connection!\n");

    if(connect_fd < 0)
    {
      printf("error accept failed");
      close(sock_fd);
      return 0;
    }
 
    /* perform read write operations ... */
    char buf[15];
    int bytes = read(connect_fd, buf, sizeof(buf));
    printf("read data: %s, %d bytes\n", buf, bytes);
 
    if (shutdown(connect_fd, SHUT_RDWR) == -1)
    {
      printf("can not shutdown socket");
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
  run_tcp_receiver();
}
