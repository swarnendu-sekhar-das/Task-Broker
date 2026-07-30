# Networking & Sockets: Deep Dive & Interview Grill Guide

This document explores the TCP/IP networking foundation of TaskBroker. It covers Sockets, the TCP Handshake, `TIME_WAIT`, and socket configuration, strictly from a Senior Systems Engineer perspective.

## 1. Everything is a File (File Descriptors)

In Linux, a core philosophy is **"Everything is a File"**. 
When TaskBroker wants to talk to a client over the network, it doesn't use magical network APIs to send data. It simply asks the OS for a File Descriptor (an integer) and uses standard `read()` and `write()` commands, exactly as if it were writing to a text file on the hard drive.

The Linux Kernel intercepts those `read()` and `write()` calls to that specific integer and translates them into TCP packets traversing the Network Interface Card (NIC).

---

## 2. The Server Socket Lifecycle

In `broker.c`, we establish the server using four distinct system calls: `socket()`, `bind()`, `listen()`, and `accept()`.

### Step A: `socket()`
```c
int server_fd = socket(AF_INET, SOCK_STREAM, 0);
```
*   `AF_INET`: Use IPv4 addressing.
*   `SOCK_STREAM`: Use TCP (Transmission Control Protocol) which guarantees order and delivery. If we wanted UDP (fire-and-forget), we would use `SOCK_DGRAM`.
*   This simply creates the integer file descriptor. It is not connected to the internet yet.

### Step B: `bind()`
```c
address.sin_port = htons(PORT);
bind(server_fd, (struct sockaddr *)&address, sizeof(address));
```
*   This assigns a specific Port (`8080`) to our socket. 
*   **The Grill:** Why `htons(PORT)`? CPUs store numbers in different byte orders (Little-Endian vs Big-Endian). The internet standardizes on Big-Endian. `htons` (Host To Network Short) flips the bytes of our integer so every computer on earth understands it.

### Step C: `listen()`
```c
listen(server_fd, 1024);
```
*   This transitions the socket from an "active" socket (used to connect out) to a "passive" socket (used to wait for incoming connections).

> [!CAUTION]
> ### The Interview Grill: The Backlog Parameter
> 
> **Interviewer:** *"What does the `1024` parameter in your `listen` call actually do?"*
> 
> **Defense:** That is the **Backlog** parameter. When a client tries to connect, they send a `SYN` packet. The kernel responds with `SYN-ACK`. Until our C code actually calls `accept()`, the connection sits in a Kernel queue. 
> The backlog parameter (`1024`) dictates the maximum size of that queue! If 1,025 clients connect at the exact same millisecond before my thread can call `accept()`, the OS Kernel will actively drop the 1,025th client's TCP packet. For high-traffic servers (like Nginx), tuning this backlog in the kernel (`somaxconn`) is critical.

### Step D: `accept()`
```c
int* client_fd = malloc(sizeof(int)); 
*client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);
```
*   This pulls the next fully established connection off the Kernel's backlog queue.
*   It returns a **brand new File Descriptor** representing that specific client. The original `server_fd` just keeps listening for new people.

---

## 3. TCP Quirks & Advanced Configuration

Networking introduces severe edge cases that systems engineers must mitigate.

> [!WARNING]
> ### The Interview Grill: `TIME_WAIT`
> 
> **Interviewer:** *"If your broker crashes and you immediately restart it, `bind()` fails with 'Address already in use'. Why? And how did you fix it in your code?"*
> 
> **Defense:** When a TCP connection is closed, the OS doesn't instantly free the port. It places the port in a **`TIME_WAIT`** state for 60 seconds. This is because old, delayed packets might still be traversing the global internet! If the OS reused the port immediately, those old packets might accidentally arrive and corrupt the *new* server's data. 
> 
> In `broker.c`, I bypassed this by using `setsockopt` with **`SO_REUSEADDR`**:
> ```c
> int opt = 1;
> setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
> ```
> This tells the Linux Kernel: "I know what I'm doing. If the port is in `TIME_WAIT`, let me bind to it anyway so I can restart the broker instantly after a crash."

> [!IMPORTANT]
> ### The Interview Grill: Nagle's Algorithm (`TCP_NODELAY`)
> 
> **Interviewer:** *"Your workers send a tiny 1-byte 'ACK' to the broker. Why might that ACK take 200 milliseconds to arrive over a fast LAN network?"*
> 
> **Defense:** Because of **Nagle's Algorithm**. By default, TCP is optimized for throughput, not latency. If you ask TCP to send a tiny 1-byte payload, it thinks: *"Sending a 40-byte TCP header just to carry 1 byte of data is horribly inefficient. I will wait up to 200ms to see if the application wants to send more data, so I can batch it all into one big packet."*
> 
> For a high-performance, real-time TaskBroker, this 200ms delay is catastrophic. To fix this, you must apply the **`TCP_NODELAY`** socket option to force the kernel to flush packets instantly, regardless of their size.

---

## 4. Blocking vs Non-Blocking I/O

When a Worker connects, our thread calls:
```c
read(client_fd, buffer, sizeof(buffer));
```
By default, sockets are **Blocking**. If the client hasn't sent any data yet, the `read()` system call puts the entire thread to sleep indefinitely until data arrives.

*   **The Vulnerability:** A malicious client could connect, never send any data, and hold our thread hostage forever (a Slowloris DOS attack).
*   **The Fix:** Modern servers use **Non-Blocking I/O**. You set the socket to `O_NONBLOCK`. If there is no data, `read()` instantly returns `-1` with an `EWOULDBLOCK` error instead of sleeping. You combine this with `epoll` so the OS tells you exactly which sockets actually have data ready to read, allowing one thread to safely juggle thousands of clients.

---

## 5. The Ultimate Campus Interview Question: TCP Echo Server

For networking roles (Cloudflare, AWS, Akamai), you will be asked to write a basic C TCP server that listens on a port, accepts a connection, and echoes back whatever the client sends, handling the socket lifecycle perfectly.

### The Solution

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    char buffer[1024] = {0};

    // 1. Create Socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed"); exit(EXIT_FAILURE);
    }

    // 2. Prevent "Address already in use" errors (Crucial for interviews!)
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. Bind
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT); // Host-to-Network Short (Big Endian)

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed"); exit(EXIT_FAILURE);
    }

    // 4. Listen (with a backlog of 3)
    if (listen(server_fd, 3) < 0) {
        perror("listen"); exit(EXIT_FAILURE);
    }

    // 5. Accept and Echo
    printf("Listening on port %d...\n", PORT);
    while (1) {
        int addrlen = sizeof(address);
        client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        
        while (1) {
            memset(buffer, 0, sizeof(buffer));
            int valread = read(client_fd, buffer, 1024);
            if (valread <= 0) break; // Client disconnected
            
            // Echo back to client
            write(client_fd, buffer, valread);
        }
        close(client_fd);
    }
    return 0;
}
```
