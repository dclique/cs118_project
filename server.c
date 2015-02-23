#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "packet.c"

#define TIMEOUT 10000 //timeout period in microseconds
#define WINDOWSIZE 8 //size of window

void error(char *msg) {
  perror(msg);
  exit(0);
}

int main(int argc, char *argv[]) {
  int sockfd = 0;
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  int n;
  int pid;
  int portno;
  int clilen;
 
  int num_packets = 0;
  int base = 0; 
  int nextseqnum = 0;
  const int windowsize = WINDOWSIZE; 

  time_t timer;
  struct timeval t1;
  struct timeval t2;
  struct timeval t3;
  struct packet window[windowsize];

  //check command line arguments for correct usage
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(0);
  }
  portno = atoi(argv[1]);
	
  //make a socket
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
    error("ERROR opening socket");

  //give the server it's address
  bzero((char *) &server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(portno);

  if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    error("ERROR on binding");
	
  clilen = sizeof(client_addr);

  struct packet received_pkt;
  struct packet ack_pkt;
 
  while(1) {        

    if (recvfrom(sockfd, &received_pkt, sizeof(received_pkt), 0, (struct sockaddr*) &client_addr, (socklen_t*) &clilen) < 0)
      error("ERROR receiving from client");
		
    //the first packet we receive should always be a request
    if(received_pkt.packet_type == REQ){
      char* buffer = 0;
      char buffer2[SIZE];
      buffer2[0] = '\0';
      long length;
      time(&timer);
      gettimeofday(&t1, NULL);
      printf("Request packet received, size %d\n", received_pkt.length);
      printf("file requested: %s\n", received_pkt.data);
      
      //open file, save to buffer
      FILE* f;
      if((f = fopen(received_pkt.data, "r")) != NULL) {
        fseek(f, 0, SEEK_END);
	length = ftell(f);

	fseek(f, 0, SEEK_SET);
	buffer = malloc(length);
	if (buffer){
	  fread(buffer, 1, length, f);
        }
        fclose(f);
      }
      else{
	
	error("requested file not found");
      }

      //calculate the number of packets we need to send
      num_packets = length / (SIZE-1) + (length % (SIZE-1) != 0);
      printf("Number of packets to generate: %d\n", num_packets);
      int x = 0;

      int buffer2_length = 0;

      while(x < length){
        //if there is still space in the window and packets left to send
        if(nextseqnum < windowsize+base && (x < length)) {

          strncat(buffer2, &buffer[x],1);
          
          buffer2_length++;
          x++;
	  //if buffer2 is full, send it as a packet
          if(buffer2_length == SIZE-1 || x == length) {

	    struct packet data_pkt;
	    data_pkt.packet_type = DATA;
	    if(x==length)
	      data_pkt.packet_type = FIN;
	    data_pkt.seq_no = nextseqnum;
	    data_pkt.length = sizeof(data_pkt.packet_type) + sizeof(data_pkt.length)*2 + strlen(buffer2) + 1;

	    strcpy(data_pkt.data, buffer2);
	    gettimeofday(&t2, NULL);
	    timersub(&t2,&t1,&t3);
	    printf("TIME: %d.%d - Sending data packet with seq. number: %d\n", (int) t3.tv_sec, (int) t3.tv_usec, data_pkt.seq_no);
	    
	    //send the buffer
	    n = sendto(sockfd, &data_pkt, data_pkt.length, 0, (struct sockaddr*) &client_addr, clilen);
	    if (n < 0)
	      error("ERROR can't send data from server to client");
            
	    //add packet to an array, so we can send it again if timeout occurs
	    window[nextseqnum % windowsize] = data_pkt;

	    //empty the buffer so we can send the next packet
	    buffer2_length = 0;
	    buffer2[0] = '\0';
	    
	    if(nextseqnum <= num_packets){
	      nextseqnum++;
	    }	
	    
	  }
	  else {
	    continue;
	  }
        }
        else {
	  //refuse data
	  printf("refuse data\n");
        }

	//receiving an ACK
        int ack_not_received = 1;
        while(ack_not_received) {

	  //set up timer
	  fd_set fdset;
	  FD_SET(sockfd, &fdset);
	  struct timeval t;
	  t.tv_sec = 0;
	  t.tv_usec = TIMEOUT;

	  //if an ACK is received before timeout
	  if (select(sockfd + 1, &fdset, NULL, NULL, &t) > 0){
            n = recvfrom(sockfd, &received_pkt, sizeof(received_pkt), 0, (struct sockaddr*) &client_addr, (socklen_t*) &clilen);
	    ack_not_received = 0;
	  }
	  //if timeout occurs
	  else {
	    int unacked = nextseqnum - base;
	    gettimeofday(&t2, NULL);
	    timersub(&t2,&t1,&t3);
	    printf("TIME: %d.%d - timeout occured, resending %d packet(s)\n", (int) t3.tv_sec, (int) t3.tv_usec, unacked);
	    //printf("TIMEOUT occured, resending %d packet(s)\n", unacked);
	    int j;					
	    for(j = 0; j < unacked; j++){
	      n = sendto(sockfd, &window[(base+j)%8], window[(base+j)%8].length, 0, (struct sockaddr*) &client_addr, clilen);
	      gettimeofday(&t2, NULL);
	      timersub(&t2,&t1,&t3);
	      printf("TIME: %d.%d - Resending packet with seq. no: %d\n", (int) t3.tv_sec, (int) t3.tv_usec, window[(base+j)%8].seq_no);
	      //printf("Resending packet with seq. number %d\n", window[(base+j)%8].seq_no);
						
	      if (n < 0)
	        error("ERROR can't send data from server to client");
	    }
	  }
        }

        if(received_pkt.packet_type == ACK){
	  gettimeofday(&t2, NULL);
	  timersub(&t2,&t1,&t3);
	  printf("TIME: %d.%d - ACK received for seq no: %d\n", (int) t3.tv_sec, (int) t3.tv_usec, received_pkt.seq_no);
	  //printf("ACK received for seq no: %d\n", received_pkt.seq_no);
	  if(received_pkt.seq_no < base) { //packet corruption detected
	    nextseqnum = received_pkt.seq_no + 1;
	    x = nextseqnum*(SIZE-1);
	  }

	  else {
	    base = received_pkt.seq_no + 1; 
          }	
        }			
        if(base == num_packets) {
          break;
        }		
				
      }
    }
    printf("File have been successfully sent. \n");
    nextseqnum = 0;
    base = 0;
  }
	
  close(sockfd);
  return 0;
}
