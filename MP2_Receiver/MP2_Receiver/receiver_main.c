#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define PORTLEN 6
#define MSS 1472
#define HEADLEN 12
#define MAXBODY MSS - HEADLEN
#define SEQRANGE 104857670 // 100MB Buffer
#define RCVBUFFSIZE 20485767 // 20MB Receiver Buffer
//#define SEQRANGE 1600


struct head{
    int seqNum;
    int ackNum;
    short rwnd;
    unsigned ack:1;
    unsigned syn:1;
    unsigned fin:1;
};

void makePacket(int _seqNum, int _ackNum, short _rwnd, unsigned _ack, unsigned _syn, unsigned _fin, void* packet){
    
    struct head h;
    h.seqNum = _seqNum;
    h.ackNum = _ackNum;
    h.rwnd =  _rwnd;
    h.ack =  _ack;
    h.syn = _syn;
    h.fin = _fin;
    
    memcpy(packet, &h, HEADLEN);
    
    //return HEADLEN;
}

void makeACK(char* packet, int ackNum){
    struct head ack;
    ack.ackNum = ackNum;
    ack.ack = 1;
    memcpy(packet, &ack, HEADLEN);
}

void makeACKFinPacket(void* packet){
    makePacket(0, 0, 0, 1, 0, 1, packet);
}

struct head* getHead(void *packet){
    return (struct head*)packet;
}

char* getBody(void *packet){
    return (char*)(packet + HEADLEN);
}

void terminate(int socket, struct sockaddr_storage their_addr, socklen_t addr_len){
    char packet[HEADLEN];
    struct head *h;
    makeACKFinPacket(packet);
    struct timeval tv;
    
    //setSocket Timeout 100ms
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
        perror("Error");
    }
    
    //Util receive ACKFin or timeout
    while (1) {
        if (sendto(socket, packet, HEADLEN, 0,
                   (struct sockaddr *)&their_addr, addr_len) == -1) {
            perror("Send ACKFin Failed.");
            exit(1);
        } else {
            if(recvfrom(socket, packet, HEADLEN, 0,
                        (struct sockaddr *)&their_addr, &addr_len) == -1){
                if (sendto(socket, packet, HEADLEN, 0,
                           (struct sockaddr *)&their_addr, addr_len) == -1) {
                    perror("Send ACKFin Failed.");
                    exit(1);
                }
            } else {
                h = (struct head*)packet;
                if (h->fin == 1 && h->ack == 1) {
                    break;
                }
            }
        }
    }
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa){
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int getListenSocket(const int port){
    int sockfd = -1;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int yes=1;
    char portStr[PORTLEN];
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    
    sprintf(portStr, "%d", port);
    
    if ((rv = getaddrinfo(NULL, portStr, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }
        
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        
        break;
    }
    
    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        return -1;
    }
    
    freeaddrinfo(servinfo); // all done with this structure
    
    return sockfd;
}

void reliablyReceive(unsigned short int myUDPport, char* destinationFile){
    int socket = -1;
    FILE *fp;
    char buf[MSS];
    long num;
    long total;
    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    struct head* h;
    char* body;
    int ackNum = 0; // Assume the first seq is always 0. Can be improved
    char ackPak[HEADLEN];
    int sameACK = 0;
    char *rcvBuffer = malloc(RCVBUFFSIZE);
    int bufStatus = 0;
    int isStarted = 0;
    struct timeval tv;
    
    if((socket = getListenSocket(myUDPport)) == -1){
        perror("Can't Bind Local Port.\n");
        exit(1);
    }
    if ((fp = fopen(destinationFile, "wb")) == NULL) {
        perror("Can't Open File.\n");
        exit(1);
    }

    //setSocket Timeout
    tv.tv_sec = 0;
    tv.tv_usec = 50000;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
        perror("Error");
    }
    
    addr_len = sizeof their_addr;
    while (1) {
        if ((num = recvfrom(socket, buf, MSS, 0,
                            (struct sockaddr *)&their_addr, &addr_len)) > 0) {
            //Can improve by handling several senders
            //buf[num] = '\0';
            h = getHead(buf);
            body = getBody(buf);

            //is FinPacket?
            if (h->fin == 1) {
                fwrite(rcvBuffer, 1, bufStatus, fp);
                terminate(socket, their_addr, addr_len);
                break;
            }
            
//            int n = rand()%100;
//            if (n < 20) { //20% probability to drop a packet
//                //printf("drop\n");
//                continue;
//            }
//            clock_t start = clock();
//            while (clock() - start < 20 * CLOCKS_PER_SEC / 1000) {
//                //20ms delay
//            }
            
            //dataPacket
            //printf("Waiting for: %d, Receive: %d\n",ackNum, h->seqNum);
            if(h->seqNum == ackNum){ //Correct Packet
                sameACK = 0;
                ackNum = (ackNum + num - HEADLEN + SEQRANGE) % SEQRANGE;
                total += num - HEADLEN;
                memcpy(rcvBuffer + bufStatus, body, num - HEADLEN);
                bufStatus += num - HEADLEN;
                
                if (bufStatus > RCVBUFFSIZE - 2 * MSS) {
                    //write to disk
                    //printf("write\n");
                    fwrite(rcvBuffer, 1, bufStatus, fp);
                    bufStatus = 0;
                }
                //printf("%s\n",body);
                //fprintf(fp, "%s", body);
                
                makeACK(ackPak,ackNum);
                if (sendto(socket, ackPak, HEADLEN, 0,
                           (struct sockaddr *)&their_addr, addr_len) == -1){
                    perror("Send ACK Failed.\n");
                    exit(1);
                }
                //printf("Receive %ld\n",total);
            } else {
                sameACK++;
                //printf("duplicate %d\n",sameACK);
                //if (sameACK < 6) { //avoid too many same ACK
                    if (sendto(socket, ackPak, HEADLEN, 0,
                               (struct sockaddr *)&their_addr, addr_len) == -1){
                        perror("Send ACK Failed.\n");
                        exit(1);
                    }
                //}
            }
            
        } else if (num == -1){
            //perror("Receive Error.\n");
            if (isStarted == 0) {
                continue;
            }
            if (sendto(socket, ackPak, HEADLEN, 0,
                       (struct sockaddr *)&their_addr, addr_len) == -1){
                perror("Send ACK Failed.\n");
                exit(1);
            }
        } else if (num == 0) {
            break;
        }
    }
    
    //clean
    //free(buf);
    fclose(fp);
    close(socket);
    printf("total %ld\n",total);
}

int main(int argc, char** argv)
{
	unsigned short int udpPort;
	
	if(argc != 3)
	{
		fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
		exit(1);
	}
	
	udpPort = (unsigned short int)atoi(argv[1]);
	
	reliablyReceive(udpPort, argv[2]);
}
