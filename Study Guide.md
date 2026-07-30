# TaskBroker — 10-Day Study & Defense Guide

Campus interview target: **AWS, Cloudflare, Stripe, Datadog, Backend Engineering Teams**
Project: **TaskBroker** — Fault-Tolerant Distributed Job Queue (Min-Heap, WAL, CondVars)
Daily budget: **4–6 focused hours**

---

## Days 1–4: BUILD PHASE

### Day 1 — Data Structures (Min-Heap & Priority Queue) (6 hours)

| Hour | Task | What You Learn |
|:---:|:---|:---|
| 1 | Project setup: `Makefile`, directory structure, headers (`job.h`, `heap.h`) | Build system, C project layout |
| 2 | Implement dynamic array for the heap (`realloc` logic) | Dynamic memory resizing amortized `O(1)` |
| 3 | Implement `heap_push` + `bubble_up()` | Min-Heap insertions (`O(log N)`) |
| 4 | Implement `heap_pop` + `bubble_down()` | Min-Heap extraction and repair (`O(log N)`) |
| 5 | Write `test_heap.c` — Push 10K random priorities, pop all, assert sorted order | Algorithmic testing methodology |
| 6 | Integrate priority strings (e.g., `PRIORITY:1|CMD:foo`) | Struct mapping and text parsing |

### Day 2 — Networking & Protocol Framing (6 hours)

| Hour | Task | What You Learn |
|:---:|:---|:---|
| 1 | Implement TCP server: `socket`, `bind`, `listen`, `accept`, `SO_REUSEADDR` | TCP socket lifecycle |
| 2 | Implement `SIGPIPE` handling and socket disconnection detection | Defending against kernel process termination |
| 3 | Protocol framing: Buffering partial TCP reads until `\n` delimiter | TCP byte-stream boundaries |
| 4 | Command parsing: Differentiate `PUSH`, `POP`, and `ACK` commands | String parsing (`strtok_r`) |
| 5 | Thread-per-client model: Spawn detached `pthread` for each new connection | Multi-client connection handling |
| 6 | Write echo client to test multi-client connectivity | End-to-end network validation |

### Day 3 — Concurrency & Orchestration (5 hours)

| Hour | Task | What You Learn |
|:---:|:---|:---|
| 1 | Global `pthread_mutex_t` protecting the Min-Heap | Basic mutual exclusion |
| 2 | `pthread_cond_t` integration: Worker `while(empty)` sleep loop | Condition Variables and Spurious Wakeups |
| 3 | Producer signaling: `pthread_cond_signal` on `PUSH` | Thread orchestration (waking sleepers) |
| 4 | Multi-threaded Producer-Consumer stress test (100 producers, 100 workers) | Concurrency validation |
| 5 | ThreadSanitizer run (`-fsanitize=thread`) to mathematically prove zero data races | Proving thread-safety |

### Day 4 — Fault Tolerance & WAL (4 hours)

| Hour | Task | What You Learn |
|:---:|:---|:---|
| 1 | Implement "In-Flight" State: Array mapping Socket FD to `Job*` | State machine tracking |
| 2 | Dead Socket Detection: If `read()==0` or `EPIPE`, pop from In-Flight, push to Heap | At-Least-Once delivery guarantees |
| 3 | WAL implementation: `O_APPEND` incoming jobs, call `fsync` | Disk durability and Page Cache |
| 4 | WAL Tombstones: Append a "DELETE" record when `ACK` is received | Idempotent log compaction |

---

## Days 5–10: DEFENSE PREPARATION PHASE

> [!IMPORTANT]
> You have **6 full days (30+ hours)** to master 40 grill topics. That's more than enough time for deep understanding + whiteboard practice.

---

### Day 5 — Algorithms Deep Dive (Min-Heaps) (5 hours)

**Goal**: Be able to whiteboard the exact math of a Min-Heap and explain why you didn't just use a Linked List.

1. **Why not a Linked List?**
   - Linked lists require `O(N)` scans to find where to insert a high-priority job. A Min-Heap inserts in `O(log N)`. Under heavy load (100,000 jobs), a linked list fails entirely.
2. **Min-Heap Array Math**
   - Must whiteboard: If you are at index `i`, parent is `(i-1)/2`, left child is `2i+1`, right child is `2i+2`.
3. **Bubble-Up (`heap_push`)**
   - Must whiteboard: Insert at the end of the array. `while (arr[i] < arr[parent(i)]) { swap(i, parent(i)); i = parent(i); }`.
4. **Bubble-Down (`heap_pop`)**
   - Must whiteboard: Take the last element, put it at the root (index 0). Compare with left and right children. Swap with the *smallest* child. Repeat until smaller than both children.
5. **Dynamic Resizing (`realloc`)**
   - When the array is full, double its capacity. This makes the insertion cost *amortized* `O(1)` memory-wise.

