#define _GNU_SOURCE
#include "pf.h"
#include "pftypes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "mybuffer.h"

/* Prototypes for PF functions (old C style doesn't auto-declare) */
int PF_GetThisPage(int fd, int pagenum, char **pagebuf);
int PF_UnfixPage(int fd, int pagenum, int dirty);
int PF_AllocPage(int fd, int *pagenum, char **pagebuf);
int PF_OpenFile(char *fname);
int PF_CreateFile(char *fname);


/* Use PF_PAGE_SIZE from pftypes.h */
#ifndef PF_PAGE_SIZE
/* fallback if not defined */
#define PF_PAGE_SIZE 4096
#endif

typedef struct {
    int valid;              /* 0=empty,1=used */
    int fd;
    int pagenum;
    char *data;             /* PF_PAGE_SIZE bytes (malloc'd) */
    int pincount;           /* number of active users */
    int dirty;              /* 1 if modified */
    unsigned long lastAccess;
} MBFrame;

typedef struct {
    MBFrame *frames;
    int capacity;
    int policy;            /* MB_POLICY_LRU or MRU */
    unsigned long timeCounter;

    /* stats */
    unsigned long logicalReads;
    unsigned long logicalWrites;
    unsigned long physicalReads;
    unsigned long physicalWrites;
    unsigned long hits;
    unsigned long misses;
} MBPool;

/* single global pool instance */
static MBPool MB = {0};

/* helper: find frame index for fd,pagenum */
static int find_frame(int fd, int pagenum) {
    for (int i = 0; i < MB.capacity; ++i) {
        if (MB.frames[i].valid && MB.frames[i].fd == fd && MB.frames[i].pagenum == pagenum)
            return i;
    }
    return -1;
}

/* helper: choose victim based on LRU or MRU; returns index or -1 if none (all pinned/empty) */
static int choose_victim() {
    int idx_empty = -1;
    for (int i = 0; i < MB.capacity; ++i) {
        if (!MB.frames[i].valid) return i; /* empty slot, use directly */
        if (MB.frames[i].pincount > 0) continue;
        if (idx_empty == -1) idx_empty = i;
    }
    /* if idx_empty is an index of first non-pinned, use policy to choose best among non-pinned */
    unsigned long bestTS;
    int bestIdx = -1;
    if (MB.policy == MB_POLICY_LRU) bestTS = ULLONG_MAX; else bestTS = 0;
    for (int i = 0; i < MB.capacity; ++i) {
        if (!MB.frames[i].valid) continue;
        if (MB.frames[i].pincount > 0) continue;
        if (MB.policy == MB_POLICY_LRU) {
            if (MB.frames[i].lastAccess < bestTS) { bestTS = MB.frames[i].lastAccess; bestIdx = i; }
        } else {
            if (MB.frames[i].lastAccess > bestTS) { bestTS = MB.frames[i].lastAccess; bestIdx = i; }
        }
    }
    return bestIdx;
}

/* internal: write a frame back to PF (using PF_GetThisPage + copy + PF_UnfixPage) */
static int write_frame_to_pf(MBFrame *f) {
    if (!f->valid) return 0;
    /* Use PF_GetThisPage to get PF internal buffer for that page, copy and mark dirty */
    char *pfbuf = NULL;
    int rc = PF_GetThisPage(f->fd, f->pagenum, &pfbuf);
    if (rc != PFE_OK && rc != PFE_PAGEFIXED) {
        /* if PF_GetThisPage failed, we cannot write; return error */
        return rc;
    }
    /* pfbuf now points to PF internal page buffer; copy our page data to it */
    memcpy(pfbuf, f->data, PF_PAGE_SIZE);
    /* mark dirty and unfix so PF's layer will write it eventually */
    rc = PF_UnfixPage(f->fd, f->pagenum, TRUE);
    if (rc != PFE_OK) return rc;
    MB.physicalWrites++;
    return 0;
}

/* internal: load page from PF into frame->data using PF_GetThisPage (copy), then unfix PF buffer */
static int load_page_from_pf(int fd, int pagenum, MBFrame *frame) {
    char *pfbuf = NULL;
    int rc = PF_GetThisPage(fd, pagenum, &pfbuf);
    if (rc != PFE_OK && rc != PFE_PAGEFIXED) {
        return rc;
    }
    /* copy PF page content into our frame buffer */
    memcpy(frame->data, pfbuf, PF_PAGE_SIZE);
    /* unfix PF page (we are keeping our own copy) */
    rc = PF_UnfixPage(fd, pagenum, FALSE);
    if (rc != PFE_OK) return rc;
    MB.physicalReads++;
    return 0;
}

/* ============================================================
   WRAPPER FUNCTIONS for assignment workload compatibility
   ============================================================ */

void MyBufferInit(int capacity, int policy) {
    MB_Init(capacity, policy);
}

int mb_read(int fd, int pagenum, char *out) {
    char *buf = NULL;
    int rc = MB_GetPage(fd, pagenum, &buf);
    if (rc != 0) return rc;
    memcpy(out, buf, PF_PAGE_SIZE);
    MB_ReleasePage(fd, pagenum, 0);
    return 0;
}

