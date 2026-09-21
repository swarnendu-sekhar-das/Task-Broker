#include <stdlib.h>
#include "heap.h"

MinHeap* heap_create(int initial_capacity) {
    MinHeap* heap = (MinHeap*)malloc(sizeof(MinHeap));
    if (!heap) return NULL;
    
    heap->capacity = initial_capacity > 0 ? initial_capacity : 16;
    heap->size = 0;
    heap->array = (Job**)malloc(heap->capacity * sizeof(Job*));
    if (!heap->array) {
        free(heap);
        return NULL;
    }
    
    return heap;
}

void heap_free(MinHeap* heap) {
    if (heap) {
        if (heap->array) {
            for (int i = 0; i < heap->size; i++) {
                if (heap->array[i]) {
                    free(heap->array[i]);
                }
            }
            free(heap->array);
        }
        free(heap);
    }
}

static void swap(Job** a, Job** b) {
    Job* temp = *a;
    *a = *b;
    *b = temp;
}

static void bubble_up(MinHeap* heap, int index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (heap->array[index]->priority < heap->array[parent]->priority) {
            swap(&heap->array[index], &heap->array[parent]);
            index = parent;
        } else {
            break;
        }
    }
}

static void bubble_down(MinHeap* heap, int index) {
    while (1) {
        int left = 2 * index + 1;
        int right = 2 * index + 2;
        int smallest = index;

        if (left < heap->size && heap->array[left]->priority < heap->array[smallest]->priority) {
            smallest = left;
        }
        if (right < heap->size && heap->array[right]->priority < heap->array[smallest]->priority) {
            smallest = right;
        }

        if (smallest != index) {
            swap(&heap->array[index], &heap->array[smallest]);
            index = smallest;
        } else {
            break;
        }
    }
}

void heap_push(MinHeap* heap, Job* job) {
    if (!heap || !job) return;

    if (heap->size >= heap->capacity) {
        heap->capacity *= 2;
        heap->array = (Job**)realloc(heap->array, heap->capacity * sizeof(Job*));
    }

    heap->array[heap->size] = job;
    bubble_up(heap, heap->size);
    heap->size++;
}

Job* heap_pop(MinHeap* heap) {
    if (!heap || heap->size == 0) return NULL;

    Job* root = heap->array[0];
    heap->array[0] = heap->array[heap->size - 1];
    heap->size--;
    
    if (heap->size > 0) {
        bubble_down(heap, 0);
    }

    return root;
}
