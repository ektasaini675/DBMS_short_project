# DBMS Short Project – External Buffer Manager

## Overview
This project implements a custom external buffer manager on top of ToyDB PF layer.
It includes:
- LRU / MRU replacement
- Page pinning
- Dirty page tracking
- Logical + physical I/O statistics
- Workload generator

## Folder Structure
DBMS_short_project/
│
├── student_buffer/
│ ├── mybuffer.c
│ ├── mybuffer.h
│ ├── test_mb.c
│ ├── test_mb_workload.c
│
├── toydb (1)/toydb/pflayer/
│ ├── pf.c
│ ├── buf.c
│ ├── hash.c
│ ├── pf.h
│ ├── pftypes.h
│
├── graphs/
│ ├── LogicalWrites.png
│ ├── Physical_IO.png
│ ├── HitVsMisses.png
│
└── report/
├── Task1_Report.pdf



## How to Compile
1. Compile PF layer
```bash
cc -I "toydb (1)/toydb/pflayer" -c "toydb (1)/toydb/pflayer/pf.c"   -o "toydb (1)/toydb/pflayer/pf.o"
cc -I "toydb (1)/toydb/pflayer" -c "toydb (1)/toydb/pflayer/buf.c"  -o "toydb (1)/toydb/pflayer/buf.o"
cc -I "toydb (1)/toydb/pflayer" -c "toydb (1)/toydb/pflayer/hash.c" -o "toydb (1)/toydb/pflayer/hash.o"
```


2. Compile Buffer Manager (Task 1)
```bash
cc -I "toydb (1)/toydb/pflayer" -I student_buffer \
   -c student_buffer/mybuffer.c \
   -o student_buffer/mybuffer.o
```

3. Compile Workload Generators
90% Read
```bash
cc -DREAD_RATIO=0.9 \
   -I "toydb (1)/toydb/pflayer" \
   -I student_buffer \
   -c student_buffer/test_mb_workload.c \
   -o student_buffer/test90.o
```

50% Read
```bash
cc -DREAD_RATIO=0.5 \
   -I "toydb (1)/toydb/pflayer" \
   -I student_buffer \
   -c student_buffer/test_mb_workload.c \
   -o student_buffer/test50.o
```

10% Read
```bash
cc -DREAD_RATIO=0.1 \
   -I "toydb (1)/toydb/pflayer" \
   -I student_buffer \
   -c student_buffer/test_mb_workload.c \
   -o student_buffer/test10.o
```

## Task 1 Output (LRU Policy)
🔹 90% Read Workload

Hits: 159
Misses: 841
Logical Reads: 1000
Logical Writes: 501
Physical Reads: 841
Physical Writes: 456

🔹 50% Read Workload

Hits: 165
Misses: 835
Logical Reads: 1000
Logical Writes: 84
Physical Reads: 835
Physical Writes: 83

🔹 10% Read Workload

Hits: 177
Misses: 823
Logical Reads: 1000
Logical Writes: 904
Physical Reads: 823
Physical Writes: 750

## How to Run
```bash
./student_buffer/run90
./student_buffer/run50
./student_buffer/run10
```