---

### Day 6 — Concurrency Deep Dive (The Hard Part) (6 hours)

**Goal**: Flawlessly defend Condition Variables and the Producer-Consumer problem.

6. **The Producer-Consumer Problem**
   - Draw the classic buffer problem. Producers add, consumers take. The buffer is shared.
7. **Condition Variables vs Polling**
   - Polling (`while(1) { check_queue(); }`) pins the CPU at 100%. `pthread_cond_wait` tells the OS to put the thread to sleep, yielding the CPU entirely.
8. **Spurious Wakeups (`while` vs `if`)**
   - **The most critical question**: Why use `while(empty) { cond_wait(); }` instead of `if(empty) { cond_wait(); }`?
   - Answer: The OS (Linux scheduler, signals) can wake a thread randomly even if no `cond_signal` was sent. You MUST re-verify the condition (that the queue is not empty) upon waking up.
9. **Lost Wakeups**
   - If a producer calls `cond_signal` *before* the consumer calls `cond_wait`, the signal vanishes. The consumer then goes to sleep and stays asleep forever. This is why the mutex *must* be held when checking the queue state.
10. **The Thundering Herd (`cond_signal` vs `cond_broadcast`)**
    - `cond_broadcast` wakes ALL sleeping workers. If you push 1 job, waking 100 workers causes 99 of them to fight for the mutex, realize the queue is empty again, and go back to sleep. This destroys CPU performance. You use `cond_signal` to wake exactly 1 worker.
11. **Mutex Internals (Futex)**
    - In userspace, a mutex is an atomic Compare-And-Swap (CAS). It only enters the kernel (`futex`) if it is contended. Uncontended mutexes take ~5 nanoseconds.
12. **Priority Inversion**
    - A high-priority thread gets stuck waiting on a mutex held by a low-priority thread. Solved by Priority Inheritance.

---

### Day 7 — Distributed Systems & Fault Tolerance (5 hours)

**Goal**: Explain exactly how your system prevents data loss during worker node crashes.

13. **At-Least-Once Delivery**
    - The broker guarantees a job is processed *at least once*. If a worker dies, it is re-processed. 
14. **In-Flight State Tracking**
    - You must explain how you map a socket FD to a Job pointer. When `read()` returns `0` (clean disconnect) or `EPIPE` (dirty disconnect), you catch the error, retrieve the Job pointer, lock the heap, and push it back in.
15. **The Two Generals Problem (Network Partitions)**
    - What if the worker finishes the job and sends the `ACK`, but the router drops the packet? The broker thinks the worker died, and re-queues the job.
16. **Worker Idempotency**
    - Because of the Two Generals problem, workers might receive the exact same job twice. The *worker* application must be idempotent (e.g., executing a database `UPDATE` that is safe to run twice).
17. **Dead Letter Queues (Theory)**
    - Even if we didn't implement it, explain how you *would*: Add a `retry_count`. If a job crashes the worker 3 times, move it to a DLQ so it doesn't poison the whole cluster.

---

### Day 8 — Storage & Networking (Identical to VeloceStore) (5 hours)

**Goal**: Defend the Write-Ahead Log and TCP Socket lifecycle.

18. **`O_APPEND` Atomicity**
    - Why `O_APPEND` guarantees atomic writes by the kernel, whereas `lseek(SEEK_END)` + `write()` can cause interleaving data corruption between threads.
19. **Page Cache vs Physical Disk**
    - `write()` just copies data to RAM (OS Page Cache). If the power cuts, data is lost.
20. **`fsync()` vs `fdatasync()`**
    - `fsync()` blocks until the physical disk controller acknowledges the write.
21. **TCP `TIME_WAIT` & `SO_REUSEADDR`**
    - Why ports lock for 60 seconds after closing, and how `SO_REUSEADDR` bypasses this for fast restarts.
22. **TCP Stream Framing**
    - `write("PUSH A\nPUSH B\n")` can arrive as a single chunk. You must explain your read-buffer loop searching for the `\n` delimiter.
23. **`SIGPIPE`**
    - Writing to a closed client socket sends a `SIGPIPE` signal which instantly kills the server. You must call `signal(SIGPIPE, SIG_IGN)` to ignore it and check `errno == EPIPE`.
24. **Why not `epoll`?**
    - The thread-per-client model is fine for this scope. To scale to 10,000+ connections, you would move to an event loop (`epoll`) to eliminate OS context-switching overhead.

---

### Day 9 — Mock Interview Practice (5 hours)

