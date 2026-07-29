#ifndef HEAP_H
#define HEAP_H

#include "job.h"

typedef struct {
    Job** array;
    int size;
    int capacity;
} MinHeap;

MinHeap* heap_create(int initial_capacity);
void heap_free(MinHeap* heap);
void heap_push(MinHeap* heap, Job* job);
Job* heap_pop(MinHeap* heap);

#endif // HEAP_H
