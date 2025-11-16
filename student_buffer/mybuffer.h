#ifndef MYBUFFER_H
#define MYBUFFER_H

#include <stddef.h>

/* Replacement policies */
#define MB_POLICY_LRU 0
#define MB_POLICY_MRU 1

/* Initialize buffer manager: capacity = number of frames, policy = MB_POLICY_LRU or MB_POLICY_MRU */
int MB_Init(int capacity, int policy);

/* Shutdown: flush dirty frames and free memory */
void MB_Shutdown();

/* Get a page: on success, *pagebuf points to an in-memory buffer (owned by buffer manager) which
   the caller may read/write. Caller must call MB_ReleasePage(...) when done.
   Returns 0 on success, non-zero on error. */
int MB_GetPage(int fd, int pagenum, char **pagebuf);

/* Release page: markDirty = 1 if caller modified page (so write-back will be scheduled on eviction)
   Returns 0 on success. */
int MB_ReleasePage(int fd, int pagenum, int markDirty);

/* Wrapper functions for external testing workload */
void MyBufferInit(int capacity, int policy);
int mb_read(int fd, int pagenum, char *out);
int mb_write(int fd, int pagenum, const char *data);


/* Force flush of all dirty frames to underlying PF layer */
int MB_FlushAll();

/* Print statistics (to stdout) */
void MB_PrintStats();

#endif
