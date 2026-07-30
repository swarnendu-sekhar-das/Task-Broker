# Distributed Systems: Deep Dive & Interview Grill Guide

This document explores the distributed architecture of TaskBroker. It covers network unreliability, delivery guarantees, and the absolute necessity of Idempotency.

## 1. The Distributed Nature of TaskBroker

It's easy to think of TaskBroker as just a C program. But the moment the Broker, the Producer, and the Worker are running on three different machines connected by a network cable, it becomes a **Distributed System**.

The fundamental law of distributed systems is: **The Network is Unreliable.**
*   Cables get unplugged.
*   Routers drop packets due to congestion.
*   Workers crash in the middle of executing a job.

---

## 2. The Two Generals Problem

> [!WARNING]
> ### The Interview Grill: The Unsolvable Problem
> 
> **Interviewer:** *"Can you guarantee mathematically that a message sent over a network was successfully delivered and processed?"*
> 
> **Defense:** No. This is famously proven by the **Two Generals Problem**.
> Imagine two generals on opposite hills trying to coordinate an attack. General A sends a messenger to General B saying "Attack at dawn." General A doesn't know if the messenger was captured. So, General B sends an Acknowledgement (ACK) messenger back. But General B doesn't know if the ACK was captured! So General A sends an ACK for the ACK... and this continues infinitely. 
> Because the network (the messengers) can fail at any time, it is mathematically impossible for both generals to reach 100% certainty. 
> Therefore, in distributed systems, we can never guarantee **"Exactly-Once"** delivery.

---

## 3. Delivery Guarantees in TaskBroker

Since "Exactly-Once" is impossible, message queues must choose between two compromises:

1.  **At-Most-Once (Send and Forget):** The broker sends the job and immediately deletes it. If the worker crashes, the job is permanently lost. (Good for real-time video streaming, terrible for financial transactions).
2.  **At-Least-Once (Send and Verify):** The broker sends the job, but keeps a copy of it. If it doesn't receive an ACK from the worker, it assumes the worker failed and sends the job again to someone else.

TaskBroker strictly implements **At-Least-Once Delivery**.

### The `in_flight` Architecture
When a worker pulls a job, `broker.c` does NOT call `free(job)`. It places the pointer into a tracking array:
```c
in_flight[client_fd] = top_job;
```
If the worker completes the job, it sends an `ACK`. The broker then deletes it:
```c
free(in_flight[client_fd]);
in_flight[client_fd] = NULL;
```

**The Safety Net:** If the worker's TCP connection drops unexpectedly, the `read()` loop exits, and we hit the Dead Socket Detection block:
```c
if (in_flight[client_fd] != NULL) {
    printf("Client disconnected with un-ACKed job. Re-queuing job %d...\n", in_flight[client_fd]->id);
    heap_push(global_heap, in_flight[client_fd]);
    pthread_cond_signal(&heap_cond);
    in_flight[client_fd] = NULL;
}
```
The job is salvaged and pushed right back into the Min-Heap!

---

## 4. Idempotency (The Worker's Burden)

Because TaskBroker guarantees At-Least-Once delivery, we have a massive problem. 
Look at the Two Generals Problem again: What if the Worker successfully executes the job (e.g., charges a customer's credit card $100), but the `ACK` packet gets dropped by the router on its way back to the Broker?

The Broker assumes the worker died, re-queues the job, and sends it to Worker B. Worker B executes it again. The customer is charged $200!

> [!IMPORTANT]
> ### The Interview Grill: Designing for Duplicate Messages
> 
> **Interviewer:** *"Because your broker uses At-Least-Once delivery, a worker might receive the exact same job twice. How do you prevent double-billing?"*
> 
> **Defense:** The Worker must be **Idempotent**. 
> Idempotency means that executing a command once has the exact same effect as executing it 100 times. 
> To achieve this, the Worker must rely on the **Job ID** (the Idempotency Key). 
> Before executing the job, the Worker queries a database: `SELECT status FROM jobs WHERE id = 105`. If the status is `completed`, the Worker knows this is a duplicate message caused by a dropped ACK. It safely ignores the job and simply sends a fresh `ACK` back to the Broker to clear it from the `in_flight` queue!

---

## 5. The Ultimate Campus Interview Question: Idempotency Key Cache

For backend and distributed systems interviews (Stripe, Uber), a classic question is: *"Design an API that charges a user's credit card, but ensure that if the client's network drops and they retry the exact same HTTP request, we don't charge them twice."*

You must implement an **Idempotency Key Cache**.

### The Solution

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define CACHE_SIZE 1024

typedef struct {
    char key[64]; // The Idempotency Key (e.g., a UUID)
    bool processed;
    char response[256]; // The cached HTTP response
} CacheEntry;

CacheEntry idempotency_cache[CACHE_SIZE];

// Simple hash function to map UUID string to array index
unsigned int hash(const char* key) {
    unsigned int hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c; 
    }
    return hash % CACHE_SIZE;
}

bool process_payment(const char* idempotency_key, const char* user_id, int amount, char* out_response) {
    unsigned int idx = hash(idempotency_key);
    
    // 1. Check if we ALREADY processed this exact request
    if (idempotency_cache[idx].processed && 
        strcmp(idempotency_cache[idx].key, idempotency_key) == 0) {
        
        printf("[IDEMPOTENT] Duplicate request detected. Returning cached response.\n");
        strcpy(out_response, idempotency_cache[idx].response);
        return true; 
    }
    
    // 2. We have never seen this key. Process the payment!
    printf("[STRIPE API] Charging user %s $%d...\n", user_id, amount);
    
    // 3. Cache the result BEFORE returning to the client
    strcpy(idempotency_cache[idx].key, idempotency_key);
    idempotency_cache[idx].processed = true;
    sprintf(idempotency_cache[idx].response, "SUCCESS: Charged $%d", amount);
    
    strcpy(out_response, idempotency_cache[idx].response);
    return true;
}
```
