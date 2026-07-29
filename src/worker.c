#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

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

    char buffer[1024] = {0};
    
    int num_jobs = 10;
    if (argc > 1) {
        num_jobs = atoi(argv[1]);
    }
    
    for (int i = 0; i < num_jobs; i++) {
        const char *pop_msg = "POP\n";
        send(sock, pop_msg, strlen(pop_msg), 0);
        printf("Sent POP\n");
        
        int bytes_read = read(sock, buffer, 1024);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("Received Job: %s", buffer);
            
            // Simulate work
            usleep(500000); 
            
            // Parse job ID from response (Assume Format: JOB|id|priority|payload\n)
            // For now, if it's "EMPTY\n", just continue
            if (strncmp(buffer, "EMPTY", 5) == 0) {
                continue;
            }
            
            char* saveptr;
            char* job_token = strtok_r(buffer, "|", &saveptr);
            if (job_token && strcmp(job_token, "JOB") == 0) {
                char* id_token = strtok_r(NULL, "|", &saveptr);
                if (id_token) {
                    char ack_msg[256];
                    snprintf(ack_msg, sizeof(ack_msg), "ACK|%s\n", id_token);
                    send(sock, ack_msg, strlen(ack_msg), 0);
                    printf("Sent %s", ack_msg);
                }
            }
        } else {
            printf("Server disconnected\n");
            break;
        }
    }
    
    close(sock);
    return 0;
}
