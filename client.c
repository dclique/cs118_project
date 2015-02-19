#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "packet.c"
#include <fcntl.h>
#include <sys/time.h>

#define PROB_LOSS 0.3
#define PROB_CORRUPT 0.3

int BUF_SIZE = 1024;

void error(char *msg) {
  perror(msg);
  exit(0);
}

int main(int argc, char **argv) {

  FILE *filecopy;
  

  int serverlen;
  int n;
  int portno;
  int sockfd;
  struct sockaddr_in server_addr;
  struct hostent *server;
  char *hostname;
  char *filename;
  char buf[BUF_SIZE];

  int expected_ack_no = 0;
  struct timeval t1, t2, t3;

  //check command line arguments for correct usage
  if (argc != 4) {
    fprintf(stderr,"usage: %s <hostname> <server port> <filename>\n", argv[0]);
    exit(0);
  }
  hostname = argv[1];
  portno = atoi(argv[2]);
  filename = argv[3];

  //set up socket
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) 
    error("ERROR opening socket");

  server = gethostbyname(hostname);

  if (server == NULL) {
    fprintf(stderr,"ERROR, no such host\n");
    exit(0);
  }

  bzero((char *) &server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr, server->h_length);
  server_addr.sin_port = htons(portno);

  
  //packet
  struct packet request;
  bzero((char *) &request, sizeof(request));
  strcpy(request.data, filename);
  request.packet_type = REQ;
  request.length = sizeof(request.packet_type) + sizeof(request.length)*2 + strlen(filename) + 1;
  serverlen = sizeof(server_addr);
  gettimeofday(&t1, NULL);
  n = sendto(sockfd, &request, request.length, 0, (struct sockaddr*) &server_addr, serverlen);
  if (n < 0)
    error("ERROR in sendto");
  printf("Requested file %s\n", request.data);


  filecopy = fopen(strcat(filename, "_copy"), "wb");
  //make a new packet for received information
  struct packet received_pkt;
  struct packet ack_pkt;



  char* receive_buffer;
  srand(time(NULL));
  while(1){
    if (recvfrom(sockfd, &received_pkt, sizeof(received_pkt), 0, (struct sockaddr*) &server_addr, (socklen_t*) &serverlen) < 0)
      error("ERROR receiving from server");

    //packet loss
    if((double) rand()/(double) RAND_MAX < PROB_LOSS) {
      gettimeofday(&t2, NULL);
      timersub(&t2,&t1,&t3);
      printf("TIME: %d.%d - A packet was lost with seq no: %d\n", (int) t3.tv_sec, (int) t3.tv_usec, received_pkt.seq_no);	
    }

    //packet corruption
    else if((double) rand()/(double) RAND_MAX < PROB_CORRUPT){
      gettimeofday(&t2, NULL);
      timersub(&t2,&t1,&t3);
      printf("TIME: %d.%d - A packet was corrupted with seq no: %d\n", (int) t3.tv_sec, (int) t3.tv_usec, received_pkt.seq_no);
      //printf("A packet was corrupted with seq no: %d\n", received_pkt.seq_no);
      //send an ACK for the last expected ACK number -1
      ack_pkt.packet_type = ACK;
      ack_pkt.seq_no = expected_ack_no - 1;
      ack_pkt.length = sizeof(int)*3;
      n = sendto(sockfd, &ack_pkt, ack_pkt.length, 0, (struct sockaddr*) &server_addr, serverlen);
      if (n < 0)
        error("ERROR in sendto");
    }

    //normal case
    else{
      gettimeofday(&t2, NULL);
      timersub(&t2,&t1,&t3);
      printf("TIME: %d.%d - Received a packet of seq. no %d and size %d\n", (int) t3.tv_sec, (int) t3.tv_usec, received_pkt.seq_no, received_pkt.length);
      //printf("Received a packet of seq. number %d and size: %d\n", received_pkt.seq_no, received_pkt.length);
      printf("        Packet contents are: %s\n", received_pkt.data);
      fflush(filecopy);
      fprintf(filecopy, "%s", received_pkt.data);
      fflush(filecopy);
      
      //send ACK
      if(received_pkt.seq_no == expected_ack_no){
        ack_pkt.packet_type = ACK;
        ack_pkt.seq_no = received_pkt.seq_no;
        ack_pkt.length = sizeof(int)*3;
	gettimeofday(&t2, NULL);
	timersub(&t2,&t1,&t3);
	printf("TIME: %d.%d - Packet seq. no is as expected, sending ACK with seq.number %d\n", (int) t3.tv_sec, (int) t3.tv_usec, ack_pkt.seq_no);
	//printf("Packet seq. number is as expected, sending ACK with seq. number %d\n", ack_pkt.seq_no);
        n = sendto(sockfd, &ack_pkt, ack_pkt.length, 0, (struct sockaddr*) &server_addr, serverlen);
        if (n < 0)
	  error("ERROR in sendto");
	if(received_pkt.packet_type == FIN)
	  break;
	expected_ack_no++;
      }
    }
    
  }
  printf("File has been successfully received.\n");

  fclose(filecopy);
  close(sockfd);

  return 0;
}
