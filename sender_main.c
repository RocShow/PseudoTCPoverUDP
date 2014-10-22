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
#define BODYSIZE 1460
#define BUFSIZE 1460000


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
    char *buf;
    int bufRear;
    char *packet;
    int lastACK;
    int dupACK;
    int seqNumBase;
};

struct swnd swFactory(){
    struct swnd sw;
    sw.base = 0;
    sw.newSeq = 0;
    sw.buf = malloc(sizeof(char) * BUFSIZE);
    sw.packet = malloc(sizeof(char) * BODYSIZE);
    sw.bufRear = 0;
    sw.lastACK = -1;
    sw.dupACK = 0;
    sw.seqNumBase = 0;
    memset(sw.buf, 0, BUFSIZE);
    memset(sw.packet, 0, BODYSIZE);
    return sw;
}
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

void makeDataPacket(int seqNum, void* packet){
    makePacket(seqNum, 0, 0, 0, 0, 0, packet);
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
size_t fillData(struct swnd* s, FILE *fp){
    size_t readBytes = fread(s->buf, 1, BUFSIZE, fp);
    s->bufRear = (int)readBytes - 1;
    //printf("fill data. BUFREAR: %d\n",s->bufRear);
    return readBytes;
}

size_t sendPacket(struct swnd* s, int socket, struct addrinfo servInfo){
    int start = s->base;
    int size = 0;
    size_t totalSent = 0;
//    if (s->seqNumBase == 100) {
//        printf("100\n");
//    }
    while (start <= s->bufRear) {
        if (s->bufRear - start + 1 >= BODYSIZE) {
            size = BODYSIZE;
        } else {
            size = s->bufRear - start + 1;
        }
        memcpy(s->packet + HEADLEN, s->buf + start, size);
        //fwrite(s->packet + HEADLEN, 1, size, fp);
        makeDataPacket(start + s->seqNumBase * BUFSIZE, s->packet);
        totalSent += sendto(socket, s->packet, size + HEADLEN, 0, servInfo.ai_addr, servInfo.ai_addrlen);
        //printf("send packet: %d\n", start + s->seqNumBase * BUFSIZE);
        start += size;
    }
    //printf("1*********************\n");
    return totalSent;
}

size_t sendOnePacket(struct swnd* s, int socket, struct addrinfo servInfo){
    int start = s->base;
    int size = 0;
    size_t totalSent = 0;
    if (s->bufRear - start + 1 >= BODYSIZE) {
        size = BODYSIZE;
    } else {
        size = s->bufRear - start + 1;
    }
    memcpy(s->packet, s->buf + start, size);
    makeDataPacket(start + s->seqNumBase * BUFSIZE, s->packet);
    totalSent += sendto(socket, s->packet, size + HEADLEN, 0, servInfo.ai_addr, servInfo.ai_addrlen);
    //printf("send packet: %d\n", start + s->seqNumBase * BUFSIZE);
    //printf("2*********************\n");
    return totalSent;
}

int rcvACK(struct swnd* s, struct head h, int socket, struct addrinfo servInfo, int *sent, int *timeout){
    //printf("RecvACK: %d, Base: %d\n",h.ackNum, s->base + s->seqNumBase * BUFSIZE);
    //if (s->seqNumBase > 0) {
    //    printf(">0\n");
    //}
    int rcvBytes = h.ackNum - s->base - s->seqNumBase * BUFSIZE;
    if (h.ackNum > s->base + s->seqNumBase * BUFSIZE) {
        s->base = h.ackNum - s->seqNumBase * BUFSIZE;
        s->dupACK = 0;
        *sent = 0;
    }
    if (h.ackNum == s->base + s->seqNumBase * BUFSIZE) {
        s->dupACK++;
        if (s->dupACK >= 4 && *sent == 0) {
            //printf("dup\n");
            sendOnePacket(s, socket, servInfo);
            *sent = 1;
            *timeout = 0;
        }
    }
    return rcvBytes;
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
    size_t readBytes;
    long long int totalSent = 0;
    //int contentLen = 0;
    //char ch;
    struct addrinfo servInfo;
    struct swnd sw = swFactory();
    struct head ackPak;
    struct sockaddr_storage their_addr;
    socklen_t addr_len = sizeof their_addr;
    struct timeval tv;
    int sent = 0;
    size_t rcvBytes;
    int timeout = 0;
    
    //FILE *fp1 = fopen("123.txt", "w");
    
    if ((socket = getSenderSocket(hostname, hostUDPport, &servInfo)) == -1){
        perror("Can't Create Socket\n");
        exit(1);
    }
    
    if ((fp = fopen(filename, "rb")) == NULL) {
        perror("Can't Open File\n");
        exit(1);
    }
    
    //setSocket Timeout
    tv.tv_sec = 0;
    tv.tv_usec = 25000;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
        perror("Error");
    }
    
    // Temporarily
    //sw.unusedWnd = 500;
    //sw.newSeq = 0;// Assume the first seq is always 0. Can be improved
    
    while(totalSent < bytesToTransfer|| sw.base != sw.bufRear){
        size_t r =fillData(&sw, fp);
        //printf("%lu\n",r);
        readBytes +=r;
        
        sendPacket(&sw, socket, servInfo);
        timeout = 0;
        while (1) {
            if (recvfrom(socket, &ackPak, HEADLEN, 0,
                         (struct sockaddr*)&their_addr, &addr_len) != -1) {
                rcvBytes = rcvACK(&sw, ackPak, socket, servInfo, &sent, &timeout);
                totalSent += rcvBytes;
                if (totalSent == bytesToTransfer || (rcvBytes > 0 && sw.base % BUFSIZE == 0)) {
                    if (sw.bufRear != BUFSIZE - 1) {
                        terminate(socket, servInfo);
                        printf("Total Sent:%lld\n",totalSent);
                        //freeSwnd(&sw);
                        fclose(fp);
                        //fclose(fp1);
                        close(socket);
                        return;
                    } else {
                        sw.seqNumBase++;
                        //fillData(&sw, fp);
                        sw.base = 0;
                        //sendPacket(&sw, socket, servInfo);
                        //printf("SeqBase:%d\n",sw.seqNumBase);
                        break;
                    }
                }
                if (timeout == 1 && rcvBytes > 0) {
                    sendOnePacket(&sw, socket, servInfo);
                }
            } else {
                //printf("timeout\n");
                timeout = 1;
                sendOnePacket(&sw, socket, servInfo);
                //sendPacket(&sw, socket, servInfo);
            }

        }
        
    }
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