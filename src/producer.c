#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT 8080

int main(int argc, char const *argv[]) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }
    
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    srand(time(NULL));
    
    int num_jobs = 10;
    if (argc > 1) {
        num_jobs = atoi(argv[1]);
    }
    
    for (int i = 0; i < num_jobs; i++) {
        int priority = rand() % 100;
        char message[256];
        snprintf(message, sizeof(message), "PUSH|%d|cmd_%d\n", priority, i);
        send(sock, message, strlen(message), 0);
        printf("Sent: %s", message);
        usleep(100000); // 100ms
    }
    
    close(sock);
    return 0;
}
