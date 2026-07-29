#ifndef JOB_H
#define JOB_H

#define MAX_CMD_LEN 256

typedef struct {
    int id;
    int priority; // Lower number = higher priority (Min-Heap)
    char cmd[MAX_CMD_LEN];
} Job;

#endif // JOB_H
