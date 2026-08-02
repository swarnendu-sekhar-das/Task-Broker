# TaskBroker In-Depth Study Session Notes

## Step 1: The Core Data Engine (Min-Heap & Data Models)

Without a way to efficiently sort and store jobs by priority, the broker would collapse under heavy load. We use a **Min-Heap** for this. Let's look at the data models and the low-level design decisions behind them.

### 1. The `Job` Struct (Memory Layout)
Located in `src/job.h`:

```c
#define MAX_CMD_LEN 256

typedef struct {
    int id;               // Unique Job ID
    int priority;         // Lower number = higher priority (Min-Heap invariant)
    char cmd[MAX_CMD_LEN]; // Fixed-size payload array
} Job;
```

**Design Decision: Why use `char cmd[256]` instead of a dynamic `char* cmd`?**
If we used a `char*`, we would have to `malloc` the struct, and then `malloc` the string separately. When a worker finishes a job, we would have to remember to `free(job->cmd)` and then `free(job)`. Across multiple threads, this creates massive risks for **Double-Free** or **Use-After-Free** bugs, or memory leaks. 
By using a fixed-size array (`char cmd[256]`), the string is embedded directly inside the struct's memory footprint. A single `malloc(sizeof(Job))` allocates everything in one contiguous block of memory, and a single `free(job)` cleanly destroys it.

### 2. The `MinHeap` Struct (Pointer Arrays vs. Struct Arrays)
Located in `src/heap.h`:

```c
typedef struct {
    Job** array;    // Array of POINTERS to Jobs
    int size;       // Current number of elements
    int capacity;   // Maximum elements before needing resize
} MinHeap;
```

**Design Decision: Why `Job** array` instead of `Job* array` (an array of structs)?**
A heap works by constantly swapping elements (bubbling up and bubbling down). 
If we stored the actual `Job` structs in the array, every time we swapped two elements, we would have to copy the entire struct (`sizeof(Job)` = roughly 264 bytes). 
By storing an array of *pointers* (`Job**`), we only swap memory addresses (8 bytes on a 64-bit system). This makes the sorting algorithms about 33 times faster in terms of memory bandwidth.

### 3. Min-Heap Array Math (Binary Tree in a Flat Array)
A Min-Heap is conceptually a Binary Tree where the parent is always smaller (higher priority) than its children. However, pointers for left/right nodes are slow and fragment memory. Instead, we map the tree into a flat, contiguous array.

The fundamental, low-level math (you must know this for interviews) is:
If you are at index `i`:
*   **Parent Index** = `(i - 1) / 2`
*   **Left Child Index** = `2 * i + 1`
*   **Right Child Index** = `2 * i + 2`

The root of the tree (the absolute highest priority job) is **always at index 0**.

### 4. Insertion and Amortized O(1) Resizing (`heap.c`)
When a Producer pushes a new job, two things happen:

**A. Dynamic Resizing**
```c
if (heap->size >= heap->capacity) {
    heap->capacity *= 2;
    heap->array = (Job**)realloc(heap->array, heap->capacity * sizeof(Job*));
}
```
**Low-Level Concept:** We double the capacity. Why? If we increased it by 1 every time, we would constantly be copying the array (`O(N)`). By doubling it, `realloc` happens logarithmically rarely. This brings the cost of resizing down to **Amortized `O(1)`**. Furthermore, the Linux kernel will often try to extend the virtual memory segment in-place rather than physically copying the bytes.

**B. Bubble-Up (`O(log N)`)**
We place the new job at the very end of the array (`heap->array[heap->size]`), and then compare it to its parent. If it has a higher priority (lower number) than its parent, we swap them. We repeat this until it stops.

```c
static void bubble_up(MinHeap* heap, int index) {
    while (index > 0) {
        int parent = (index - 1) / 2;
        if (heap->array[index]->priority < heap->array[parent]->priority) {
            swap(&heap->array[index], &heap->array[parent]);
            index = parent; // Move up the tree
        } else {
            break;
        }
    }
}
```

