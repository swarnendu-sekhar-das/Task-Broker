#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "heap.h"

#define NUM_JOBS 10000

int main() {
    printf("Starting Min-Heap test...\n");
    
    MinHeap* heap = heap_create(16);
    if (!heap) {
        fprintf(stderr, "Failed to create heap\n");
        return 1;
    }
    
    srand(time(NULL));
    
    Job* jobs[NUM_JOBS];
    
    printf("Pushing %d jobs...\n", NUM_JOBS);
    for (int i = 0; i < NUM_JOBS; i++) {
        jobs[i] = (Job*)malloc(sizeof(Job));
        jobs[i]->id = i;
        jobs[i]->priority = rand() % 1000; // random priority 0-999
        heap_push(heap, jobs[i]);
    }
    
    assert(heap->size == NUM_JOBS);
    printf("Heap size is correct.\n");
    
    printf("Popping %d jobs and validating order...\n", NUM_JOBS);
    int last_priority = -1;
    for (int i = 0; i < NUM_JOBS; i++) {
        Job* popped = heap_pop(heap);
        assert(popped != NULL);
        assert(popped->priority >= last_priority);
        last_priority = popped->priority;
        free(popped);
    }
    
    assert(heap->size == 0);
    assert(heap_pop(heap) == NULL);
    printf("Heap order validated successfully!\n");
    
    heap_free(heap);
    printf("Test passed.\n");
    
    return 0;
}
