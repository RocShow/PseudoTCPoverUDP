#include <stdio.h>

#define BUFSIZE 5
#define FIVE 5

char data = 'a';

struct swnd{
    //Except beginning, wndSize will never equals to base;
    //wndSize + 1 == base means the buffer is full.
    int base;
    int newSeq;
    int wndRear;
    int bufRear;
    char buf[BUFSIZE];
};

void fillData(struct swnd* s){
    while (((s->bufRear + 1) % BUFSIZE) != s->base) { //not full
        //file data
        printf("Fill Data: %c\n", data);
        s->buf[s->bufRear] = data++;
        s->bufRear = (s->bufRear + 1) % BUFSIZE;
    }
}

void sendData(struct swnd* s){
    while (s->newSeq != s->wndRear) { //can send data
        printf("%c",s->buf[s->newSeq]);
        s->newSeq = (s->newSeq + 1) % BUFSIZE;
    }
}

void rcvACK(struct swnd* s){
    if (s->base != s->newSeq) {
        s->base = (s->base + 1) % BUFSIZE;
        s->wndRear = (s->wndRear +1) % BUFSIZE;
    }
    //should fill data after revACK
}

void extendWndSize(struct swnd* s){
    if((s->wndRear + 1) % BUFSIZE != s->bufRear){
        s->wndRear = (s->wndRear + 1) % BUFSIZE;
    }
}

void shrinkWndSize(struct swnd* s){
    int curSize = (s->wndRear - s->base) % BUFSIZE;
    s->wndRear = (s->wndRear - curSize / 2) % BUFSIZE;
}

int main(int argc, const char * argv[]) {
    // insert code here...
    struct swnd s;
    
    fillData(&s);
    s.wndRear = 2;
    sendData(&s);
    rcvACK(&s);
    rcvACK(&s);
    rcvACK(&s);
    fillData(&s);
    extendWndSize(&s);
    sendData(&s);
    sendData(&s);
    rcvACK(&s);
    rcvACK(&s);
    rcvACK(&s);
    rcvACK(&s);
    rcvACK(&s);
    rcvACK(&s);
    fillData(&s);
    shrinkWndSize(&s);
    shrinkWndSize(&s);
    shrinkWndSize(&s);
    shrinkWndSize(&s);
    sendData(&s);
    
    printf("%lu",sizeof(s));
    
    return 0;
}
