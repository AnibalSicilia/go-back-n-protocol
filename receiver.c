#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>

#define SIZE 1024

struct packet {
  	int ack_no;
	int seq_no;
	int length;
	long file_size;
	int checksum;
  	char buf[SIZE];
};

void syserr(char *msg) {
	perror(msg);
	exit(-1);
}

int tries;
int flag;
void set_signal(int useless) {
	tries = tries + 1;
	flag = 1;
}

int checksum(struct packet p) {
	int checksum;
	int i;

	checksum = p.seq_no;
	checksum = checksum + p.ack_no;
	checksum = checksum + p.file_size;

	for(i=0; i<p.length; i++) {		
		checksum = checksum + (int)p.buf[i];
	}

	checksum = 0-checksum;
	//aPacket.checksum = checksum;
	return checksum;
}

int isCorrupted(struct packet *p) {
	int checksum;
	int i;
	printf("Checking for corruption on packet\n");	
	checksum = p->seq_no;
	checksum = checksum + p->ack_no;
	checksum = checksum + p->file_size;
	if(p->length > SIZE)
		return 1;
	for(i=0; i<p->length; i++) {
//printf("i = %d and p->buf[%d] = %d\n", i , i, p->buf[i]);
		checksum = checksum + (int)(p->buf[i]);
	}
	if((p->checksum + checksum) == 0)
		return 0;
	else
		return 1;
}


/*
	The main function
*/
int main(int argc, char **argv) {

	FILE *fp;
	int sockfd;
	int portno;
	socklen_t clilen;	// contain the size (in bytes) of the structure pointed to by addr (accept());
	struct sockaddr_in send_sockaddr;	// client's socket address (ip addres + port number)
	struct sockaddr_in receiver_sockaddr;	// server's socket address (ip address + port number)
	int seq_no, n, i;
	int dropped_packages = 0;

	if(argc != 3) {					 //argv[1]     argv[2]
		fprintf(stderr, "Usage: %s <port-number> <filename>\n", argv[0]);
		return 1;
	}

	portno = atoi(argv[1]); // Retrieve port number as an integer
	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);	// open UDP socket
	
	// open file to write
	fp = fopen(argv[2], "w");

	memset(&receiver_sockaddr, 0, sizeof(receiver_sockaddr));
	receiver_sockaddr.sin_family = AF_INET;	/// IPv4 family
	receiver_sockaddr.sin_addr.s_addr = INADDR_ANY;	// IP address
	receiver_sockaddr.sin_port = htons(portno); // the port number

	if(bind(sockfd, (struct sockaddr *) &receiver_sockaddr, sizeof(receiver_sockaddr)) < 0) {
		syserr("bind() function failed");	// i.e. can't assign address (receiver_sockaddr) to a socket (listenfd)
	}
	printf("binded socket to port %d\n", portno);
	
	//create a timeout while waiting for packets. Uses <sys/time.h> library
	struct timeval tv;
 	tv.tv_sec = 20;
  	tv.tv_usec = 100000;
  	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
      	perror("Error");
	}

	int f_size = 0;
	int seq_accum = 0;
	int addrlen = sizeof(send_sockaddr);
	int checkCorruption;	// keep return value of isCorrupted()
	int packet_received = 0;
	struct packet *aPacket;

	while(1) {

		aPacket = (struct packet *) malloc(sizeof(struct packet));
			if(aPacket == NULL)
				syserr("Memory allocation failed\n");

		n = recvfrom(sockfd, aPacket, sizeof(struct packet), 0, (struct sockaddr*)&send_sockaddr, &addrlen);
  		if(n < 0) {
			syserr("can't receive from client"); 
		}

		aPacket->ack_no = ntohl(aPacket->ack_no);
		aPacket->seq_no = ntohl(aPacket->seq_no);
		// extract seq_no to send back as acknowledgement if checksum is correct
		seq_no = aPacket->seq_no;
		aPacket->length = ntohl(aPacket->length);
		aPacket->file_size = ntohl(aPacket->file_size);
		aPacket->checksum = ntohl(aPacket->checksum);

		// check for corruption
		checkCorruption = isCorrupted(aPacket);
//		printf("the value of checkCorruption is: %d\n", checkCorruption);
		if(checkCorruption == 0) {
			// first of all, get correct file size (it could be wrong if checksum was wrong
			f_size = aPacket->file_size;
			// if accumulated data plus current packet's length equals the sequence number just received.... (ends in line 170)
			if((seq_accum + aPacket->length) == aPacket->seq_no)  {
				// this one will always keep the received data amount
				seq_accum = seq_accum + aPacket->length;
//				printf("Data Received increased  to: %d\n", seq_accum);
				fwrite(aPacket->buf, aPacket->length, 1, fp);

				// HERE FOR TESTING PURPOSES FILE SIZE AND ACKNOWLEDGED SEQUENCE WILL BE PRINTED OUT
				printf("file_size = %ld, seq_acum = %d and packet number = %d\n", aPacket->file_size, seq_accum, ++packet_received);


				//memset(&aPacket, 0, sizeof(aPacket));
				aPacket->ack_no = seq_no;
				aPacket->seq_no = 0;

				for(i=0; i<SIZE; i++)
					aPacket->buf[i] = 0;
				aPacket->length = 0;
				aPacket->file_size = 0;
				aPacket->checksum = checksum(*aPacket);

				aPacket->ack_no = htonl(aPacket->ack_no);
				aPacket->seq_no = htonl(aPacket->seq_no);
				aPacket->length = htonl(aPacket->length);
				aPacket->checksum = htonl(aPacket->checksum);
				n = sendto(sockfd, aPacket, sizeof(struct packet), 0, (struct sockaddr*)&send_sockaddr, addrlen);
				if(n < 0) {
					syserr("can't send to sender");
				}
				else {
					// YOU NOW NEED TO CHECK IF THE FILE RECEIVED WAS THE LAST ONE NEEDED
					if(seq_accum <= f_size) {
						printf("send acknowledgement...\n");
						dropped_packages = 0;
						if(seq_accum == f_size) {
							printf("All packets have been received\n");
							break;
						}
					}
					else {
						printf("All packets have been received\n");
						break;
					}
				}
			}
			else {
				// TODO: seq_accum + aPacket.length) != aPacket.seq_no
				printf("packet %d out of sequence. Will be Dropped\n", packet_received + 1);
				dropped_packages++;
				if(dropped_packages > 500)
					syserr("reached limit of more than 500 consecutive damaged packets. Drop connection\n");
				continue;
			}
		}	// end if(checksum is correct)
		else {
			// TODO: A CORRUPTED PACKET WAS RECEIVED
			printf("received corrupted packet. Send negative acknowledgement\n");
				dropped_packages++;
				if(dropped_packages > 500)
					syserr("reached limit of more than 500 consecutives damaged packets. Drop connection\n");
			continue;
		}
		free(aPacket);
	}
	close(sockfd);
	fclose(fp);
	return 0;
}

