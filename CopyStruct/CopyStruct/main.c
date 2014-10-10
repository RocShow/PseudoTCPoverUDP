//
//  main.cpp
//  CopyStruct
//
//  Created by Show on 10/8/14.
//  Copyright (c) 2014 Show. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


struct head{
    //Head Length: 16Byte
    int seqNum;
    int ackNum;
    short rwnd;
    unsigned ack:1;
    unsigned syn:1;
    unsigned fin:1;
};

struct head makeHead(int _seqNum, int _ackNum, short _rwnd, unsigned _ack, unsigned _syn, unsigned _fin){
    struct head h;
    
    h.seqNum = _seqNum;
    h.ackNum = _ackNum;
    h.rwnd =  _rwnd;
    h.ack =  _ack;
    h.syn = _syn;
    h.fin = _fin;
    
    return h;
}



int main(int argc, const char * argv[]) {
    // insert code here...
    //std::cout << "Hello, World!\n";
    
    char *content = "This is content.";
    
    char *buf = (char*)malloc(16 + strlen(content));
    char *body = buf + 16;
    
    struct head h = makeHead(1, 2, 3, 0, 11, 1);
    
    memcpy(buf, &h, sizeof(h));
    
    memcpy(body, content, strlen(content));
    
    
    //parse
    char *buf2 = (char*)malloc(16 + strlen(content));
    char *body2 = buf2 + 16;
    memcpy(buf2, buf, 16 + strlen(content));
    struct head *h2 = (struct head*)buf2;
    
    printf("%lu\n",sizeof(struct head));
    printf("%s\n",body2);
    
    return 0;
}