#### Hour 1: Elevator Pitch Drill (Repeat 5 Times Out Loud)
> "I built a fault-tolerant, priority-based distributed job queue in pure C. To guarantee at-least-once delivery, I implemented an In-Flight state machine that tracks socket connections and automatically re-queues jobs if a worker node crashes. To optimize routing, I wrote a custom Min-Heap data structure that bubbles high-priority jobs to the front in `O(log N)` time. The system leverages POSIX condition variables to orchestrate sleeping workers with zero idle CPU overhead, and uses a Write-Ahead Log to survive server crashes."

#### Hour 2: Rapid-Fire Q&A
1. **Why a Min-Heap instead of a Linked List?** → `O(log N)` insertions vs `O(N)`.
2. **What is a Spurious Wakeup?** → OS waking a thread without a `cond_signal`. Use a `while` loop.
3. **What is the Thundering Herd?** → `cond_broadcast` waking 100 threads for 1 job. Use `cond_signal`.
4. **How do you guarantee a job isn't lost if a worker loses WiFi?** → In-Flight state tracking mapped to the Socket FD.
5. **Is `write()` durable?** → No, it goes to RAM. You must `fsync`.

#### Hour 3-4: Whiteboard Drill
*   Draw the exact Bubble-Up pseudocode from memory.
*   Draw the exact `cond_wait` Producer-Consumer loop from memory.
*   Draw the exact flow of the `EPIPE` recovery logic.

---

### Day 10 — Final Review & Weak Spots (5 hours)

Read through your code line by line. If the interviewer points to any line and asks "Why did you do this?", you must have a 10-second answer prepared.

**Common Weak Spots to double-check (And how to defend them):**

1.  **The Array Resizing Logic (`realloc`)**
    *   **The Trap**: An interviewer asks, "If the queue grows to 10,000 jobs, won't `realloc` be incredibly slow if it has to copy the entire array every time?"
    *   **The Defense (Way Around)**: Defend the **Amortized `O(1)`** resizing strategy. When the heap hits capacity, you don't increase it by 1; you double it (`capacity = capacity * 2`). This means `realloc` happens logarithmically rarely. Furthermore, explain that `realloc` on Linux will attempt to extend the memory segment *in-place* if there is contiguous space available in the virtual address space, meaning it often doesn't even need to copy the memory at all.
2.  **String Parsing (`strtok` vs `strtok_r`)**
    *   **The Trap**: The interviewer looks at your TCP parsing code and asks, "If 10 threads receive jobs at the exact same time, will your string parser corrupt the data?"
    *   **The Defense (Way Around)**: You must confidently explain that the standard C `strtok()` function is fundamentally **not thread-safe** because it uses a hidden, global static variable in glibc to remember where it left off in the string. If Thread A calls it, and Thread B preempts and calls it, Thread A's state is permanently destroyed. You defend that you strictly used `strtok_r()` (the reentrant version), which requires you to pass a `char **saveptr`. This tracks the parsing state locally on the thread's own stack, making it 100% thread-safe.
3.  **The Mutex Scope (I/O Under a Lock)**
    *   **The Trap**: The interviewer looks at your worker loop and asks, "Why didn't you just `write()` the job string to the TCP socket while you still had the global heap mutex locked? It seems safer."
    *   **The Defense (Way Around)**: State this as an absolute rule: **Never perform network or disk I/O while holding a global lock.** Network `write()` calls can block if the client's TCP receive window is full, or it can take milliseconds to resolve. If you hold the global heap mutex while writing to the network, your entire broker freezes and the other 9,999 clients will time out. The correct way around: Lock the mutex -> Pop the job -> Move it to the In-Flight state -> **Unlock the mutex** -> Then `write()` to the socket.

---

---

# Part 2: The Builder's Deep Dive — Everything You Actually Wrote

> This section is the complete technical reference for the TaskBroker codebase as built. Read this after Part 1. Use it to defend every line of code during interviews.

---

## Section A: Architecture Diagram & System Overview

### Diagram 1 — System Architecture

