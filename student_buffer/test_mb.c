#include "pf.h"
#include "pftypes.h"
#include "mybuffer.h"
#include <stdio.h>


int main() {
    /* initialize PF library first */
    PF_Init();

    /* Create/open a test file (change path as per your data) */
    PF_CreateFile("testfile.db");
    int fd = PF_OpenFile("testfile.db");
    if (fd < 0) { printf("open failed: %d\n",fd); return 1; }

    /* Initialize my buffer */
    MB_Init(8, MB_POLICY_LRU);

    /* Allocate one page using PF, write some content, then test MB read/write */
    int pnum;
    char *buf;
    PF_AllocPage(fd, &pnum, &buf);
    snprintf(buf, PF_PAGE_SIZE, "Hello page %d", pnum);
    PF_UnfixPage(fd, pnum, TRUE); /* commit into PF layer */

    /* Now get page via MB */
    char *mbp;
    MB_GetPage(fd, pnum, &mbp);
    printf("MB read: %.20s\n", mbp);
    MB_ReleasePage(fd, pnum, 0);

    MB_PrintStats();

    MB_Shutdown();
    PF_CloseFile(fd);
    return 0;
}
