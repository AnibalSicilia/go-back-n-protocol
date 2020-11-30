// This program was written by Jesse Domack. Thanks!!!
// Compile with "gcc twister.c -o twister".

// twister.c	-	This is the "man in the middle". This program receives packets
//					from the sender and forwards them to the receiver, and vice-versa.
//					For packets in both directions, the packets are queued and released
//					at specific intervals. In addition, given a probability for each event 
//					to occur, the packets are randomly corrupted, dropped, or sent in the
//					wrong order. This can happen in both directions.
					
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdint.h>
//#include "util.h"
#include <signal.h>

#define syserr(msg) { perror(msg); exit(-1); }

#define IN_BUFF_SIZE 65508

#define should_drop_packet(prob) (((float)(rand()%1000))/1000 < prob)
#define should_corrupt_packet(prob) (((float)(rand()%1000))/1000 < prob)
#define should_reorder_packet(prob) (((float)(rand()%1000))/1000 < prob)
#define can_reorder_packet(list) ((list).head && (list).head->next)

// corrupt a bit in the packet by randomly flipping one bit, note this bit can be
// anywhere in the packet, including the header
#define corrupt_packet(node) (node)->data[rand() % (node)->length] ^= 1 << rand() % 8

// The queues are implemented here as linked lists for optimal performance
struct list_node {
	uint16_t length;
	struct list_node *next;
	char data[];
};

// each queue maintains a pointer to the head and tail; new packets are added at the tail
// and removed from the head
struct linked_list {
	struct list_node *head;
	struct list_node *tail;
};

// constants for the output log
const char *twisted[] = {"", "reordered ", "corrupted ", "reordered and corrupted"};

// flag indicating that the twister should shutdown
int twister_should_shutdown = 0;

// Add a packet to the end of the specified linked list. This function creates a node, and copies 
// the data to the node.
void add_packet_to_list(struct linked_list *list, uint16_t length, void *data) {
	struct list_node *node = (struct list_node *)malloc(sizeof(struct list_node) + length);
	node->length = length;
	node->next = NULL;
	memcpy(&node->data, data, length);
	if (!list->tail) {
		list->head = list->tail = node;
	} else {
		list->tail = list->tail->next = node;
	}
}

// Remove a node from the linked list. If second is non-zero, then this function returns the 
// second node instead of the first node. If the list only has one node, then it returns the 
// only node. This function does *not* free tha data associated with the node.
struct list_node *pop_packet_from_list(struct linked_list *list, int second) {
	struct list_node *ret = NULL;
	if (list->head) {
		if (second && list->head->next) {
			ret = list->head->next;
			list->head->next = list->head->next->next;
			if (!list->head->next)
				list->tail = list->head;
		} else {
			ret = list->head;
			list->head = list->head->next;
			if (!list->head)
				list->tail = NULL;
		}
	}
	return ret;
}

// Clear the specified linked list, and free all memory associated with it.
void clear_list(struct linked_list *list) {
	while (list->head) {
		struct list_node *node = list->head;
		list->head = list->head->next;
		free(node);
	}
	list->tail = NULL;
}

// intercept ctrl-c
void sig_int(int signal) {
	twister_should_shutdown=1;
}

