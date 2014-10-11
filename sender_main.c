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
#include <time.h>

#define PORTLEN 6
#define MSS 1472
#define HEADLEN 12
#define MAXBODY MSS - HEADLEN
#define BUFSIZE 102401 //0.1MB Buffer
//#define BUFSIZE 1600 //0.1MB Buffer
#define TIMEOUT 25

clock_t start;

void startTimer(){
    start = clock();
}

int getTime(){
    clock_t now = clock() - start;
    clock_t msec = now * 1000 / CLOCKS_PER_SEC;
    return (int)msec;
}

int isTimeOut(clock_t end){ //unit of threshold is millisecond
    return getTime() - start > end ? 1 : 0;
}

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
    int wndRear; //wndRear can never exceed bufRear
    int bufRear;
    int unusedWnd; //handle the situation that no enough data to fill the window
    char buf[BUFSIZE];
    int timers[BUFSIZE];
//    int timerMap[BUFSIZE];
//    int curTimer;
//    int timerRear;
    int timerStarted;
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

void makeFinPacket(void* packet){
    makePacket(0, 0, 0, 0, 0, 1, packet);
}

void makeACKFinPacket(void* packet){
    makePacket(0, 0, 0, 1, 0, 1, packet);
}

void makeDataPacket(const struct swnd sw, void* packet){
    makePacket(sw.newSeq, 0, 0, 0, 0, 0, packet);
}

void makeResendDataPacket(int seqNum, void* packet){
    makePacket(seqNum, 0, 0, 0, 0, 0, packet);
}

struct head* getHead(void *packet){
    return (struct head*)packet;
}

char* getBody(void *packet){
    return (char*)(packet + HEADLEN);
}

