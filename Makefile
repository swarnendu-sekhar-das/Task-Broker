CC = gcc
CFLAGS = -Wall -Wextra -pthread -g
LDFLAGS = -pthread

# Uncomment for ThreadSanitizer:
# CFLAGS += -fsanitize=thread
# LDFLAGS += -fsanitize=thread

all: test_heap

test_heap: tests/test_heap.c src/heap.c
	$(CC) $(CFLAGS) -Isrc $^ -o $@ $(LDFLAGS)

clean:
	rm -f test_heap broker worker producer