### 5. Extraction (`heap_pop`)
When a Worker wants a job, we extract the root (index 0). But this leaves a hole at the root of our tree!
To fix it, we take the *last* element in the array, move it to the root, and let it "Bubble Down" to its correct position by swapping it with its smallest child.

```c
Job* heap_pop(MinHeap* heap) {
    if (!heap || heap->size == 0) return NULL;

    Job* root = heap->array[0]; // Save the highest priority job
    heap->array[0] = heap->array[heap->size - 1]; // Move last element to root
    heap->size--;
    
    if (heap->size > 0) {
        bubble_down(heap, 0); // O(log N) operation to restore the heap
    }
    return root;
}
```

---

### Interview Concept Check 🧠

**Question:** Imagine the broker receives a massive spike of traffic, and 100,000 jobs are pushed into the queue at once. If we had used a standard sorted Linked List instead of a Min-Heap, exactly why would the system catastrophically fail under this load? Compare the Big-O time complexity of inserting a job into our Min-Heap vs inserting into a sorted Linked List.

**Your Answer:** It would take order n time because we have to do a linear scan to find the correct position as compared to logn in heap latency would spike.

**Correction/Validation:** Spot on! Exactly correct. At 100,000 jobs, a linked list would require 100,000 comparisons (a linear scan) per insertion, completely blocking the server. The Min-Heap requires at most ~17 comparisons ($log_2(100,000)$), meaning latency remains perfectly flat even under massive load.

---

## Step 2: Persistence & Durability (The Write-Ahead Log)

Right now, if our server loses power, the entire heap in RAM is erased and all un-processed jobs are permanently lost. To fix this, we use a **Write-Ahead Log (WAL)**. 

### 1. The WAL Initialization (`wal_init`)
Located in `src/wal.c`:

```c
int wal_init(const char* filepath) {
    wal_filepath = strdup(filepath);
    // Open for appending, create if missing, write-only
    wal_fd = open(filepath, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (wal_fd < 0) { ... }
}
```

**Design Decision: Why is the `O_APPEND` flag absolutely critical here?**
Imagine you have 10 Producer threads trying to write jobs to the log at the exact same millisecond. 
If we used the standard approach of seeking to the end of the file (`lseek(SEEK_END)`) and then calling `write()`, we would have a race condition:
1. Thread A seeks to byte 100.
2. Thread B preempts, seeks to byte 100, and writes 20 bytes (file is now at 120).
3. Thread A wakes up and writes its 20 bytes *starting at byte 100*, overwriting and corrupting Thread B's data!

By using `O_APPEND`, the Linux kernel guarantees that the seek-to-end and the write operation happen as a **single, uninterruptible atomic action**. No matter how many threads try to log simultaneously, the kernel safely sequences them.

### 2. Durability: `write()` vs `fsync()`
Located in `wal_append_push`:

```c
void wal_append_push(int id, int priority, const char* cmd) {
    char buffer[512];
    int len = snprintf(buffer, sizeof(buffer), "PUSH|%d|%d|%s\n", id, priority, cmd);
    
    if (write(wal_fd, buffer, len) > 0) {
        fsync(wal_fd); // The crucial step
    }
}
```

**Low-Level Concept:** In Linux, when you call `write()`, it doesn't actually touch the physical hard drive. It just copies the bytes into RAM (the OS Page Cache). If the server loses power 1 millisecond later, the data is gone forever.
The `fsync(wal_fd)` system call forcefully blocks the thread until the physical disk controller acknowledges that the magnetic platter (or SSD NAND) has permanently stored the bytes. 

*Note for Interviews:* `fsync()` is slow (1-10ms). In a real production system like Kafka or PostgreSQL, you wouldn't fsync every single job individually. You would use "Group Commit" (batching 10ms of jobs together and fsyncing them once).

