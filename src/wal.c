#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "wal.h"

static int wal_fd = -1;
static const char* wal_filepath = NULL;

int wal_init(const char* filepath) {
    wal_filepath = strdup(filepath);
    wal_fd = open(filepath, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (wal_fd < 0) {
        perror("Failed to open WAL");
        return -1;
    }
    return 0;
}

void wal_append_push(int id, int priority, const char* cmd) {
    if (wal_fd < 0) return;
    char buffer[512];
    int len = snprintf(buffer, sizeof(buffer), "PUSH|%d|%d|%s\n", id, priority, cmd);
    if (write(wal_fd, buffer, len) > 0) {
        fsync(wal_fd); // Ensure durability
    }
}

void wal_append_ack(int id) {
    if (wal_fd < 0) return;
    char buffer[256];
    int len = snprintf(buffer, sizeof(buffer), "ACK|%d\n", id);
    if (write(wal_fd, buffer, len) > 0) {
        fsync(wal_fd);
    }
}

void wal_recover(MinHeap* heap) {
    // Stub for Commit 13
    (void)heap;
}
