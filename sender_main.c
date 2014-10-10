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
    char content[MSS];
    int totalLen = 0;
    int position = 0;
    char ch;
    struct addrinfo servInfo;
    
    if ((socket = getSenderSocket(hostname, hostUDPport, &servInfo)) == -1){
        perror("Can't Create Socket\n");
        exit(1);
    }
    
    if ((fp = fopen(filename, "r")) == NULL) {
        perror("Can't Open File\n");
        exit(1);
    }
    
    memset(content, 0, MSS);
    
    while((ch = fgetc(fp)) != EOF ){
        content[position++] = ch;
        if (position > MSS - 1) {
            if (sendto(socket, content, position, 0,
                       servInfo.ai_addr, servInfo.ai_addrlen) == -1) {
                perror("Send Response Error.");
            }
            memset(content,0,MSS);
            totalLen += position;
            position = 0;
        }
    }
    
    if (sendto(socket, content, position, 0,
               servInfo.ai_addr, servInfo.ai_addrlen) == -1) {
        perror("Send Response Error.");
    }
    
    totalLen += position;
    
    printf("Sent %d", totalLen);
    
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