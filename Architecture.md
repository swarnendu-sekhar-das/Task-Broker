# TaskBroker: Architecture, Functionality & Difficulty

If we want to elevate TaskBroker so that it doesn't just look like a "wrapper around a linked list," we introduce **Priority Queuing** and **Fault Tolerance**. 

Here is exactly how it functions, how it is built, and why the difficulty perfectly balances out.

---

## 1. Functionality (What does it actually do?)

Imagine you have a backend system that needs to process 10,000 video files. You don't want the web server to process them; you want background workers to do it.

1.  **The Producers**: A client connects via TCP and sends a job string with a priority level, e.g., `PRIORITY:1|CMD:process_video_123`.
2.  **The Broker (Your Server)**: 
    *   Receives the string.
    *   Saves it to a log file on disk (so it survives power outages).
    *   Places it into the queue.
3.  **The Workers**: Background clients connect via TCP and ask for a job.
    *   If the queue is empty, the worker doesn't disconnect; the server puts the worker to *sleep* until a job arrives.
    *   When a job arrives, the server wakes up one worker and gives it the highest-priority job.
4.  **The Fault-Tolerant ACK**: 
    *   The worker processes the video and sends back an `ACK` string.
    *   *The Magic Feature*: If the worker's laptop catches on fire or their Wi-Fi drops before sending the `ACK`, your server detects the dead TCP socket and automatically puts the job *back* into the queue for another worker to pick up.

---

## 2. Architecture (How is it built?)

To make this a master's level project, we use specific CS architectures to solve these problems.

### A. The Min-Heap (Instead of a Linked List)
If you just use a linked list, inserting a high-priority job takes `O(N)` time because you have to scan the list.
*   **The Architecture**: We use a **Min-Heap** (an array-based binary tree). When a Priority 1 job arrives, it "bubbles up" to the root of the tree in `O(log N)` time. 
*   **The Interview Value**: Interviewers love Heaps. It proves you can implement a core data structure from scratch and integrate it into a concurrent system.

### B. Condition Variables (Instead of Polling)
If a worker asks for a job and the queue is empty, you *could* just write a `while(1)` loop that checks the queue every second. That is terrible for CPU usage.
*   **The Architecture**: We use a `pthread_cond_t` (Condition Variable). The worker thread goes into a deep OS-level sleep (0% CPU usage). When a producer pushes a job to the Heap, the producer calls `pthread_cond_signal`, instantly waking up exactly one sleeping worker.
*   **The Interview Value**: Mastering Condition Variables (and understanding "spurious wakeups") is the hallmark of a senior-level systems engineer.

### C. The "In-Flight" State Machine
When a worker takes a job, we don't delete it from memory. 
*   **The Architecture**: We move the job from the Min-Heap into an "In-Flight" hash map or array, mapped to that worker's Socket File Descriptor (FD). If `read(socket)` returns `0` or `EPIPE`, we know the worker died. We pull the job out of the In-Flight state and push it back into the Min-Heap.

---

## 3. Difficulty Comparison: TaskBroker vs. VeloceStore

**TaskBroker (Elevated) Difficulty: 7.0 / 10**
**VeloceStore Difficulty: 7.5 / 10**

By adding Priority Heaps and Fault-Tolerant ACKs, we bring the difficulty of TaskBroker almost perfectly in line with VeloceStore, but the *type* of difficulty is much more manageable.

### Why it is easier than VeloceStore:
1.  **No Hash Collisions**: You don't have to worry about separate chaining, load factors, or dynamically resizing arrays.
2.  **No Memory Allocators**: You don't have to write a custom Slab Allocator using `mmap`. You can safely use standard `malloc` and `free` because job strings are cleanly allocated and freed once ACKed.
3.  **Narrower Scope**: VeloceStore forces you to study four different domains. TaskBroker forces you to study exactly one domain very deeply: **Concurrency & Synchronization**.

### The Grill Topics (What you must defend)
Instead of being grilled on OS memory pages, the interviewer will attack your concurrency logic:
*   *"What happens if two workers wake up at the exact same time? How do you guarantee they don't pop the exact same job from the Heap?"* (Defense: Mutex locking around the Heap pop).
*   *"How does the Wait-Ahead Log (WAL) avoid slowing down the Producers?"* (Defense: Group Commit or background fsyncing).

**Summary**: This elevated version of TaskBroker is the perfect "Goldilocks" project. It completely removes the complex, low-level memory/hashing bugs of VeloceStore, replacing them with Distributed Systems architecture (ACKs and Heaps) which are much easier to whiteboard and explain!
