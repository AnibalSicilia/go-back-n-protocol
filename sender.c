#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#define SIZE 1024
#define MAXTRIES 10 /* Tries before giving up */

struct packet {
  	int ack_no;	// used by receiver to keep track of received data
	int seq_no;	// used by sender to keep track of sent data
	int length;	// the length of data
	long f_size;	// the total size of the file being sent
	int checksum;
  	char buf[SIZE];
};

void syserr(char *msg) {
	perror(msg);
	exit(-1);
}

int checksum(struct packet aPacket) {
	int checksum;
	int i;

	checksum = aPacket.seq_no;
	checksum = checksum + aPacket.ack_no;
	checksum = checksum + aPacket.f_size;
	for(i=0; i<aPacket.length; i++) {
		
		checksum = checksum + (int)aPacket.buf[i];
	}
	checksum = 0-checksum;
	//aPacket.checksum = checksum;
	return checksum;
}

int isCorrupted(struct packet aPacket) {
	int checksum;
	int i;
	
	checksum = aPacket.seq_no;
	checksum = checksum + aPacket.ack_no;
	checksum = checksum + aPacket.f_size;
	if(aPacket.length > SIZE)
		return 1;
	for(i=0; i<aPacket.length; i++) {
		checksum = checksum + (int)(aPacket.buf[i]);
	}
	if((aPacket.checksum + checksum) == 0)
		return 0;
	else
		return 1;
}


/* 
 * The main function.
 */