```mermaid
graph TB
    subgraph CLIENTS["External Clients"]
        P1["Producer 1"]
        P2["Producer 2"]
        W1["Worker 1"]
        W2["Worker 2"]
    end

    subgraph BROKER["TaskBroker Server — broker.c"]
        direction TB
        ACCEPT["accept loop — main thread"]

        subgraph PHANDLER["Producer Thread — client_handler"]
            WAL_W["wal_append_push
O_APPEND + fsync"]
            LOCK1["Lock heap_mutex"]
            PUSH["heap_push"]
            SIG["cond_signal — wake one worker"]
            UNLOCK1["Unlock heap_mutex"]
        end

        subgraph WHANDLER["Worker Thread — client_handler"]
            LOCK2["Lock heap_mutex"]
            WAIT["while heap empty
cond_wait — 0 percent CPU"]
            POP["heap_pop"]
            UNLOCK2["Unlock heap_mutex"]
            INFLIGHT["in_flight[fd] = job"]
            SEND["write job to socket
OUTSIDE the lock"]
        end

        subgraph SHARED["Global Shared State — Protected by heap_mutex"]
            MUTEX["pthread_mutex_t heap_mutex
pthread_cond_t heap_cond"]
            HEAP["MinHeap global_heap
Array-backed binary tree
O log N push and pop"]
            IFMAP["Job in_flight[1024]
Socket FD to Job pointer mapping"]
        end

        subgraph DISK["Persistence Layer"]
            WALFILE["taskbroker.wal
Append-Only Log on disk
PUSH-42-1-cmd_0
ACK-42"]
        end
    end

    P1 -->|TCP PUSH| ACCEPT
    P2 -->|TCP PUSH| ACCEPT
    W1 -->|TCP POP| ACCEPT
    W2 -->|TCP POP| ACCEPT

    ACCEPT -->|pthread_create| PHANDLER
    ACCEPT -->|pthread_create| WHANDLER

    WAL_W --> LOCK1 --> PUSH --> SIG --> UNLOCK1
    PUSH --> HEAP
    SIG -.->|wakes| WAIT

    LOCK2 --> WAIT --> POP --> UNLOCK2 --> INFLIGHT --> SEND
    POP --> HEAP
    INFLIGHT --> IFMAP

    WAL_W --> WALFILE
```

---

### Diagram 2 — Job Lifecycle: Happy Path vs Failure Path

```mermaid
sequenceDiagram
    participant P as Producer
    participant B as Broker Thread
    participant HEAP as MinHeap
    participant WAL as taskbroker.wal
    participant W as Worker
    participant IF as in_flight array

    Note over P,IF: HAPPY PATH

    P->>B: TCP PUSH 25 cmd_3
    B->>WAL: wal_append_push 7421 25 cmd_3 + fsync
    B->>HEAP: Lock then heap_push then bubble_up
    B->>B: cond_signal — wake one worker
    B-->>HEAP: Unlock

    W->>B: TCP POP
    B->>HEAP: Lock then while empty cond_wait then woken
    B->>HEAP: heap_pop returns job id=7421 prio=25
    B-->>HEAP: Unlock
    B->>IF: in_flight[worker_fd] = job
    B->>W: JOB 7421 25 cmd_3 — sent OUTSIDE lock

    Note over W: 500ms simulated work

    W->>B: TCP ACK 7421
    B->>WAL: wal_append_ack 7421 + fsync
    B->>IF: Lock then free in_flight[fd] = NULL then Unlock
    Note over B,IF: Job lifecycle COMPLETE

    Note over P,IF: FAILURE PATH — Worker Crashes Mid-Job

    P->>B: TCP PUSH 10 cmd_X
    B->>WAL: wal_append_push 8888 10 cmd_X + fsync
    B->>HEAP: Lock heap_push Unlock and cond_signal

    W->>B: TCP POP
    B->>HEAP: heap_pop returns job id=8888
    B->>IF: in_flight[worker_fd] = job
    B->>W: JOB 8888 10 cmd_X

    Note over W: kill -9 — Worker process dies

    B->>B: read worker_fd returns 0 or EPIPE
    B->>IF: Lock — in_flight[fd] != NULL — job orphaned
    B->>HEAP: heap_push re-queued the orphaned job
    B->>B: cond_signal — another worker picks it up
    B->>IF: in_flight[fd] = NULL then Unlock
    Note over B: At-Least-Once delivery guaranteed
```

---

### Diagram 3 — Crash Recovery: WAL Replay on Boot

```mermaid
flowchart TD
    A["Broker Process Starts"] --> B["signal SIGPIPE SIG_IGN"]
    B --> C["global_heap = heap_create(1024)"]
    C --> D["wal_init — open taskbroker.wal O_WRONLY O_APPEND O_CREAT"]
    D --> E["wal_recover — open WAL read-only"]

    E --> F{"taskbroker.wal exists?"}
    F -->|No| G["Skip recovery — start with empty heap"]
    F -->|Yes| H["fgets line by line"]

    H --> I{"Parse command token"}
    I -->|"PUSH id prio cmd"| J["malloc Job
active_jobs[id] = job"]
    I -->|"ACK id"| K["free active_jobs[id]
Set to NULL"]
    I -->|"Partial or corrupt line"| L["silently ignored"]

    J --> M{"More lines?"}
    K --> M
    L --> M
    M -->|Yes| H
    M -->|No — EOF| N["Loop through active_jobs array"]

    N --> O{"active_jobs[i] != NULL?"}
    O -->|"Yes — unACKd job found"| P["heap_push job into Min-Heap"]
    O -->|"No — already ACKd"| Q["Skip"]
    P --> R{"More entries?"}
    Q --> R
    R -->|Yes| O
    R -->|No| S["Print: WAL Recovery complete — N jobs restored"]

    G --> T["socket then bind then listen"]
    S --> T
    T --> U["while 1: accept then pthread_create"]
    U --> V["Broker fully operational
Consistent state guaranteed before first client"]
```

