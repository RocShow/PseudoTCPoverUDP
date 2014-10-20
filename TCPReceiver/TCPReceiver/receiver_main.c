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
//#define SEQRANGE 10485767 // 10MB Buffer
//#define SEQRANGE 52428853 // 50MB Buffer
//#define SEQRANGE 1600
#define RCVBUFSIZE 10485767 // 10MB


struct head{
    int seqNum;
    int ackNum;
    short rwnd;
    unsigned ack:1;
    unsigned syn:1;
    unsigned fin:1;
};

struct rcvBuf{
    char *data;
    char *rcvFlags;
    int ack;
    FILE *fp;
};

struct rcvBuf bufFactory(FILE *fp){
    struct rcvBuf buf;
    buf.ack = 0;
    buf.fp = fp;
    buf.data = (char*)malloc(RCVBUFSIZE);
    buf.rcvFlags = (char*)malloc(RCVBUFSIZE);
    memset(buf.data, 0, RCVBUFSIZE);
    memset(buf.rcvFlags, 0, RCVBUFSIZE);
    return buf;
}

void writeFile(struct rcvBuf *buf, int size){
    fwrite(buf->data, 1, size, buf->fp);
}

void setACK(struct rcvBuf *buf){
    int old = buf->ack;
    while (buf->rcvFlags[buf->ack] != 0) {
        buf->rcvFlags[buf->ack] = 0;
        if (buf->ack == RCVBUFSIZE - 1 && buf->ack != old) {
            //printf("write to file\n");
            writeFile(buf, RCVBUFSIZE);
            buf->ack = 0;
        }
        buf->ack = buf->ack + 1;
    }
}

int getACK(struct rcvBuf *buf){
    setACK(buf);
    return buf->ack;
}

void setBufFlags(struct rcvBuf *buf, char flag, int start, int size){
    int freeSpace = RCVBUFSIZE - buf->ack;
    if (freeSpace < size) {
        memset(buf->rcvFlags + start, flag, freeSpace);
        start = 0;
        size = size - freeSpace;
        //printf("%d\n", size);
    }
    memset(buf->rcvFlags + start, (short)flag, size);
    //setACK(buf);
}

//void setFlag0(struct rcvBuf *buf, int start, int size){
//    setBufFlags(buf,0,start,size);
//}

void setFlag1(struct rcvBuf *buf, int start, int size){
    if (buf->rcvFlags[start] != 0) {
        return;
    }
    setBufFlags(buf,1,start,size);
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
    hints.ai_socktype = 1;
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
void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}
void reliablyReceive(unsigned short int myUDPport, char* destinationFile){
    int socket = -1;
    FILE *fp;
    struct sigaction sa;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    const int bufferSize = 102400;
    char *buffer = malloc(bufferSize);
    size_t totalRecv = 0, numRecv = 0;
    
    if((socket = getListenSocket(myUDPport)) == -1){
        perror("Can't Bind Local Port.\n");
        exit(1);
    }
    if ((fp = fopen(destinationFile, "wb")) == NULL) {
        perror("Can't Open File.\n");
        exit(1);
    }
    
    if (listen(socket, 10) == -1) {
        perror("listen");
        exit(1);
    }
    
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    sin_size = sizeof their_addr;
    socket = accept(socket, (struct sockaddr *)&their_addr, &sin_size);
    
    while (1) {
        memset(buffer, 0, bufferSize);
        numRecv = recv(socket, buffer, bufferSize, 0);
        if (numRecv == -1) {
            break;
        }
        totalRecv += numRecv;
        //printf("RECV:%zu\n",totalRecv);
        if (strcmp(buffer, "FINISH") == 0) {
            close(socket);
            //break;
        } else {
            fwrite(buffer, 1, numRecv, fp);
        }
    }
    printf("RECV:%zu\n",totalRecv - 7);
    close(socket);
    free(buffer);
    fclose(fp);
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