### 3. Tombstones and Log Compaction
When a worker finishes a job, we don't delete the `PUSH` record from the log. Modifying the middle of a file on disk is incredibly slow.
Instead, we append an `ACK` "Tombstone" to the end of the log:

```c
void wal_append_ack(int id) {
    char buffer[256];
    int len = snprintf(buffer, sizeof(buffer), "ACK|%d\n", id);
    if (write(wal_fd, buffer, len) > 0) {
        fsync(wal_fd);
    }
}
```

### 4. Crash Recovery (`wal_recover()`)
When the server boots up (before it accepts any network connections), it reads the WAL file line by line.

1.  If it sees `PUSH|7421`, it allocates a `Job` struct and stores it in an array (`active_jobs[7421] = job`).
2.  If it sees `ACK|7421` further down the file, it frees that job (`free(active_jobs[7421])` and sets it to `NULL`).
3.  When it reaches the end of the file (EOF), any job still remaining in the `active_jobs` array was pushed, but never finished! It loops through the array and pushes those orphaned jobs back into the Min-Heap.

---

### Interview Concept Check 🧠

**Question:** We mentioned that the WAL just grows forever as we keep appending `PUSH` and `ACK` strings. If this system runs for a year, the WAL file might be 500 Gigabytes, meaning a server reboot would take hours to process `wal_recover()`. As a systems engineer, if you were asked to fix this unbounded growth in a production environment (like how Postgres or Redis handles it), how would you design a mechanism to safely shrink or reset the WAL without losing data or blocking the live producers?

**Your Answer:** we could run cron jobs that wake up a thread at low traffic times and take lock on rows that contain logs from say 30 days back and delete that

**Correction/Validation:** That is great intuition—doing it periodically in the background (like a cron job) during low traffic is exactly the right instinct! 

However, there is one low-level catch with how file systems work: a WAL is a flat text file, not a database table. You actually **cannot delete data from the beginning or middle of a file** on Linux without rewriting the entire file. The OS just doesn't support "shifting" bytes leftward on the physical disk.

Because of this, the industry-standard solution (used by Redis, PostgreSQL, and Kafka) is a combination of **Snapshotting (Checkpointing)** and **Log Rotation**:
1.  A background thread wakes up periodically.
2.  It briefly locks the Min-Heap, writes the *entire current state* of the heap directly to a new file (e.g., `snapshot.bin`), and records the exact timestamp.
3.  Because the `snapshot.bin` now contains the perfect current state, the system can safely **delete the entire old WAL file** and start a brand new, empty WAL.
4.  If the server crashes, it loads `snapshot.bin` into memory instantly, and then replays only the tiny new WAL file for any jobs that arrived after the snapshot.

---

## Step 3: Networking & Stream Framing

Now that we can store data safely, how do we get data into the broker? We use TCP Sockets. 

### 1. Fast Restarts (`SO_REUSEADDR`)
Located in `main()` inside `src/broker.c`:

```c
int opt = 1;
setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```
**Low-Level Concept:** When you kill a TCP server, the OS keeps the port locked in a `TIME_WAIT` state for about 60 seconds to ensure delayed packets on the network don't accidentally get delivered to a new application. If you try to reboot the broker immediately, it will crash with `Address already in use`. 
`SO_REUSEADDR` tells the Linux kernel: *"I know what I'm doing, let me bind to this port immediately."* This is critical for fast incident recovery.

### 2. Defending Against `SIGPIPE`
```c
signal(SIGPIPE, SIG_IGN);
```
**Low-Level Concept:** If a Worker's laptop dies, their TCP connection breaks. If the Broker tries to `write()` a job to that dead socket, the Linux kernel will instantly send a `SIGPIPE` signal to the Broker. **The default behavior of `SIGPIPE` is to instantly kill your entire server process.** 
By setting it to `SIG_IGN` (Ignore), the `write()` function simply returns an `EPIPE` error code instead, allowing us to handle it gracefully without the server crashing.

### 3. TCP Byte-Stream Framing (The Most Common Networking Bug)
Located in `client_handler()` in `src/broker.c`:

```c
char buffer[2048];
int buffer_len = 0;

int bytes_read = read(client_fd, buffer + buffer_len, ...);
buffer_len += bytes_read;

char* newline;
while ((newline = strchr(buffer, '\n')) != NULL) {
    *newline = '\0'; 
    // ... parse the command using strtok_r ...

    // Shift remaining bytes to the front of the buffer!
    int consumed = (newline - buffer) + 1;
    memmove(buffer, newline + 1, buffer_len - consumed);
    buffer_len -= consumed;
}
```

**Design Decision: Why do we need this complicated `memmove` buffer logic?**
TCP is a **Byte-Stream Protocol**, not a Message Protocol. 
If a Producer quickly sends `"PUSH|1|A\n"` and then `"PUSH|2|B\n"`, TCP does not guarantee they arrive together. 
When you call `read()`, you might receive `"PUSH|1|A\nPU"`. 
If you just parse that chunk blindly, the second command is corrupted. 

We must append bytes to a `buffer`, use `strchr` to search for the `\n` delimiter, process exactly one full message, and then use `memmove` to slide any remaining bytes (like `"PU"`) to the front of the buffer so the next `read()` can finish the word.

---

### Interview Concept Check 🧠

**Question:** We mentioned that TCP is a Byte-Stream protocol. In our `client_handler` thread, we use `strtok_r` to parse the strings we extract from the buffer. There is an older C function just called `strtok()` (without the `_r`). If we had used `strtok()` instead of `strtok_r()` inside our multi-threaded server, it would cause massive data corruption. Why is `strtok()` fundamentally not thread-safe, and how does `strtok_r()` fix the problem?

**Your Answer:** strtok takes recurrent locks and strtok does not thats why it is not thread safe

**Correction/Validation:** That is a very common guess, but it's actually not about locks at all! It's about **hidden state**. 

Here is the exact trap interviewers lay for this question:
The standard `strtok()` function relies on a **hidden, global `static` variable** inside the C standard library (glibc) to remember where it left off in your string between calls. 
If Thread A calls `strtok()` and pauses mid-string, and Thread B suddenly preempts it and calls `strtok()` on a completely different string, Thread B overwrites that hidden global variable. When Thread A resumes, its parsing state is completely destroyed!

The `_r` in `strtok_r` stands for **Reentrant** (which implies thread-safe). It fixes this by forcing you to pass a `char** saveptr`. This variable lives locally on each thread's own stack, meaning Thread A and Thread B have completely separate memory to track their parsing state. No locks needed, just private memory!

---

## Step 4: Concurrency & Orchestration (The Hardest Part)

This is where interviewers will spend 80% of their time grilling you. We have multiple Producer threads and multiple Worker threads all trying to touch the `global_heap` at the exact same time.

### 1. The Global Mutex (Mutual Exclusion)
Located in `src/broker.c`:

```c
pthread_mutex_t heap_mutex = PTHREAD_MUTEX_INITIALIZER;
```
Before any thread can call `heap_push` or `heap_pop`, they must lock this mutex. Under the hood, this uses an atomic Compare-And-Swap (CAS) instruction. If two workers try to pop simultaneously, one gets the lock, and the other is forced by the OS to wait. This prevents Double-Free and Memory Corruption bugs.

### 2. Condition Variables (Zero-CPU Sleeping)
If a Worker asks for a job and the heap is empty, what should it do? 
We *could* write:
```c
while (global_heap->size == 0) {
    // Just keep checking really fast! (Polling)
}
```
**The Problem:** This is called "Busy Waiting" or "Polling." It pins the CPU at 100% doing absolutely nothing. If you have 1,000 idle workers, your server will melt.

**The Solution:** We use a Condition Variable (`pthread_cond_t`).