int mb_write(int fd, int pagenum, const char *data) {
    char *buf = NULL;
    int rc = MB_GetPage(fd, pagenum, &buf);
    if (rc != 0) return rc;
    memcpy(buf, data, strlen(data)+1);  // write string
    MB_ReleasePage(fd, pagenum, 1);     // mark dirty
    return 0;
}

void MB_print_stats() {
    MB_PrintStats();
}


/* public API implementations */

int MB_Init(int capacity, int policy) {
    if (capacity <= 0) capacity = 8;
    if (capacity > PF_MAX_BUFS) {
        /* PF_MAX_BUFS is 20 in pftypes.h; allow a reasonable cap */
        /* allow higher but warn */
    }
    MB.frames = (MBFrame*) malloc(sizeof(MBFrame) * capacity);
    if (!MB.frames) return -1;
    MB.capacity = capacity;
    MB.policy = (policy == MB_POLICY_MRU) ? MB_POLICY_MRU : MB_POLICY_LRU;
    MB.timeCounter = 0;
    MB.logicalReads = MB.logicalWrites = MB.physicalReads = MB.physicalWrites = MB.hits = MB.misses = 0;
    for (int i = 0; i < capacity; ++i) {
        MB.frames[i].valid = 0;
        MB.frames[i].fd = -1;
        MB.frames[i].pagenum = -1;
        MB.frames[i].data = (char*) malloc(PF_PAGE_SIZE);
        MB.frames[i].pincount = 0;
        MB.frames[i].dirty = 0;
        MB.frames[i].lastAccess = 0;
        if (!MB.frames[i].data) {
            /* free previously allocated */
            for (int j = 0; j < i; ++j) free(MB.frames[j].data);
            free(MB.frames);
            return -1;
        }
    }
    return 0;
}

void MB_Shutdown() {
    MB_FlushAll();
    if (MB.frames) {
        for (int i = 0; i < MB.capacity; ++i) {
            free(MB.frames[i].data);
        }
        free(MB.frames);
    }
    memset(&MB, 0, sizeof(MBPool));
}

int MB_GetPage(int fd, int pagenum, char **pagebuf) {
    if (MB.frames == NULL) return -1;
    MB.timeCounter++;
    int idx = find_frame(fd, pagenum);
    if (idx >= 0) {
        /* hit */
        MB.hits++;
        MB.logicalReads++;
        MB.frames[idx].pincount++;
        MB.frames[idx].lastAccess = MB.timeCounter;
        *pagebuf = MB.frames[idx].data;
        return 0;
    }

    /* miss: need to load page into a victim frame */
    MB.misses++;
    MB.logicalReads++;

    int victim = choose_victim();
    if (victim == -1) {
        /* no frame available (all pinned) */
        return -2; /* indicate no buffer space due to pinned pages */
    }

    MBFrame *vf = &MB.frames[victim];

    /* if victim valid and dirty, write it back via PF */
    if (vf->valid && vf->dirty) {
        int rc = write_frame_to_pf(vf);
        if (rc != 0 && rc != PFE_OK) {
            /* writing failed */
            return -3;
        }
    }

    /* load the requested page from PF into victim frame */
    int rc = load_page_from_pf(fd, pagenum, vf);
    if (rc != 0 && rc != PFE_OK) {
        /* loading failed */
        return -4;
    }

    /* set metadata */
    vf->valid = 1;
    vf->fd = fd;
    vf->pagenum = pagenum;
    vf->pincount = 1;
    vf->dirty = 0;
    vf->lastAccess = MB.timeCounter;
    *pagebuf = vf->data;
    return 0;
}

int MB_ReleasePage(int fd, int pagenum, int markDirty) {
    int idx = find_frame(fd, pagenum);
    if (idx < 0) return -1;
    MBFrame *f = &MB.frames[idx];
    if (f->pincount <= 0) return -2;
    if (markDirty) {
        f->dirty = 1;
        MB.logicalWrites++;
    }
    f->pincount--;
    f->lastAccess = ++MB.timeCounter;
    return 0;
}

int MB_FlushAll() {
    /* write all dirty frames back to underlying PF */
    for (int i = 0; i < MB.capacity; ++i) {
        if (MB.frames[i].valid && MB.frames[i].dirty) {
            int rc = write_frame_to_pf(&MB.frames[i]);
            if (rc != 0 && rc != PFE_OK) return -1;
            MB.frames[i].dirty = 0;
        }
    }
    return 0;
}

void MB_PrintStats() {
    printf("=== MyBuffer Stats ===\n");
    printf("Capacity: %d, Policy: %s\n", MB.capacity, (MB.policy==MB_POLICY_LRU)?"LRU":"MRU");
    printf("Hits: %lu, Misses: %lu\n", MB.hits, MB.misses);
    printf("Logical Reads: %lu, Logical Writes: %lu\n", MB.logicalReads, MB.logicalWrites);
    printf("Physical Reads: %lu, Physical Writes: %lu\n", MB.physicalReads, MB.physicalWrites);
}
