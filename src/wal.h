#ifndef WAL_H
#define WAL_H

#include "heap.h"

int wal_init(const char* filepath);
void wal_append_push(int id, int priority, const char* cmd);
void wal_append_ack(int id);
int wal_recover(MinHeap* heap);

#endif // WAL_H
