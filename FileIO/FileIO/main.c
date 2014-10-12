//
//  main.cpp
//  FileIO
//
//  Created by Show on 10/11/14.
//  Copyright (c) 2014 Show. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>

int main(int argc, const char * argv[]) {
    // insert code here...
    
    FILE *exein, *exeout;
    
    exein = fopen("test.pdf", "rb");
    if (exein == NULL) {
        /* handle error */
        perror("file open for reading");
        exit(EXIT_FAILURE);
    }
    exeout = fopen("output.pdf", "wb");
    if (exeout == NULL) {
        /* handle error */
        perror("file open for writing");
        exit(EXIT_FAILURE);
    }
    
    size_t n, m;
    unsigned char buff[8192];
    do {
        n = fread(buff, 1, sizeof buff, exein);
        if (n) m = fwrite(buff, 1, n, exeout);
        else   m = 0;
    } while ((n > 0) && (n == m));
    if (m) perror("copy");
    
    if (fclose(exeout)) perror("close output file");
    if (fclose(exein)) perror("close input file");    return 0;
}
