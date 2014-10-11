
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

clock_t start;

void startTimer(){
    start = clock();
}

clock_t getTime(){
    clock_t now = clock() - start;
    clock_t msec = now * 1000 / CLOCKS_PER_SEC;
    return msec;
}

int isTimeOut(clock_t end){ //unit of threshold is millisecond
    return getTime() > end ? 1 : 0;
}


int main(){
    puts("123");
    
    clock_t start, now;
    
    start = clock();
    
    int i = 0;
    
//    while(i<1000000){
//        printf("%d\n",i++);
//    }
    
    now = clock() - start;
    clock_t msec = now * 1000 / CLOCKS_PER_SEC;
    //sleep(5);
    
    printf("%lu\n",msec);
    //printf("%d\n",CLOCKS_PER_SEC);
    printf("%d\n",-1%5);
    exit(0);
}