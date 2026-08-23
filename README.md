# Task Broker

A lightweight, high-performance, and persistent Task/Job Broker implemented in C. It uses a custom TCP-based protocol to enqueue and dequeue jobs with priority, providing durability through a Write-Ahead Log (WAL) and concurrent handling of multiple clients using POSIX threads.

## Tech Stack

*   **Language:** C
*   **Concurrency:** POSIX Threads (`pthread`), Mutexes, and Condition Variables
*   **Networking:** TCP/IP Sockets
*   **Data Structures:** Min-Heap (Priority Queue)
*   **Durability:** Custom Write-Ahead Log (WAL)
*   **Build System:** Make

## Architecture Overview

The system consists of three main components:

1.  **Broker (`broker`):** The central server that listens on port `8080`.
    *   **Priority Queue:** Uses a Min-Heap (`src/heap.c`) to store jobs. Jobs with a lower priority number are executed first.
    *   **Concurrency:** Spawns a new thread for each client connection. Uses condition variables (`pthread_cond_wait`) for efficient deep sleeping when the queue is empty, waking up instantly when a new job is pushed.
    *   **Durability:** Every state change (PUSH or ACK) is recorded in a Write-Ahead Log (`src/wal.c`) before the memory state is modified. On startup, the broker recovers its state from the WAL (`broker.wal`).
    *   **In-flight Tracking:** Tracks jobs sent to clients that have not yet been acknowledged to manage state safely.

2.  **Producer (`producer`):** A client that connects to the broker and enqueues jobs with a specific priority and payload.

3.  **Worker (`worker`):** A client that connects to the broker, fetches jobs, processes them, and sends acknowledgments back to the broker.

## Protocol

The broker communicates over TCP using a simple string-based protocol framed by newline characters (`\n`).

*   **PUSH:**
    *   Request: `PUSH|<priority>|<payload>\n`
    *   Description: Enqueues a new job.
*   **POP:**
    *   Request: `POP\n`
    *   Response: `JOB|<id>|<priority>|<payload>\n`
    *   Description: Dequeues the highest priority job. Blocks if the queue is empty.
*   **ACK:**
    *   Request: `ACK|<id>\n`
    *   Description: Acknowledges the successful completion of a job, allowing the broker to clean up its state and write a tombstone to the WAL.

## Building and Running

### Prerequisites
*   GCC or Clang
*   Make
*   POSIX-compliant OS (Linux, macOS)

### Build
Run `make` to compile the broker, producer, worker, and tests:
```bash
make all
```

### Run
1.  **Start the Broker:**
    ```bash
    ./broker
    ```
2.  **Start a Worker (Consumer) in a new terminal:**
    ```bash
    ./worker
    ```
3.  **Start a Producer in a new terminal:**
    ```bash
    ./producer
    ```

### Clean
```bash
make clean
```