---

## Section B: File-by-File Code Walkthrough

### `src/job.h` — The Core Data Unit

```c
typedef struct {
    int id;               // Unique Job ID (generated by broker on PUSH)
    int priority;         // Lower number = higher priority (Min-Heap invariant)
    char cmd[256];        // The payload string from the producer
} Job;
```

**Why a fixed-size `char cmd[256]` instead of `char*`?**
If we stored `char*`, we would need to `malloc` the string separately and `free` it across multiple threads and state transitions (PUSH, POP, ACK, re-queue). This creates use-after-free and double-free risks. A fixed-size array embeds the string directly inside the struct: one `malloc(sizeof(Job))` allocates everything, one `free(job_ptr)` deallocates everything — cleanly from any thread.

---

### `src/heap.c` — The Priority Engine

**Why `Job**` (pointer array) not `Job[]` (struct array)?**
Swapping items during `bubble_up`/`bubble_down` would copy `sizeof(Job)` = 264 bytes per swap if we stored structs. Storing pointers means each swap exchanges exactly 8 bytes. This is 33x cheaper.

**Heap Index Math (must whiteboard):**
- Parent of index `i`: `(i - 1) / 2`
- Left child: `2i + 1`
- Right child: `2i + 2`
- Root at index `0` always holds the minimum priority element.

**`heap_push` logic:**
```
1. Double capacity if heap->size >= heap->capacity (amortized O(1))
2. Place job at heap->array[heap->size]   (end of array)
3. bubble_up(heap, heap->size)            (restore Min-Heap upward)
4. heap->size++
```

**`heap_pop` logic:**
```
1. Save root = heap->array[0]             (minimum priority job)
2. Move last element to root              (array[0] = array[size-1])
3. heap->size--
4. bubble_down(heap, 0)                   (restore Min-Heap downward)
5. return root
```

**Why move last to root (not second element)?**
In a heap, only the root and last leaf can be removed cleanly. Placing the last element at root and doing O(log N) `bubble_down` is the optimal repair. Shifting all others would be O(N).

---

### `src/wal.c` — Durability Layer

**`wal_init()` flags:**
- `O_APPEND` — atomic kernel-level seek+write, thread-safe for concurrent producers.
- `O_WRONLY` — write-only during normal operation.
- `O_CREAT` — creates file on first run.

**Write-then-Fsync Pattern:**
```
write() → copies to OS Page Cache (RAM only, lost on power cut)
fsync() → blocks until physical disk controller confirms storage
```
WAL write happens BEFORE acquiring the heap mutex, so slow disk I/O never blocks other threads.

**`wal_recover()` algorithm:**
```
1. Open WAL read-only
2. For each line:
     PUSH|id|prio|cmd  → malloc(Job), store at active_jobs[id]
     ACK|id            → free(active_jobs[id]), set NULL
3. After EOF: push all active_jobs[i] != NULL into Min-Heap
4. Called BEFORE listen() — broker is consistent before first client
```

---

### `src/broker.c` — The Orchestrator

**Startup Sequence (ordering is mandatory):**
```
signal(SIGPIPE, SIG_IGN)         ← 1: Prevent single dead socket from killing broker
global_heap = heap_create(1024)  ← 2: Allocate heap
wal_init("taskbroker.wal")       ← 3: Open WAL for appending
wal_recover(global_heap)         ← 4: Replay WAL before any client can connect
socket()→bind()→listen()         ← 5: Open network
while(1) { accept()→pthread_create() }  ← 6: Serve clients
```

**Why `malloc` the client_fd before passing to thread:**
Stack variable `client_fd` is overwritten by the next `accept()` loop iteration before the new thread reads it. Heap-allocating a unique `int` per connection gives each thread stable, private memory. Thread calls `free(arg)` immediately upon entry.

**TCP Stream Framing:**
TCP delivers bytes, not messages. `"PUSH|1|cmd\n"` may arrive as `"PUSH|1|c"` then `"md\n"`. We accumulate bytes in a buffer, find `\n` with `strchr`, process one complete message, then `memmove` remaining bytes to buffer start. Repeat until no `\n` found.

**The Condition Variable `while` loop:**
```c
while (global_heap->size == 0) {
    pthread_cond_wait(&heap_cond, &heap_mutex);
}
```
`cond_wait` can return spuriously (OS wakes thread without `cond_signal`). `while` forces re-verification. `if` would call `heap_pop` on an empty heap → NULL dereference → crash.

---

## Section C: End-to-End Lifecycle of a Single Job

