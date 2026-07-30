# File Systems & Persistence: Deep Dive & Interview Grill Guide

This document explores how TaskBroker survives power outages and server crashes. It covers the OS Page Cache, `fsync`, Atomic Appends, and the Write-Ahead Log (WAL) architecture.

## 1. The Write-Ahead Log (WAL)

If TaskBroker stored all jobs purely in the `MinHeap` in RAM, a server crash would permanently delete millions of unprocessed jobs. 
To achieve **Durability** (the 'D' in ACID for databases), we use a Write-Ahead Log.

**The Concept:** Before we ever push a job into the RAM `MinHeap`, we must *first* write a record of it to a text file on the hard drive (`wal.txt`). If a job is pulled and completed by a worker, we write an `ACK` to the file.

If the server loses power, the RAM is wiped. But when we reboot, the broker simply reads `wal.txt` from top to bottom, recreating the exact state of the heap by replaying the `PUSH` and `ACK` commands.

---

## 2. The `write()` System Call is a Lie

When a Producer sends a job, our `wal_append_push` function executes:
```c
write(wal_fd, buffer, len);
```

> [!WARNING]
> ### The Interview Grill: The OS Page Cache
> 
> **Interviewer:** *"Does your `write()` system call actually write data to the SSD? If the server's power cable is ripped out 1 millisecond after `write()` returns success, is your data safe?"*
> 
> **Defense:** No! In Linux, `write()` **does not touch the hard drive**. 
> It simply copies the data from my program's buffer into the Linux Kernel's **Page Cache** (which lives in RAM). The kernel marks those pages in RAM as "Dirty". 
> Later, in the background, a kernel thread (like `pdflush`) will wake up and actually sync those dirty pages down to the physical SSD. 
> If the server loses power 1 millisecond after `write()` succeeds, the Page Cache is destroyed, and the data is permanently lost.

---

## 3. Forcing Durability (`fsync`)

To solve the Page Cache vulnerability, TaskBroker must force the kernel to flush the data immediately.

```c
if (write(wal_fd, buffer, len) > 0) {
    fsync(wal_fd); // Ensure durability
}
```

> [!IMPORTANT]
> ### The Interview Grill: `fsync` vs. `fdatasync`
> 
> **Interviewer:** *"You used `fsync()`. Why is that terrible for performance, and what is the alternative?"*
> 
> **Defense:** `fsync(fd)` blocks my thread and forces the SSD to physically spin up (or flash the NAND cells) to save the data. However, `fsync` actually performs **two** physical disk writes:
> 1. It writes the actual file data.
> 2. It writes the file's **Metadata** (updating the "Last Modified" timestamp in the inode).
> 
> Updating the timestamp is usually completely useless for a WAL, but it forces the hard drive to do twice the work! 
> **The Fix:** I should use **`fdatasync(fd)`**. This system call flushes the file data to the disk, but *skips* updating the metadata timestamp, cutting the disk I/O latency in half.
> 
> *(Note: The absolute fastest method is bypassing the Page Cache entirely using `O_DIRECT`, which is what extreme databases like ScyllaDB use, but that requires highly complex memory-aligned buffers).*

---

## 4. Atomic Appends (`O_APPEND`)

In `wal_init`, we open the file with a very specific flag:
```c
wal_fd = open(filepath, O_WRONLY | O_APPEND | O_CREAT, 0644);
```

> [!TIP]
> ### The Interview Grill: Thread Safety without Mutexes
> 
> **Interviewer:** *"If 10 different threads call `write()` to the WAL file at the exact same nanosecond, and you aren't using a Mutex to protect the file, won't the strings overwrite each other and become corrupted?"*
> 
> **Defense:** No, because I used the **`O_APPEND`** flag. 
> In POSIX, `O_APPEND` guarantees that before every single `write()` operation, the kernel will automatically reposition the file offset pointer to the absolute end of the file. 
> The Linux Kernel guarantees that `O_APPEND` writes are **Atomic**. Even if 10 threads try to write simultaneously, the kernel serializes them, ensuring every string is perfectly appended one after the other without any userspace Mutex locking required!

---

## 5. The Recovery Process

When `broker.c` boots up, it calls `wal_recover(global_heap)`.

It reads `wal.txt` line by line.
*   If it sees `PUSH|105|1|cat /etc/passwd`, it allocates a `Job` in RAM and puts it in a temporary array `active_jobs[105]`.
*   If it sees `ACK|105`, it knows the job was completed right before the crash. It immediately `free()`s `active_jobs[105]`.

When it reaches the end of the file, whatever is left inside the `active_jobs` array are the jobs that were in the queue when the server lost power! It pushes them all back into the Min-Heap, and the broker resumes exactly where it left off.

---

## 6. The Ultimate Campus Interview Question: Thread-Safe Logger

In storage systems interviews (Snowflake, Databricks), they will ask you to implement a high-performance, thread-safe logging mechanism. 
The trick is to see if you try to use heavy `pthread_mutex_t` locks, or if you know about the magic of the `O_APPEND` flag for atomic kernel-level serialization.

### The Solution

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

typedef struct {
    int fd;
} Logger;

// The interviewer wants to see O_APPEND here!
Logger* logger_init(const char* filepath) {
    Logger* logger = malloc(sizeof(Logger));
    
    // O_APPEND guarantees atomic writes even with 100 threads!
    logger->fd = open(filepath, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (logger->fd < 0) {
        free(logger);
        return NULL;
    }
    return logger;
}

// No Mutexes needed!
void logger_log(Logger* logger, const char* message) {
    if (logger == NULL || logger->fd < 0) return;
    
    // write() with O_APPEND is automatically serialized by the Linux Kernel
    if (write(logger->fd, message, strlen(message)) < 0) {
        perror("Write failed");
    }
}

// The interviewer might ask: "How do you guarantee it survives a power cut?"
void logger_force_flush(Logger* logger) {
    // fdatasync flushes data to disk but skips metadata (faster than fsync)
    fdatasync(logger->fd);
}
```
