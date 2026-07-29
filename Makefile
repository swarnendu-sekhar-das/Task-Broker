CC = gcc
CFLAGS = -Wall -Wextra -pthread -g
LDFLAGS = -pthread

# Uncomment for ThreadSanitizer:
# CFLAGS += -fsanitize=thread
# LDFLAGS += -fsanitize=thread

all: test_heap broker producer worker

test_heap: tests/test_heap.c src/heap.c
	$(CC) $(CFLAGS) -Isrc $^ -o $@ $(LDFLAGS)

broker: src/broker.c src/heap.c src/wal.c
	$(CC) $(CFLAGS) -Isrc $^ -o $@ $(LDFLAGS)

producer: src/producer.c
	$(CC) $(CFLAGS) -Isrc $^ -o $@

worker: src/worker.c
	$(CC) $(CFLAGS) -Isrc $^ -o $@

clean:
	rm -f test_heap broker producer worker
