#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORTLEN 6
#define MSS 1472
#define HEADLEN 12
#define MAXBODY MSS - HEADLEN
#define BUFSIZE 1048577 //1MB Buffer

struct head{
    int seqNum;
    int ackNum;
    short rwnd;
    unsigned ack:1;
    unsigned syn:1;
    unsigned fin:1;
};

struct swnd{
    //Except beginning, wndSize will never equals to base;
    //wndSize + 1 == base means the buffer is full.
    int base;
    int newSeq;
    int wndRear;
    int bufRear;
    int unusedWnd; //handle the situation that no enough data to fill the window
    char buf[BUFSIZE];
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

//void makeDataPacket(int _seqNum, int _ackNum, short _rwnd, void* packet){
//    makePacket(_seqNum, _ackNum, _rwnd, 0, 0, 0, packet);
//}

void makeDataPacket(const struct swnd sw, void* packet){
    makePacket(sw.newSeq, 0, 0, 0, 0, 0, packet);
}

//sliding window operations
int fillData(struct swnd* s, char data){
    if (((s->bufRear + 1) % BUFSIZE) != s->base) { //not full
        //file data
        //printf("Fill Data: %c\n", data);
        //efficiency can be improved by fill chunk of data together
        s->buf[s->bufRear] = data;
        s->bufRear = (s->bufRear + 1) % BUFSIZE;
        while(s->unusedWnd > 0){
            s->wndRear++;
            s->unusedWnd--;
        }
        return 1;
    } else {
        return 0;
    }
}

int sendPacket(struct swnd* s, int socket, struct addrinfo servInfo){
    int total = 0;
    if (s->newSeq != s->wndRear) { //can send data
        char packet[MSS];
        memset(packet, 0, MSS);
        while ((s->wndRear - s->newSeq) % BUFSIZE > MAXBODY) { //can send full size body
            //send
            memcpy(packet + HEADLEN, s->buf + s->newSeq, MAXBODY); //copy data to packet
            makeDataPacket(*s, packet); //copy head to packet
            if (sendto(socket, packet, MSS, 0,
                        servInfo.ai_addr, servInfo.ai_addrlen) == -1){
                perror("Send Data Failed.\n");
                exit(1);
            }
            s->newSeq = (s->newSeq + MAXBODY) % BUFSIZE;
            total += MAXBODY;
        }
        if (s->newSeq != s->wndRear) { //body size is less than MAXBODY
            int bodySize = (s->wndRear - s->newSeq) % BUFSIZE;
            //send
            memcpy(packet + HEADLEN, s->buf + s->newSeq, bodySize); //copy data to packet
            makeDataPacket(*s, packet); //copy head to packet
            if (sendto(socket, packet, HEADLEN + bodySize, 0,
                       servInfo.ai_addr, servInfo.ai_addrlen) == -1){
                perror("Send Data Failed.\n");
                exit(1);
            }
            s->newSeq = s->wndRear;
            total += bodySize;
        }
    }
    
    return total;
}

void rcvACK(struct swnd* s, struct head h){
    int ackNum = h.ackNum;
    if((ackNum - s->base) % BUFSIZE <= (s->newSeq - s->base) % BUFSIZE){ //Useful ACK
        int steps = (ackNum - s->base) % BUFSIZE;
        s->base = ackNum;
        //Move the window forward
        if ((s->wndRear + steps) % BUFSIZE > s->bufRear){
            s->unusedWnd = s->unusedWnd + steps - (s->bufRear - s->wndRear) % BUFSIZE;
            s->wndRear = s->bufRear;
        } else {
            s->wndRear = (s->wndRear + steps) % BUFSIZE;
        }
    }
    //should fill data after revACK
}

void extendWndSize(struct swnd* s, int size){
    if ((s->bufRear - s->wndRear) % BUFSIZE > size) {
        s->wndRear = (s->wndRear + size) % BUFSIZE;
    } else {
        s->unusedWnd = s->unusedWnd + size - (s->bufRear - s->wndRear) % BUFSIZE;
        s->wndRear = s->bufRear;
    }
}

void shrinkWndSize(struct swnd* s){
    int curSize = (s->wndRear - s->base) % BUFSIZE;
    s->wndRear = (s->wndRear - curSize / 2) % BUFSIZE;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int getSenderSocket(const char* hostname, const int port, struct addrinfo *servInfo){
    int sockfd = -1;
    struct addrinfo hints, *servinfo, *p;
    char remoteIP[INET6_ADDRSTRLEN];
    int rv;
    char portStr[PORTLEN];
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    
    sprintf(portStr, "%d", port);
    
    printf("%s\n",portStr);
    
    if ((rv = getaddrinfo(hostname, portStr, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    
    // loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }
        
        *servInfo = *p;
        
        break;
    }
    
    if (p == NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return -1;
    }
    
    //get remote IP
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
              remoteIP, sizeof remoteIP);
    
    printf("Server IP: %s...\n", remoteIP);
    
    //May cause memory leak
    //freeaddrinfo(servinfo); // all done with this structure
    
    return sockfd;
}

void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer)
{
    int socket;
    FILE *fp;
    //char packet[MSS];
    int totalSent = 0;
    int contentLen = 0;
    //int position = HEADLEN;
    char ch;
    struct addrinfo servInfo;
    struct swnd sw;
    
    if ((socket = getSenderSocket(hostname, hostUDPport, &servInfo)) == -1){
        perror("Can't Create Socket\n");
        exit(1);
    }
    
    if ((fp = fopen(filename, "r")) == NULL) {
        perror("Can't Open File\n");
        exit(1);
    }
    
//    memset(packet, 0, MSS);
//    
//    while((ch = fgetc(fp)) != EOF ){
//        packet[position++] = ch;
//        if (position > MSS - 1) {
//            makeDataPacket(sw,packet);
//            if (sendto(socket, packet, position, 0,
//                       servInfo.ai_addr, servInfo.ai_addrlen) == -1) {
//                perror("Send Response Error.");
//            }
//            memset(packet,0,MSS);
//            totalLen += position;
//            position = HEADLEN;
//        }
//    }
//    
//    makeDataPacket(sw,packet);
//    if (position > HEADLEN && sendto(socket, packet, position, 0,
//               servInfo.ai_addr, servInfo.ai_addrlen) == -1) {
//        perror("Send Response Error.");
//    }
//    
//    totalLen += position;
//    
//    printf("Sent %d", totalLen);
    
    // Temporarily
    sw.wndRear = 15;
    struct head ackH;
    ackH.ackNum = 15;
    
    
    ch = fgetc(fp);
    while(ch != EOF || totalSent < contentLen){
        while (ch != EOF && fillData(&sw, ch) == 1) {
            contentLen++;
            ch = fgetc(fp);
        }
        totalSent += sendPacket(&sw, socket, servInfo);
        rcvACK(&sw, ackH);
    }
    
    printf("Sent %d Bytes.\n", totalSent);
    
    //Clean
    //free(content);
    fclose(fp);
    close(socket);
}

int main(int argc, char** argv)
{
	unsigned short int udpPort;
	unsigned long long int numBytes;
	
	if(argc != 5)
	{
		fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
		exit(1);
	}
	udpPort = (unsigned short int)atoi(argv[2]);
	numBytes = atoll(argv[4]);
	
	reliablyTransfer(argv[1], udpPort, argv[3], numBytes);
}