### Happy Path
```
STEP 1 [Producer connects]:
  producer: connect(127.0.0.1:8080)
  broker: accept() → malloc(int*) → pthread_create(client_handler)

STEP 2 [PUSH handler]:
  producer: send("PUSH|25|cmd_3\n")
  broker thread:
    strtok_r → cmd="PUSH", prio="25", payload="cmd_3"
    malloc(Job) → {id=7421, priority=25, cmd="cmd_3"}
    wal_append_push(7421, 25, "cmd_3") + fsync()  ← durability
    [Lock] heap_push(job) → cond_signal() [Unlock]

STEP 3 [POP handler - worker wakes]:
  worker: send("POP\n")
  broker thread:
    [Lock] while(heap empty) cond_wait()  ← woken by cond_signal
    job = heap_pop()   ← {id=7421, priority=25}
    [Unlock]
    in_flight[worker_fd] = job  ← track ownership
    write(worker_fd, "JOB|7421|25|cmd_3\n")  ← OUTSIDE lock

STEP 4 [ACK handler]:
  worker: usleep(500ms) → send("ACK|7421\n")
  broker thread:
    wal_append_ack(7421) + fsync()  ← tombstone written
    [Lock] free(in_flight[worker_fd]); in_flight[worker_fd]=NULL [Unlock]
    ← Job lifecycle COMPLETE
```

### Failure Path (Worker Crashes Mid-Job)
```
  kill -9 <worker_pid>
  OS sends TCP RST to broker
  broker: read(worker_fd) returns 0 or -1
  broker thread (exits read loop):
    [Lock]
    in_flight[worker_fd] != NULL  ← job detected as orphaned
    heap_push(global_heap, in_flight[worker_fd])  ← RE-QUEUED
    cond_signal()  ← another worker will pick it up
    in_flight[worker_fd] = NULL
    [Unlock]
    close(worker_fd)
```

### Crash Recovery (Broker Power Loss)
```
  WAL on disk after STEP 2 (broker crashes before STEP 4):
    PUSH|7421|25|cmd_3

  Broker restarts:
    wal_recover():
      Read "PUSH|7421|25|cmd_3" → active_jobs[7421] = malloc'd Job
      No matching "ACK|7421" found
      heap_push(heap, active_jobs[7421])
      "WAL Recovery: 1 jobs restored"
    listen()  ← now safe to accept connections
    → Workers reconnect; job 7421 re-processed
```

---

## Section D: Build Phases — Git Branch History

| Branch | Commits | What Was Built |
|:---|:---:|:---|
| `feature/data-structures` | 1–3 | `job.h`, `heap.h`, `heap.c`, `test_heap.c` |
| `feature/networking` | 4–7 | `broker.c` (TCP + threads), `producer.c`, `worker.c` |
| `feature/concurrency` | 8–10 | Mutex, Condition Variables, In-Flight array |
| `feature/fault-tolerance` | 11–13 | `wal.h`, `wal.c`, crash recovery in `main()` |

Each branch is a working, testable milestone. `git log --oneline` shows 13 clean commits evolving from a simple data structure to a fault-tolerant distributed system — engineering discipline visible to any reviewer.

---

## Section E: The STAR Method — Your Interview Narrative

**Situation:**
"At IIITB, I wanted to build a systems project demonstrating concurrency, networking, and distributed systems — the exact domains targeted by AWS, Cloudflare, and Datadog. I needed something non-trivial that I could defend deeply at the code level."

**Task:**
"I designed and built TaskBroker — a fault-tolerant, priority-based distributed job queue in pure C, no external libraries. Three hard problems: O(log N) priority ordering, managing concurrent workers without busy-waiting, and zero job loss even if both the broker and a worker crash simultaneously."

**Action:**
"Built in four phases. Phase 1: dynamic array-backed Min-Heap with amortized O(1) resizing, verified with 10,000-job randomized unit tests. Phase 2: TCP server with thread-per-client model, robust byte-stream framing with a memmove buffer, and thread-safe string parsing using strtok_r. Phase 3: POSIX mutex for heap protection and POSIX condition variables for zero-CPU worker sleep, waking exactly one thread per job with cond_signal. Phase 4: append-only Write-Ahead Log with O_APPEND atomic writes and fsync durability, combined with an In-Flight state machine indexed by socket FDs for dead connection detection and automatic job re-queuing. Crash recovery replays the WAL before accepting any connections."

**Result:**
"Complete, production-architecture-inspired system with correct priority ordering, zero idle CPU, and at-least-once delivery. I can explain every single line under grilling. The project demonstrates simultaneous reasoning about Mutexes, Condition Variables, Distributed Systems failure modes, and Linux kernel semantics."

---

## Section F: Comprehensive Grill Topics & Defenses

### Category 1: Data Structure Grilling