```c
// Inside the Worker's POP handler
pthread_mutex_lock(&heap_mutex);
while (global_heap->size == 0) {
    // Tell the OS to put this thread to sleep! (0% CPU)
    pthread_cond_wait(&heap_cond, &heap_mutex);
}
Job* job = heap_pop(global_heap);
pthread_mutex_unlock(&heap_mutex);
```
When `cond_wait` is called, it automatically unlocks the mutex and puts the thread into a deep sleep. 

### 3. Spurious Wakeups (The `while` vs `if` Trap)
Look closely at the code above. We used a `while (size == 0)` loop, not an `if (size == 0)` statement. 
**Low-Level Concept:** The Linux OS (and POSIX standard) explicitly states that threads in `cond_wait` can wake up "spuriously" — meaning they can randomly wake up even if no job was pushed, simply due to CPU scheduler interrupts or POSIX signals. 
If we used an `if` statement, the thread would wake up, assume a job was there, call `heap_pop` on an empty heap, get `NULL`, and crash the broker. By using a `while` loop, the thread wakes up, immediately re-checks the heap size, realizes it was a false alarm, and goes back to sleep.

### 4. The Thundering Herd (`cond_signal` vs `cond_broadcast`)
When a Producer finally pushes a job, it needs to wake up a sleeping Worker.

```c
// Inside the Producer's PUSH handler
pthread_mutex_lock(&heap_mutex);
heap_push(global_heap, new_job);
pthread_cond_signal(&heap_cond); // Wake up EXACTLY ONE worker
pthread_mutex_unlock(&heap_mutex);
```

**Design Decision:** POSIX provides `cond_broadcast()` (wakes ALL sleepers) and `cond_signal()` (wakes exactly ONE sleeper). 
If we used `cond_broadcast`, pushing a single job would wake up 1,000 sleeping workers. All 1,000 would fight for the mutex, 1 would win and take the job, and the other 999 would get the lock, realize the heap is empty again, and go back to sleep. This is called the **Thundering Herd** problem and it destroys CPU performance. `cond_signal` is mathematically optimal here.

---

### Interview Concept Check 🧠

**Question:** Here is a critical bug we avoided in our implementation: In the Producer's code, we write the data to the physical disk (WAL) **BEFORE** we lock the `heap_mutex`. 

```c
// Correct way:
wal_append_push(new_job); // Disk I/O (fsync takes ~10ms)
pthread_mutex_lock(&heap_mutex);
heap_push(global_heap, new_job);
pthread_mutex_unlock(&heap_mutex);
```

What would happen to the rest of the system if we swapped the order, locked the `heap_mutex` first, and THEN called `wal_append_push` (which triggers a slow `fsync` disk write) while holding the lock?

**Your Answer:** other threads would unnecesaarliy wait for fsync to complete but it might jumble up the order of logs and jobs maybe pushed in a different order but it doesnt matter because the heap is built by prioroty and not pushing order.

**Correction/Validation:** That is an excellent answer, and your second point about the log ordering is incredibly astute! 

You are completely correct. Because we use `O_APPEND`, the kernel guarantees the log writes won't corrupt each other. The order they hit the log doesn't matter because the Min-Heap sorts them by priority on boot anyway. 

The fatal flaw is the **performance collapse**. If `fsync` takes 10 milliseconds, and you hold the global lock during it, your highly concurrent server is suddenly bottlenecked to a maximum throughput of exactly 100 jobs per second (1000ms / 10ms). By doing the I/O *outside* the lock, throughput remains in the tens of thousands of jobs per second. 

This brings us to an absolute golden rule of systems engineering: **Never perform network or disk I/O while holding a global lock.**

---

## Step 5: Distributed Fault Tolerance

What happens if a Worker successfully pops a job from the heap, starts processing the video file, and then their laptop catches on fire or their WiFi drops? If we just deleted the job from the heap on `POP`, that job would be permanently lost.

We must guarantee **At-Least-Once Delivery**. 

### 1. The "In-Flight" State Machine
Located at the top of `src/broker.c`:

```c
Job* in_flight[1024] = {NULL};
```
Instead of deleting a job from memory when a worker pops it, we move the pointer into this global array. We use the Worker's TCP Socket File Descriptor (`client_fd`) as the index! 
If worker on FD `4` pops a job, `in_flight[4] = job`.

### 2. Dead Socket Detection
Inside the `while(1)` loop of `client_handler`, we are constantly trying to `read()` from the worker's socket to see if they send an `ACK`.

```c
int bytes_read = read(client_fd, ...);
if (bytes_read <= 0) {
    // Dead socket detection or clean disconnect
    break; // Exit the loop!
}
```
**Low-Level Concept:** If `read()` returns `0`, it means the client gracefully closed the TCP connection. If it returns `-1` (with `errno == EPIPE`), it means the connection was abruptly broken. Either way, the loop breaks and we move to the cleanup phase.

### 3. Orphaned Job Re-queuing (The Magic Trick)
When the loop breaks, the thread executes this final block of code before exiting:

```c
// Dead socket detection cleanup
pthread_mutex_lock(&heap_mutex);
if (in_flight[client_fd] != NULL) {
    // We found a job that was popped but never ACKed!
    printf("Client %d disconnected with un-ACKed job. Re-queuing...\n", client_fd);
    
    // Push it right back into the Min-Heap
    heap_push(global_heap, in_flight[client_fd]);
    
    // Wake up a DIFFERENT worker to take it!
    pthread_cond_signal(&heap_cond); 
    
    in_flight[client_fd] = NULL;
}
pthread_mutex_unlock(&heap_mutex);
close(client_fd);
```

By tying the Job's lifecycle directly to the lifecycle of the TCP socket, we create an incredibly robust, self-healing system. If a worker dies, the OS notifies our broker via the socket, and the broker instantly re-queues the job.

---

### Interview Concept Check 🧠

**Question:** Because our queue is "At-Least-Once", the Worker application itself must be programmed to be "Idempotent." In software engineering, what does it mean for a worker's logic to be "idempotent", and why is it mandatory when consuming from a queue like TaskBroker, RabbitMQ, or AWS SQS?

**Your Answer:** the woker sshould be able to identify that the command it is receiving has been applied before and not reapply it again. it is mandatory it many the the worker processed the command and then the ack ges dropped in between the server thinks the worker died and resend the sae command to be processed again. if the worker isnt idempotent it may end up applying duplicate commands twice.

**Correction/Validation:** Absolutely perfect! You nailed the "Two Generals Problem." If the worker finishes processing and sends the `ACK`, but the router drops the packet, the broker will assume the worker died and re-queue the exact same job to someone else. Because of this, the worker's logic must be safe to run twice (e.g., executing a database `UPDATE` that sets a status flag vs a blind `INSERT` which would create duplicates).

---

## Step 6: Production Scaling & Weaknesses

When an interviewer looks at this C code, they aren't expecting it to run Google. They are looking to see if you can *identify your own architectural limits* and explain how you would upgrade them.

### 1. The ID Collision Bug
Currently, when a Producer pushes a job, we assign the ID like this:
```c
new_job->id = rand() % 10000;
```
**The Weakness:** Not only will this cause collisions after 10,000 jobs, but `rand()` uses a hidden global state in glibc, making it non-thread-safe!
**The Production Fix:** Use a lock-free atomic counter. 
```c
static atomic_int next_id = 0; 
new_job->id = atomic_fetch_add(&next_id, 1);
```
This guarantees unique, monotonically increasing IDs with zero locks.

### 2. The In-Flight Assignment Gap
Take a look at how we hand a job to a worker in the POP handler:
```c
pthread_mutex_lock(&heap_mutex);
Job* job = heap_pop(global_heap);
pthread_mutex_unlock(&heap_mutex);

if (job) {
    in_flight[client_fd] = job; 
    write(...);
}
```
**The Weakness:** The assignment `in_flight[client_fd] = job` happens *outside* the lock. If the worker thread gets killed by the OS in the 1-nanosecond gap between `unlock` and assigning the pointer, the job is popped from the heap, but never recorded in the `in_flight` array. It is permanently lost.
**The Production Fix:** Move the assignment `in_flight[client_fd] = job` *inside* the lock block. 

