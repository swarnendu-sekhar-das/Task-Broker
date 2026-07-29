#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include "heap.h"

#define PORT 8080

pthread_mutex_t heap_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t heap_cond = PTHREAD_COND_INITIALIZER;
MinHeap* global_heap;

void* client_handler(void* arg) {
    int client_fd = *(int*)arg;
    free(arg); // Free the memory allocated in accept loop

    char buffer[2048];
    int buffer_len = 0;

    while (1) {
        int bytes_read = read(client_fd, buffer + buffer_len, sizeof(buffer) - buffer_len - 1);
        if (bytes_read <= 0) {
            // Dead socket detection or clean disconnect
            break;
        }
        
        buffer_len += bytes_read;
        buffer[buffer_len] = '\0';
        
        // Find newline for TCP stream framing
        char* newline;
        while ((newline = strchr(buffer, '\n')) != NULL) {
            *newline = '\0'; // Null-terminate the string at newline
            
            char* saveptr;
            char* cmd = strtok_r(buffer, "|", &saveptr);
            
            if (cmd != NULL) {
                if (strcmp(cmd, "PUSH") == 0) {
                    // Format: PUSH|priority|payload
                    char* priority_str = strtok_r(NULL, "|", &saveptr);
                    char* payload_str = strtok_r(NULL, "|", &saveptr);
                    if (priority_str && payload_str) {
                        printf("Parsed PUSH: prio=%s, payload=%s\n", priority_str, payload_str);
                        Job* new_job = (Job*)malloc(sizeof(Job));
                        new_job->id = rand() % 10000;
                        new_job->priority = atoi(priority_str);
                        strncpy(new_job->cmd, payload_str, MAX_CMD_LEN - 1);
                        new_job->cmd[MAX_CMD_LEN - 1] = '\0';
                        
                        pthread_mutex_lock(&heap_mutex);
                        heap_push(global_heap, new_job);
                        pthread_cond_signal(&heap_cond); // Wake up one sleeping consumer
                        pthread_mutex_unlock(&heap_mutex);
                    }
                } else if (strcmp(cmd, "POP") == 0) {
                    printf("Parsed POP\n");
                    
                    pthread_mutex_lock(&heap_mutex);
                    while (global_heap->size == 0) {
                        // Deep sleep until a job is pushed
                        pthread_cond_wait(&heap_cond, &heap_mutex);
                    }
                    Job* job = heap_pop(global_heap);
                    pthread_mutex_unlock(&heap_mutex);
                    
                    if (job) {
                        char resp[512];
                        snprintf(resp, sizeof(resp), "JOB|%d|%d|%s\n", job->id, job->priority, job->cmd);
                        write(client_fd, resp, strlen(resp));
                        free(job);
                    } else {
                        const char* mock_resp = "EMPTY\n";
                        write(client_fd, mock_resp, strlen(mock_resp));
                    }
                } else if (strcmp(cmd, "ACK") == 0) {
                    // Format: ACK|id
                    char* id_str = strtok_r(NULL, "|", &saveptr);
                    if (id_str) {
                        printf("Parsed ACK: id=%s\n", id_str);
                        // TODO: process ACK (tombstone)
                    }
                }
            }
            
            // Shift remaining data to start of buffer
            int consumed = (newline - buffer) + 1;
            int remaining = buffer_len - consumed;
            memmove(buffer, newline + 1, remaining);
            buffer_len = remaining;
            buffer[buffer_len] = '\0';
        }
    }

    close(client_fd);
    return NULL;
}

int main() {
    // Ignore SIGPIPE to prevent server crashes on dead sockets
    signal(SIGPIPE, SIG_IGN);
    
    global_heap = heap_create(1024);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // SO_REUSEADDR allows fast restart of the broker without "Address already in use" errors
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 1024) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("TaskBroker initialized. Listening on port %d...\n", PORT);

    while (1) {
        struct sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int* client_fd = malloc(sizeof(int)); // Allocate memory for the fd
        
        *client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);
        if (*client_fd < 0) {
            perror("accept failed");
            free(client_fd);
            continue;
        }

        // Spawn a new detached thread for each client
        pthread_t thread_id;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        
        if (pthread_create(&thread_id, &attr, client_handler, (void*)client_fd) != 0) {
            perror("pthread_create failed");
            close(*client_fd);
            free(client_fd);
        }
        pthread_attr_destroy(&attr);
    }
    
    close(server_fd);
    return 0;
}