**Q: Why Min-Heap instead of sorted linked list?**
Sorted linked list needs O(N) scan per insertion. At 100,000 jobs: 100,000 comparisons per insert. Min-Heap: O(log N) both push and pop. At 100,000 jobs: ~17 comparisons. The interviewer tests: "Do you know when NOT to use a linked list?"

**Q: Whiteboard the heap index math.**
Parent of `i`: `(i-1)/2`. Left child: `2i+1`. Right child: `2i+2`. Root at `0` is always minimum.

**Q: Won't realloc be slow copying the entire array when it grows?**
Amortized O(1). We double capacity on overflow — realloc happens only log₂(N) times. Linux realloc often extends the virtual memory segment in-place (zero copy). Even if it copies, total work across N inserts is 2N element moves, amortized O(1) per insert.

**Q: Can heap_pop return NULL? What happens?**
Yes, on empty heap. Our POP handler uses `while(size==0) cond_wait()` — guaranteed non-empty before pop. Defensive `if(job)` check also present. Without the while loop, NULL dereference on `job->id` crashes the broker.

---

### Category 2: Concurrency Grilling

**Q: Two workers send POP simultaneously. Do they get the same job?**
No. Both reach `pthread_mutex_lock`. The futex guarantees only one acquires the lock; the other blocks. They cannot pop the same job. Without mutex: two threads with same `Job*` pointer → double-free crash.

**Q: Why while(empty) not if(empty) with cond_wait?**
`pthread_cond_wait` returns spuriously — OS wakes threads due to signals/scheduler events with no cond_signal. `if` calls `heap_pop` on empty heap → NULL → crash. `while` re-verifies actual heap state on every wakeup.

**Q: Why cond_signal inside the lock, not after release?**
Signals after unlock can race: Producer unlocks, Worker re-acquires, checks heap, goes to sleep, Producer then signals a sleeping thread — wasted signal. More critically prevents lost wakeups: if Producer signals before Consumer enters cond_wait, the signal vanishes. Holding mutex ensures signal either wakes Consumer or Consumer sees non-empty heap before sleeping.

**Q: Thundering Herd — did you cause it?**
No. `cond_broadcast` wakes ALL sleeping workers. 1 job + 100 workers = 99 futile mutex acquisitions. I used `cond_signal` which wakes exactly one worker per one job.

**Q: Priority Inversion?**
Theoretically possible: high-priority worker thread blocked on mutex held by low-priority producer thread. Production fix: POSIX Priority Inheritance (`PTHREAD_MUTEX_PROTECT_NP`). Defensible known limitation for student project.

**Q: strtok vs strtok_r — why does it matter?**
`strtok()` uses a hidden global static variable in glibc. If Thread A calls it, Thread B preempts and calls it — Thread A's saved position is permanently overwritten, both parses are corrupted. `strtok_r()` requires explicit `char **saveptr` on each thread's own stack — 100% reentrant, thread-safe.

---

### Category 3: Storage & Networking Grilling

**Q: Why O_APPEND is thread-safe but lseek+write is not?**
`O_APPEND` makes write() an atomic kernel operation — seek-to-end and write are one uninterruptible system call. With `lseek+write`: Thread A seeks to offset 100, Thread B seeks to 100, Thread B writes (file now at 120), Thread A writes at 100 — overwrites Thread B's data. `O_APPEND` is the standard correct solution for concurrent log appending.

**Q: fsync is slow (1-10ms). Won't every PUSH be slow?**
Correct trade-off: durability over throughput. Production mitigation: Group Commit — accumulate 10ms of writes, fsync once, amortize disk cost across many operations. WAL write also happens before heap mutex lock, so disk I/O never blocks heap operations for other threads.

**Q: Why SO_REUSEADDR?**
TCP connections enter TIME_WAIT for ~60 seconds after close. Fast broker restart hits `EADDRINUSE` from bind(). `SO_REUSEADDR` allows rebinding a port in TIME_WAIT when no socket is actively using it. Essential for fast restarts during operations and incident recovery.

**Q: ACK packet dropped — is the job lost?**
No — At-Least-Once delivery. Broker detects connection timeout via TCP keepalive or RST, reads 0 bytes, finds job in `in_flight[fd]`, re-queues it. Same job processed again. Workers must be idempotent: `UPDATE users SET processed=true WHERE id=X` is safe twice; blind `INSERT INTO log(...)` is not.

**Q: Why not epoll?**
Thread-per-client gives clean, linear logic — each connection has its own stack, buffer, and blocking cond_wait. With epoll, one event loop manages all connections but blocking operations are forbidden, forcing a complex state machine per connection. At 1,024 connections, thread-per-client is clean, correct, fast. Upgrade path to epoll + non-blocking I/O is clear and I can describe it fully.

---