int main(int argc, char* argv[])
{
	int windowsize;
	int sockfd;
	int send_port;
	int receiver_port;
	struct sockaddr_in send_sockaddr; // client's socket address (ip address + port number)
	struct sockaddr_in receiver_sockaddr;	// server's socket address (ip address + port number)
	struct hostent* receiver_address;
	//struct sigaction signal_handler;
	char *filename;	// will hold the value entered in argv[4]
	
    if(argc != 6) {					 //argv[1]     argv[2]       argv[3]         argv[4]      argv[5]
		fprintf(stderr, "Usage: %s <sender-port> <receiver-ip> <receiver-port> <filename> <max-windows-size>\n", argv[0]);
        return 1;
    }

	// get input (argv[n]) values
	send_port = atoi(argv[1]);	// get sender's port number

	printf("Trying to connect to receiver at address %s through port %s\n", argv[2], argv[3]);
	receiver_address = gethostbyname(argv[2]); // get receiver's ip address
	if(!receiver_address) {
		fprintf(stderr, "ERROR: no such host: %s\n", argv[1]);
    	return 2;
	}
	receiver_port = atoi(argv[3]);	// get receiver's port number
	filename = argv[4]; // get client's filename
	// get windows size for a value <= 100
	if((windowsize = atoi(argv[5])) > 100) {
		fprintf(stderr, "ERROR: windows size too big: %d. Process will now exit.\n", windowsize);
    	return 3;
	}

	// set up the sender's address
	memset(&send_sockaddr, 0, sizeof(send_sockaddr));
	send_sockaddr.sin_family = AF_INET;
	send_sockaddr.sin_port = htons(send_port);

	// set up receiver's address
	memset(&receiver_sockaddr, 0, sizeof(receiver_sockaddr));
  	receiver_sockaddr.sin_family = AF_INET;	// IPv4 family
  	receiver_sockaddr.sin_addr = *((struct in_addr*)receiver_address->h_addr);	// IP address
  	receiver_sockaddr.sin_port = htons(receiver_port);	// the port number
	
 	// create socket
	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);	// open socket
 	if(sockfd < 0) 
		syserr("can't open sender's socket");
	printf("create sender's socket...\n");

	// bind the socket to the specified address and port
	if(bind(sockfd, (struct sockaddr*)&send_sockaddr, sizeof(send_sockaddr)) == -1) {
		syserr("could not bind the socket");
	}

	//create a timeout while waiting for packets (TTL). Uses <sys/time.h> library
	// Also set the time to obtain how long the file transfer lasted.
	struct timeval tv, tv_start, tv_end, tv_tt_time;
	double total_transfer_time;

	// For TTL (time to live)
 	tv.tv_sec = 0;
  	tv.tv_usec = 10000; // 10 miliseconds, as proposed
  	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
      	perror("Error");
	}

  	
	/*
	 * From now on goes all the file processing and (sending and receiving) from server handling
     */

	FILE *fp;
	long file_size; // how large the file to be send is
	int total_packets; // the total packet amount
	int packets_acknowledged = 0;
	int packets_sent = 0;
	int cwp; // current windows packet
	int pd_size; // data amount in current packet
	int isSend;	// to keep track of sent files
	int i; // temporary variables for multiple use
	int iterator;	// traverses the entire FILE fp
	int addrlen;
	int current_ack_no = 0;	// to keep track of most recent acknowledge number (I use it in the if( .... curr_ack_no != iterator) thing) (line 257)
	int wc; // the counter for windowsize. Controls the iterations in the sender for loop
	int base = 0; // the base int will controls where the iterator's position as well as the starting point for the windowsize. 
	int size_handler; // handles comparison for amount destined to fill buffer data
	int tries = 0;
	struct packet *aPacket; // handles current received packet

	
	fp = fopen(filename, "r");	// opened_file is a FILE type
	if(fp == NULL) {	// if there is no file...
		syserr("Error Opening the file, the system will now exit");
	}                      
	// if file exists...
	fseek(fp, 0L, SEEK_END); // goto end of file
	file_size = ftell(fp); // then, obtain file's length
	fseek(fp, 0L, SEEK_SET);
	printf("The size of fp is: %ld\n", file_size);

	// create an 
	char *data = NULL;
	data = (char *) malloc(sizeof(char *) * (file_size  + 1));
	int rd;
	if((rd = fread(data, sizeof(char), file_size, fp)) == 0)
		printf("Error reading file\n");
	printf("the size of rd (from fread) is %d\n", rd);
	// find out how many packets are going to be send
	if(file_size > 0){
		total_packets = file_size / SIZE;
		if(total_packets == 0)
			total_packets = 1;	// for data of size less than buffer size
		printf("the total amount of  packets is: %d\n", total_packets);
	}
	else
		syserr("there is nothing to be sent\n");

	// Calculate the total file transaction
	gettimeofday(&tv_start, NULL);
	
	while(packets_acknowledged < total_packets)
	{		
		iterator = base;	// base holds the current acknowledged data track in file
		for(wc=0; wc < windowsize; wc++) {

			// current packet
			aPacket = (struct packet *) malloc(sizeof(struct packet));
			if(aPacket == NULL)
				syserr("Memory allocation failed\n");

			// here starts windows packet's sending

			// if last packet...	
			// I use a little trick here because these packets are not yet acknowledged 
			// until checksum is performed after recvfrom() gets an acknowledge message
			size_handler = file_size - ((packets_acknowledged + wc) * SIZE);
			if(size_handler < SIZE) {
				if(file_size < SIZE)
					pd_size = file_size;
				else
				{	
					pd_size = size_handler; // adjust buffer for last packet_sent
					printf("I just adjusted buffer for last packet sent\n");
					printf("pd_size is %d and wc value is %d\n", pd_size, wc);
				}
			}
			else 	// pd_size = sizeof(buf)
				pd_size = SIZE;	

			//filling up packet struct	
			aPacket->ack_no = 1;			
			aPacket->length = pd_size;	
			// fill up buffer.
			// The idea is to have an iterator that runs through the entire file and goes
			// back if necessary to fill up the dropped || NACK packets	
			for(i=0; i<pd_size; i++) {
				aPacket->buf[i] = data[iterator];
				iterator++;
			}
			aPacket->seq_no = iterator;
			aPacket->f_size = file_size;
			aPacket->checksum = checksum(*aPacket);	
//				printf("checksum for packet %d is %d\n", packets_sent, aPacket->checksum);	

			// Convert integers to network format
			aPacket->ack_no = htonl(aPacket->ack_no);						
			aPacket->length = htonl(aPacket->length);
			aPacket->seq_no = htonl(aPacket->seq_no);
			aPacket->f_size = htonl(aPacket->f_size); 
			aPacket->checksum = htonl(aPacket->checksum);
		
			// send packet
			addrlen =  sizeof(receiver_sockaddr);
			isSend = sendto(sockfd, aPacket, sizeof(struct packet), 0, (struct sockaddr *)&receiver_sockaddr, addrlen);

			if(isSend < 0)
				syserr("An error occurred when sending packet");
			printf("Packet # %d was sent to receiver\n", ++packets_sent);
			if((packets_acknowledged + wc) >= total_packets)
				break;
			free(aPacket);
	}	// end for loop that sends packets

		/*
			From this point on the program will handle all the receiving files thing
		*/
		int check_corruption;	// hold value for returned isCorrupted() function
		int counter_window = 0;	// counter_window will be updated with packets_acknowledged variable
		ssize_t recv_temp;	// will hold current value of recvfrom() function
		int received_packets = 0; // For testing return of isCorrupted() packets only. (line 279)
		
		while((counter_window != windowsize)) // goes all the way up to line 317. After that just the close() functions
		{
			struct packet s_recv;

			// attempt to RECEIVE a packet. 
			recv_temp = recvfrom(sockfd, &s_recv, sizeof(s_recv), 0, (struct sockaddr*)&receiver_sockaddr, &addrlen);

			// If sender cannot get a packet, or if stopped receiving and did not acknowledged 
			// all sent data from from for loop above (line 185)
			// if not receiving but still waiting for some packets being acknowledged...
			if((recv_temp < 0) && (current_ack_no != iterator))
			{
//				printf("Current_ack_no is: %d, and iterator is at %d\n", current_ack_no, iterator);	// testing purposes
				printf("Somehow a packet resulted lost...\n");
				if(tries < MAXTRIES) {
					tries++;
					printf("time out, %d keep trying to resend packet...\n", MAXTRIES - tries);	
					//continue;	// maybe lost but maybe receiver did received, so go on
					break; // trying to correct continue....				
				}
				if(tries == MAXTRIES)
					syserr("failed to receive acknowledgement from server");
			}


			// acknowledge was received from receiver. Get data from packets
			s_recv.ack_no = ntohl(s_recv.ack_no);
			s_recv.seq_no = ntohl(s_recv.seq_no);
			s_recv.f_size = ntohl(s_recv.f_size);
			s_recv.checksum = ntohl(s_recv.checksum);
			s_recv.length = ntohl(s_recv.length);
			
			check_corruption = isCorrupted(s_recv);
			current_ack_no = s_recv.ack_no; // current acknowledge number for test in line 254 is updated here
			
			// YOU USE received_packets TO PRINT A CORRUPTION MESSAGE!!!!! (used below, and updated in line 285)		
			if(check_corruption == 0) // a closing bracket in line 316
			{
				if(s_recv.ack_no > base)
				{
					received_packets++; // do the received_packets upgrading here because, just cared for acknowledged packets
					base = s_recv.ack_no; // to control iterator's position
					if(pd_size == SIZE)
						packets_acknowledged = s_recv.ack_no / SIZE;	// get amount of correct received data in packets
					else
						packets_acknowledged++;
					printf("Amount of packets correctly received by receiver: %d\n", packets_acknowledged);

					// counter windows is updated for while conditional at line 245. 
					// It will be reset to ZERO at line 239 just before this while loop				
					counter_window++;
					
					// if counter windows equals the amount of packets sent, then everything was perfectly executed
					if(counter_window == packets_sent) {
						tries = 0;	// reset tries				
						break;	// get out of receiving while loop
					}
					else {	// maybe more packets, maybe failed. TAKE A LOOK AT THIS
						tries = 0;	// reset tries for next packet
						continue;
					}
				}
				else
				// s_recv.ack_no < base
				continue; // maybe will be lucky next iteration.

			}
			else {
				// Handle bad checksum packets
				break; // I think a break will do ...
			}
		}	// end receiving while
		
	}// end go-back-n while

	gettimeofday(&tv_end, NULL);
	timersub(&tv_end, &tv_start, &tv_tt_time);
	double tv_seconds = (double) tv_tt_time.tv_sec;
	double tv_useconds = (double) tv_tt_time.tv_usec;
	total_transfer_time = (tv_seconds + (tv_useconds/1000000));
	double tf_size = (double) file_size;
	tf_size = tf_size * 8; // get the amount in 'bits'


	printf("\nThe file has been sent completely to the receiver\n");
	printf("Packets lost during transaction: %d\n", packets_sent - packets_acknowledged);
	printf("The total file transfer lasted for: %f seconds \n", total_transfer_time);
	printf("for a throughput of %f bits per second\n", tf_size/total_transfer_time);

	fclose(fp);
	free(data);
  	close(sockfd);
  	return 0;
}