//sliding window operations
int fillData(struct swnd* s, char data){
    if (((s->bufRear + 1 + BUFSIZE) % BUFSIZE) != s->base) { //not full
        //file data
        //printf("Fill Data: %c\n", data);
        //efficiency can be improved by fill chunk of data together
        s->buf[s->bufRear] = data;
        s->bufRear = (s->bufRear + 1 + BUFSIZE) % BUFSIZE;
        if(s->unusedWnd > 0){
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
        while ((s->wndRear - s->newSeq + BUFSIZE) % BUFSIZE > MAXBODY) { //can send full size body
            //send
            if (s->newSeq + MAXBODY > BUFSIZE - 1) {
                int first = BUFSIZE - s->newSeq;
                memcpy(packet + HEADLEN, s->buf + s->newSeq, first);
                memcpy(packet + HEADLEN + first, s->buf, MAXBODY - first);
            } else {
                memcpy(packet + HEADLEN, s->buf + s->newSeq, MAXBODY);
            }
            makeDataPacket(*s, packet); //copy head to packet
            if (sendto(socket, packet, MSS, 0,
                        servInfo.ai_addr, servInfo.ai_addrlen) == -1){
                perror("Send Data Failed.\n");
                exit(1);
            }
            //Set Timer
            if(s->timerStarted == 0){
                startTimer();
                s->timerStarted = 1;
            }
            s->timers[s->newSeq] = getTime() + TIMEOUT;
            
            s->newSeq = (s->newSeq + MAXBODY + BUFSIZE) % BUFSIZE;
            total += MAXBODY;
        }
        if (s->newSeq != s->wndRear) { //body size is less than MAXBODY
            int bodySize = (s->wndRear - s->newSeq + BUFSIZE) % BUFSIZE;
            //send
            
            //copy data to packet
            if (s->newSeq + bodySize > BUFSIZE - 1) {
                int first = BUFSIZE - s->newSeq;
                memcpy(packet + HEADLEN, s->buf + s->newSeq, first);
                memcpy(packet + HEADLEN + first, s->buf, bodySize - first);
            } else {
                memcpy(packet + HEADLEN, s->buf + s->newSeq, bodySize);
            }
            
            makeDataPacket(*s, packet); //copy head to packet
            if (sendto(socket, packet, HEADLEN + bodySize, 0,
                       servInfo.ai_addr, servInfo.ai_addrlen) == -1){
                perror("Send Data Failed.\n");
                exit(1);
            }
            //Set Timer
            if(s->timerStarted == 0){
                startTimer();
                s->timerStarted = 1;
            }
            s->timers[s->newSeq] = getTime() + TIMEOUT;
            
            s->newSeq = s->wndRear;
            total += bodySize;
        }
    }
    
    return total;
}

int resend(struct swnd* s, int socket, struct addrinfo servInfo){
    printf("resend\n");
    int total = 0;
    if (s->newSeq != s->base){ //there is data to resend
        int tempBase = s->base;
        char packet[MSS];
        memset(packet, 0, MSS);
        while ((s->newSeq - tempBase + BUFSIZE) % BUFSIZE > MAXBODY) { //can send full size body
            //send
            //copy data to packet
            if (tempBase + MAXBODY > BUFSIZE - 1) {
                int first = BUFSIZE - tempBase;
                memcpy(packet + HEADLEN, s->buf + tempBase, first);
                memcpy(packet + HEADLEN + first, s->buf, MAXBODY - first);
            } else {
                memcpy(packet + HEADLEN, s->buf + tempBase, MAXBODY);
            }
            makeResendDataPacket(tempBase, packet); //copy head to packet
            if (sendto(socket, packet, MSS, 0,
                       servInfo.ai_addr, servInfo.ai_addrlen) == -1){
                perror("Resend Data Failed.\n");
                exit(1);
            }
            //Set Timer
            if(s->timerStarted == 0){
                startTimer();
                s->timerStarted = 1;
            }
            s->timers[tempBase] = getTime() + TIMEOUT;
            
            tempBase = (tempBase + MAXBODY + BUFSIZE) % BUFSIZE;
            total += MAXBODY;
        }
        if (tempBase != s->newSeq) { //body size is less than MAXBODY
            int bodySize = (s->newSeq - tempBase + BUFSIZE) % BUFSIZE;
            //send
            
            //copy data to packet
            if (tempBase + bodySize > BUFSIZE - 1) {
                int first = BUFSIZE - tempBase;
                memcpy(packet + HEADLEN, s->buf + tempBase, first);
                memcpy(packet + HEADLEN + first, s->buf, bodySize - first);
            } else {
                memcpy(packet + HEADLEN, s->buf + tempBase, bodySize);
            }
            
            makeResendDataPacket(tempBase, packet); //copy head to packet
            if (sendto(socket, packet, HEADLEN + bodySize, 0,
                       servInfo.ai_addr, servInfo.ai_addrlen) == -1){
                perror("Resend Data Failed.\n");
                exit(1);
            }
            //Set Timer
            if(s->timerStarted == 0){
                startTimer();
                s->timerStarted = 1;
            }
            s->timers[tempBase] = getTime() + TIMEOUT;
            
            //tempBase = s->newSeq;
            total += bodySize;
        }
    }
    return total;
}

int rcvACK(struct swnd* s, struct head h){
    int ackNum = h.ackNum;
    if((ackNum - s->base + BUFSIZE) % BUFSIZE <= (s->newSeq - s->base + BUFSIZE) % BUFSIZE){ //Useful ACK
        int steps = (ackNum - s->base + BUFSIZE) % BUFSIZE;
        s->base = ackNum;
        //Move the window forward
        if ((s->wndRear + steps + BUFSIZE) % BUFSIZE > s->bufRear){ //wndRear can never exceed bufRear
            s->unusedWnd = s->unusedWnd + steps - (s->bufRear - s->wndRear + BUFSIZE) % BUFSIZE;
            s->wndRear = s->bufRear;
        } else {
            s->wndRear = (s->wndRear + steps + BUFSIZE) % BUFSIZE;
        }
        return 1;
    }
    
    //if all ack are received
    if (s->base == s->newSeq) {
        //stop timer
        s->timerStarted = 0;
    }

    return 0;
    //should fill data after revACK
}

void extendWndSize(struct swnd* s, int size){
    if ((s->bufRear - s->wndRear + BUFSIZE) % BUFSIZE > size) {
        s->wndRear = (s->wndRear + size + BUFSIZE) % BUFSIZE;
    } else {
        s->unusedWnd = s->unusedWnd + size - (s->bufRear - s->wndRear + BUFSIZE) % BUFSIZE;
        s->wndRear = s->bufRear;
    }
}

void shrinkWndSize(struct swnd* s){
    int curSize = (s->wndRear - s->base + BUFSIZE) % BUFSIZE;
    s->wndRear = (s->wndRear - curSize / 2 + BUFSIZE) % BUFSIZE;
}

void terminate(int socket,struct addrinfo servInfo){
    char packet[HEADLEN];
    struct head *h;
    struct sockaddr_storage their_addr;
    socklen_t addr_len = sizeof their_addr;
    ssize_t recvNum;
    
    makeFinPacket(packet);
    if (sendto(socket, packet, HEADLEN, 0,
               servInfo.ai_addr, servInfo.ai_addrlen) == -1){
        perror("Send Fin Failed.\n");
        exit(1);
    }
    
    
    
    //Until terminate or timeout
    while (1) {
        recvNum = recvfrom(socket, packet, MSS, 0,
                           (struct sockaddr *)&their_addr, &addr_len);
        if (recvNum == -1){ //timeout
            if (sendto(socket, packet, HEADLEN, 0,
                       servInfo.ai_addr, servInfo.ai_addrlen) == -1){
                perror("Send Fin Failed.\n");
                exit(1);
            }
        } else {
            h = (struct head*)packet;
            if (h->fin == 1 && h->ack == 1) {
                makeACKFinPacket(packet);
                if (sendto(socket, packet, HEADLEN, 0,
                           servInfo.ai_addr, servInfo.ai_addrlen) == -1){
                    perror("Send ACKFin Failed.\n");
                    exit(1);
                }
                break;
            }
        }
    }
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
    struct head ackPak;
    struct sockaddr_storage their_addr;
    socklen_t addr_len = sizeof their_addr;
    struct timeval tv;
    int lastACK = -1;
    int sameACK = 0;
    
    if ((socket = getSenderSocket(hostname, hostUDPport, &servInfo)) == -1){
        perror("Can't Create Socket\n");
        exit(1);
    }
    
    if ((fp = fopen(filename, "r")) == NULL) {
        perror("Can't Open File\n");
        exit(1);
    }
    
    sw.base = 0;
    sw.newSeq = 0;
    sw.bufRear = 0;
    sw.wndRear = 0;
    
    //setSocket Timeout 100ms
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
        perror("Error");
    }
    
    // Temporarily
    sw.unusedWnd = 500;
    sw.newSeq = 0;// Assume the first seq is always 0. Can be improved
    
    ch = fgetc(fp);
    while(ch != EOF || totalSent < contentLen){
        while (ch != EOF && fillData(&sw, ch) == 1) {
            contentLen++;
            ch = fgetc(fp);
        }
        totalSent += sendPacket(&sw, socket, servInfo);
        
        do {
            if (isTimeOut(sw.timers[sw.base]) == 1) {
                resend(&sw, socket, servInfo);
                //printf("timeout\n");
            }
            if (recvfrom(socket, &ackPak, HEADLEN, 0,
                         (struct sockaddr*)&their_addr, &addr_len) == -1) {
            }
            if (lastACK == ackPak.ackNum) {
                sameACK++;
                if (sameACK == 4) {
                    resend(&sw, socket, servInfo);
                    //printf("fast Resend\n");
                }
            } else {
                lastACK = ackPak.ackNum;
                sameACK = 1;
            }
        }while (rcvACK(&sw, ackPak) != 1);
    }
    
    printf("Sent %d Bytes.\n", totalSent);
    
    //Terminate the transimission
    terminate(socket, servInfo);
    
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