### Category 4: WAL & Recovery Grilling

**Q: WAL grows forever. Won't startup take minutes after a week?**
Known limitation. Production solution: periodic Snapshotting — background thread serializes current heap to `snapshot.bin`, atomically replaces old checkpoint, truncates WAL. On restart: load snapshot, replay only WAL entries after checkpoint timestamp. Exactly how PostgreSQL (WAL + base backups) and Kafka (log segments + offsets) work.

**Q: What if WAL is partially written due to crash mid-fsync?**
Partial line not picked up by fgets → strtok_r fails to parse it → silently ignored. That one job at crash boundary may be lost. Production fix: CRC32 checksum per record (`PUSH|742|1|cmd|CRC:0xABCD\n`). Recovery validates checksum, discards corrupted entries → bit-perfect recovery.

**Q: Is `active_jobs[10000]` in wal_recover() a good data structure?**
Deliberate simplicity: direct-address table, O(1) insert and O(1) lookup by Job ID. Fixed 10,000-job upper bound. For production: hash table (open addressing, load factor 0.7) or standard hash map. The replay concept — build map, subtract ACKs, push remainder to heap — is identical at any scale.

---

## Section G: Weak Points — Know Your Limits and Defend Them

### Weakness 1: Non-Atomic In-Flight Assignment
**The issue:** `in_flight[client_fd] = job` happens after `pthread_mutex_unlock`. Theoretically, thread death between unlock and assignment leaves `in_flight[fd]` = NULL and loses the job.
**Reality:** Same thread — no other thread touches this fd's slot. But strictly, assignment belongs inside the lock.
**Defense:** "I acknowledge this. The correct fix: move `in_flight[client_fd] = job` before `pthread_mutex_unlock`. My choice illustrated keeping I/O outside the lock, but production code would be stricter."

### Weakness 2: Non-Unique Random Job IDs
**The issue:** `rand() % 10000` — not thread-safe (global state in glibc), collisions after 10,000 jobs.
**Defense:** "Production fix: `static atomic_int next_id = 0; int id = atomic_fetch_add(&next_id, 1);` — lock-free, monotonically increasing, guaranteed unique, thread-safe."

### Weakness 3: WAL Never Compacted
*(Defend with Snapshot + Incremental Replay — see Section F.)*

### Weakness 4: Fixed `in_flight[1024]` Array
**The issue:** `ulimit -n > 1024` → `client_fd = 1025` → out-of-bounds write.
**Defense:** "Production fix: dynamically sized hash map keyed by fd, or array sized to `RLIMIT_NOFILE`. Add `assert(client_fd < 1024)` as a compile-time guard."

### Weakness 5: No Graceful Shutdown
**The issue:** `Ctrl+C` kills process instantly — in-flight jobs not drained, sockets not closed.
**Defense:** "Production fix: `SIGTERM` handler sets `volatile sig_atomic_t shutdown_requested = 1`, closes listening socket, joins all client_handler threads, closes WAL fd. WAL guarantees recoverability even without graceful shutdown."

### Weakness 6: Blocking fsync Per WAL Write
*(Defend with Group Commit / background fsync thread — see Section F.)*

---

## Section H: Quick-Reference Cheat Sheet for Interview Day

| Concept | Your Implementation | One-Line Defense |
|:---|:---|:---|
| Priority Queue | Min-Heap in `heap.c` | O(log N) push/pop vs O(N) linked list insert |
| Heap Math | Parent=(i-1)/2, children=2i+1,2i+2 | Standard binary tree in flat array |
| Dynamic Resize | realloc, capacity x2 | Amortized O(1); Linux often extends in-place |
| Thread Model | pthread_create per client | Simple, correct, defensible for <1024 connections |
| Zero-CPU Idle | pthread_cond_wait | OS-level sleep; 0% CPU until cond_signal |
| Spurious Wakeup | while(empty) not if(empty) | Re-verify condition on every OS wakeup |
| Thundering Herd | cond_signal not cond_broadcast | 1 job wakes exactly 1 worker |
| Thread-Safe Parse | strtok_r not strtok | saveptr on stack, not glibc global state |
| I/O Under Lock | Never — WAL write before lock | Disk/network blocks for ms; hold mutex briefly |
| Fault Tolerance | In-Flight array + WAL | At-Least-Once via dead socket detection |
| Atomic Log Append | O_APPEND flag | Kernel combines seek+write atomically |
| Disk Durability | fsync() after every write | Physical disk confirmation, not just Page Cache |
| Crash Recovery | wal_recover() before listen() | Replay unACKd PUSHes before accepting clients |
| Scaling Limit | 1024 FDs, no epoll | Known; upgrade path: epoll + non-blocking I/O |
| WAL Growth | No compaction | Known; production fix: Snapshot + incremental replay |
