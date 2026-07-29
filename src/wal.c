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
    if (!wal_filepath) return;
    FILE* file = fopen(wal_filepath, "r");
    if (!file) return;

    printf("Recovering from WAL...\n");

    // Simple fixed-size hash map for active jobs based on ID
    Job* active_jobs[10000] = {NULL};
    int max_id = 0;

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        char* saveptr;
        char* cmd = strtok_r(line, "|", &saveptr);
        
        if (cmd && strcmp(cmd, "PUSH") == 0) {
            char* id_str = strtok_r(NULL, "|", &saveptr);
            char* prio_str = strtok_r(NULL, "|", &saveptr);
            char* payload_str = strtok_r(NULL, "\n", &saveptr);
            
            if (id_str && prio_str && payload_str) {
                int id = atoi(id_str);
                Job* job = (Job*)malloc(sizeof(Job));
                job->id = id;
                job->priority = atoi(prio_str);
                strncpy(job->cmd, payload_str, MAX_CMD_LEN - 1);
                job->cmd[MAX_CMD_LEN - 1] = '\0';
                
                if (id < 10000) {
                    active_jobs[id] = job;
                    if (id > max_id) max_id = id;
                }
            }
        } else if (cmd && strcmp(cmd, "ACK") == 0) {
            char* id_str = strtok_r(NULL, "\n", &saveptr);
            if (id_str) {
                int id = atoi(id_str);
                if (id < 10000 && active_jobs[id] != NULL) {
                    free(active_jobs[id]);
                    active_jobs[id] = NULL;
                }
            }
        }
    }
    
    fclose(file);

    int recovered_count = 0;
    for (int i = 0; i <= max_id; i++) {
        if (active_jobs[i] != NULL) {
            heap_push(heap, active_jobs[i]);
            recovered_count++;
        }
    }
    
    printf("WAL Recovery complete. %d jobs restored to Min-Heap.\n", recovered_count);
}