### 3. The Thread-per-Client Scaling Limit
Our server uses a Thread-per-Client architecture:
```c
while (1) {
    int client_fd = accept(...);
    pthread_create(&thread_id, ..., client_handler, client_fd);
}
```
This means if we have 10,000 workers connected, we have 10,000 active threads running inside our server process.

---

### Interview Concept Check 🧠

**Question:** The Thread-per-Client model is perfectly fine for our `in_flight[1024]` array. However, if we wanted to scale this broker to handle **100,000 simultaneous TCP connections**, the Thread-per-Client model would destroy the server's memory and CPU. Why specifically does having 100,000 threads crash or freeze a Linux server? And what advanced Linux Networking API would you use instead to handle 100,000 connections using just a single thread?

**Your Answer:** we can use epoll

**Correction/Validation:** Spot on with `epoll`! To elaborate on *why* the server crashes: First, every thread in Linux requires its own stack memory (usually 8MB by default). 100,000 threads would instantly try to allocate 800 Gigabytes of RAM just for thread stacks! Second, OS Context Switching: the Linux scheduler would waste 99% of its CPU cycles just rapidly switching between 100,000 threads trying to figure out which one has data. 

By using `epoll` and an Event Loop (like Nginx, Node.js, and Redis do), a single thread can monitor 100,000 non-blocking sockets simultaneously, only waking up to process the exact sockets that have bytes ready to read.

---

## Step 7: The External Clients (Producer & Worker)

The Broker handles 95% of the system's complexity. But to interact with the Broker, we use two separate TCP client programs. These clients are entirely unaware of the Min-Heap or WAL architecture; they just send bytes over a socket.

### 1. The Producer (`src/producer.c`)
The Producer connects to the Broker over TCP and blasts jobs into the system in a loop.

```c
for (int i = 0; i < num_jobs; i++) {
    int priority = rand() % 100; // Random priority 0-99
    char message[256];
    
    // Format: PUSH|<priority>|<command>\n
    snprintf(message, sizeof(message), "PUSH|%d|cmd_%d\n", priority, i);
    
    send(sock, message, strlen(message), 0); // Send the raw string
    printf("Sent: %s", message);
    
    usleep(100000); // Sleep for 100ms before sending the next one
}
```
**Key Concept:** The producer simply formats a string containing the delimiter `\n` and sends it via TCP. It acts as our firehose of incoming data.

### 2. The Worker / Consumer (`src/worker.c`)
The Worker connects to the broker, explicitly asks for work, simulates a heavy task, and confirms completion.

```c
for (int i = 0; i < num_jobs; i++) {
    // 1. Ask for a job
    const char *pop_msg = "POP\n";
    send(sock, pop_msg, strlen(pop_msg), 0);
    
    // 2. Wait for the broker to reply. 
    // If the broker's queue is empty, the broker puts its thread to sleep (cond_wait), 
    // so this read() call will safely block until the broker finally wakes up and replies.
    int bytes_read = read(sock, buffer, 1024);
    
    // 3. Simulate processing a video or heavy CPU task
    usleep(500000); // 500ms
    
    // 4. Send the ACK back to clear the job from the In-Flight array
    char* id_token = strtok_r(NULL, "|", &saveptr); // Extract job->id from broker's reply
    char ack_msg[256];
    snprintf(ack_msg, sizeof(ack_msg), "ACK|%s\n", id_token);
    send(sock, ack_msg, strlen(ack_msg), 0);
}
```
**Key Concept:** The Worker actively drives the state machine. By sleeping during execution, it creates artificial load, testing the broker's ability to keep track of the Job in the `in_flight` array until the `ACK` arrives.

---
**Study Session Complete.**
