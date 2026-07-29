#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>

#define PORT 8080

void* client_handler(void* arg) {
    int client_fd = *(int*)arg;
    free(arg); // Free the memory allocated in accept loop

    // TODO: Protocol Framing & Parsing

    close(client_fd);
    return NULL;
}

int main() {
    // Ignore SIGPIPE to prevent server crashes on dead sockets
    signal(SIGPIPE, SIG_IGN);

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
