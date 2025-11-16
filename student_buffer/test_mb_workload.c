#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../toydb (1)/toydb/pflayer/pf.h"
#include "mybuffer.h"

#define TOTAL_OPS 1000
#define PAGE_COUNT 50

// Returns 1 for READ, 0 for WRITE
int decide_read(double read_ratio) {
    double r = (double)rand() / RAND_MAX;
    return r < read_ratio;
}

int main() {
    srand(time(NULL));

    PF_Init();
    MyBufferInit(8, MB_POLICY_LRU);

    PF_CreateFile("workload.db");
    int fd = PF_OpenFile("workload.db");

    int pnum;
    char *buf;

    // Allocate some initial pages
    for (int i = 0; i < PAGE_COUNT; i++) {
        PF_AllocPage(fd, &pnum, &buf);
        buf[0] = 'A' + (i % 26);
        PF_UnfixPage(fd, pnum, TRUE);
    }

    printf("Running workload...\n");

    for (int i = 0; i < TOTAL_OPS; i++) {
        int page = rand() % PAGE_COUNT;
        int isRead = decide_read(READ_RATIO);

        if (isRead) {
            char outbuf[4096];
            mb_read(fd, page, outbuf);
        } else {
            mb_write(fd, page, "DATA");
        }
    }

    PF_CloseFile(fd);
    MB_PrintStats();

    return 0;
}