int main(int argc, char * argv[]) {
	struct sockaddr_in sa_me, sa_src, sa_dest, sa_recv;
	int my_port, src_port, dest_port, slen=sizeof(sa_me);
	int sock, millisecs;
	void *buffer;
	fd_set socks;
	struct timeval timeout, cur_time;
	int select_result = 0;
	struct hostent *src, *dest;
	float p_corrupt, p_drop, p_reorder;
	
	// initialize the queues
	struct linked_list src_queue = {NULL, NULL};
	struct linked_list dest_queue = {NULL, NULL};
	
	// get the command line parameters
	if (argc!=10) {
		printf("usage: %s <twister_rcv_port> <sender_addr> <sender_port> <receiver_addr> <receiver_port> "
		       "<prob_corrupt> <prob_drop> <prob_reorder> <interval>\n\n"
		       "-- All probabilities should be specified"
		       "as a percentage in the range from 0.00 to 1.00, and the interval should be specified in ms.\n"
		       "-- Note that probability values greater than 0.2 are not recommended as it causes cascading effects"
		       " and exponentially increasing retry attempts. For best observable results, a probability of 0.05 "
		       "is recommended.\n", argv[0]);
		exit(-1);
	}
	my_port = atoi(argv[1]);
	src = gethostbyname(argv[2]);
	if (!src)
		syserr("no such host - sender");
	src_port = atoi(argv[3]);
	dest = gethostbyname(argv[4]);
	if (!dest)
		syserr("no such host - receiver");
	dest_port = atoi(argv[5]);
	p_corrupt = atof(argv[6]);
	p_drop = atof(argv[7]);
	p_reorder = atof(argv[8]);
	millisecs = atoi(argv[9]);
	
	// set up the twister's addresses
	memset(&sa_me, 0, slen);
	sa_me.sin_family = AF_INET;
	sa_me.sin_port = htons(my_port);
	
	// set up the sender address
	memset(&sa_src, 0, slen);
	sa_src.sin_family = AF_INET;
	sa_src.sin_port = htons(src_port);
	sa_src.sin_addr = *((struct in_addr*)src->h_addr);
	
	// set up the receiver address
	memset(&sa_dest, 0, slen);
	sa_dest.sin_family = AF_INET;
	sa_dest.sin_port = htons(dest_port);
	sa_dest.sin_addr = *((struct in_addr*)dest->h_addr);
	
	// create the socket
	if ((sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
		syserr("could not create socket");
	}
	// bind the socket to the specified address and port
	if((bind(sock, (struct sockaddr*)&sa_me, slen)==-1)) {
		syserr("could not bind socket");
	}
	
	// set up the signal handler for ctrl-c
	signal(SIGINT, sig_int);

	// allocate the input buffer
	buffer = malloc(IN_BUFF_SIZE);
	
	while(!twister_should_shutdown) {
		ssize_t bytes_read;
		FD_ZERO(&socks);
		FD_SET(sock, &socks);
		
		// reset the timeout timer
		if(!select_result) {
			timeout.tv_sec = millisecs / 1000;
			timeout.tv_usec = millisecs % 1000 * 1000;
		}
		// check for received input
		select_result = select(sock+1, &socks, NULL, NULL, &timeout);
		
		// get the current time
		gettimeofday(&cur_time, NULL);

		if (select_result == -1) {
			break;
		} else if (select_result==0) {
			struct list_node *node;
			int pack_no = 0;		// inidicates which packet to send, first or second
			int twistno = 0;		// indicates what has been done to the current packet
			// transmit packet to receiver
			if (can_reorder_packet(dest_queue) && should_reorder_packet(p_reorder))
				twistno = pack_no = 1;
			if ((node = pop_packet_from_list(&dest_queue, pack_no))) {
				if (should_corrupt_packet(p_corrupt)) {
					twistno += 2;
					corrupt_packet(node);	// corrupt a bit in the packet
				}
				if (!should_drop_packet(p_drop)) {
					printf("%ld.%03d: Sending %spacket to %s:%u\n", cur_time.tv_sec, cur_time.tv_usec/1000, twisted[twistno], 
						inet_ntoa(sa_dest.sin_addr), ntohs(sa_dest.sin_port));
					// send the packet
					sendto(sock, &node->data, node->length, 0, (struct sockaddr*)&sa_dest, slen);
				} else {
					printf("%ld.%03d: Dropping packet to %s:%u\n", cur_time.tv_sec, cur_time.tv_usec/1000, 
						inet_ntoa(sa_dest.sin_addr), ntohs(sa_dest.sin_port));
				}
				// free the memory for this packet
				free(node);
			}
			
			// transmit packet to sender
			if (can_reorder_packet(src_queue) && should_reorder_packet(p_reorder))
				twistno = pack_no = 1;
			else
				twistno = pack_no = 0;
			if ((node = pop_packet_from_list(&src_queue, pack_no))) {
				if (should_corrupt_packet(p_corrupt)) {
					twistno += 2;
					corrupt_packet(node);	// corrupt a bit in the packet
				}
				if (!should_drop_packet(p_drop)) {
					printf("%ld.%03d: Sending %spacket to %s:%u\n", cur_time.tv_sec, cur_time.tv_usec/1000, twisted[twistno], inet_ntoa(sa_src.sin_addr), ntohs(sa_src.sin_port));
					// send the packet
					sendto(sock, &node->data, node->length, 0, (struct sockaddr*)&sa_src, slen);
				} else {
					printf("%ld.%03d: Dropping packet to %s:%u\n", cur_time.tv_sec, cur_time.tv_usec/1000, 
						inet_ntoa(sa_src.sin_addr), ntohs(sa_src.sin_port));
				}
				// free the memory for this packet
				free(node);
			}
		} else {
			if (FD_ISSET(sock, &socks)) {
				// receive packets
				bytes_read = recvfrom(sock, buffer, IN_BUFF_SIZE, 0, (struct sockaddr *)&sa_recv, &slen);
				if (bytes_read) {
					printf("%ld.%03d: Received packet from %s:%u\n", cur_time.tv_sec, cur_time.tv_usec/1000, 
						inet_ntoa(sa_recv.sin_addr), ntohs(sa_recv.sin_port));
					// check which direction the packet is going and save it in the appropriate queue
					if (sa_recv.sin_addr.s_addr==sa_dest.sin_addr.s_addr && sa_recv.sin_port==sa_dest.sin_port) {
						add_packet_to_list(&src_queue, bytes_read, buffer);
					} else {
						add_packet_to_list(&dest_queue, bytes_read, buffer);
					}
				} else {
					perror("error receiving data");
					twister_should_shutdown = 1;
				}
			}
		}
	}
	
	// exit cleanly, releasing all memory
	printf("%s exiting...\n", argv[0]);
	clear_list(&src_queue);
	clear_list(&dest_queue);
	free(buffer);
	
}

