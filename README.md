# Thread-Safe Channel Synchronization Primitive in C

A lightweight implementation of Go-style channels for C11,
enabling safe concurrent communication through message passing rather than shared memory.

## Overview

This library brings Go's channel concurrency model to C, providing a type-safe,
thread-safe communication primitive for building concurrent applications.
Channels eliminate common pitfalls of traditional mutex-based synchronization
by enforcing the principle:
**"Don't communicate by sharing memory; share memory by communicating."**

### Key Features

- **Thread-safe message passing** between producer and consumer threads
- **Bounded and unbounded channels** for flexible buffering strategies
- **Blocking semantics** that simplify synchronization logic
- **Zero external dependencies** beyond C11 standard library and pthreads
- **Comprehensive test suite** ensuring correctness under concurrent load
- **Performance benchmarks** validating throughput characteristics

## Technical Implementation

### Architecture Decisions

**Synchronization Strategy**: Uses mutex and condition variable primitives to
implement blocking send/receive operations. While this creates some lock contention
under high concurrency (see benchmarks), it provides correctness guarantees and
portability across POSIX systems.

**Memory Management**: Bounded channels use a fixed-size circular buffer to
minimize allocations. Unbounded channels dynamically resize to prevent blocking
producers, trading memory for throughput.

## API Reference

```c
/* Handle to the channel */
typedef struct channel_t channel_t;

/**
 * @brief Creates a new channel that holds capacity items of size item_size.
 * Capacity of 0 indicates an unbounded channel that grows dynamically.
 *
 * @param item_size The size of the items the channel stores.
 * @param capacity Maximum number of items the channel can hold (0 for
 * unbounded).
 * @return A pointer to the initialized channel_t.
 */
channel_t *channel_create(size_t item_size, size_t capacity);

/**
 * @brief Sends a value into the channel.
 * Blocks if bounded channel is at capacity until space is available.
 *
 * @param ch The channel handle.
 * @param value A pointer to the data to send.
 * @return true on success, false otherwise (closed)
 */
bool channel_send(channel_t *ch, const void *value);

/**
 * @brief Receives a value from the channel.
 * Blocks until a value is available.
 *
 * @param ch The channel handle.
 * @param value Pointer to write received data.
 * @return true on success, false otherwise (closed and empty)
 */
bool channel_recv(channel_t *ch, void *value);

/**
 * @brief Closes the channel, preventing further sends.
 * Wakes all blocked threads to allow graceful shutdown.
 *
 * @param ch The channel handle.
 */
void channel_close(channel_t *ch);

/**
 * @brief Destroys the channel and frees all resources.
 *
 * @param ch The channel handle.
 */
void channel_destroy(channel_t *ch);
```

## Example: Producer-Consumer Pattern

```c
#include "channels.h"
#include <pthread.h>
#include <stdio.h>

void *producer(void *arg) {
    channel_t *ch = (channel_t *)arg;
    for (int i = 0; i < 10; i++) {
        channel_send(ch, &i);
    }
    channel_close(ch);
    return NULL;
}

int main(void) {
    channel_t *ch = channel_create(sizeof(int), 10);
    pthread_t thread;

    pthread_create(&thread, NULL, producer, ch);

    int val;
    while (channel_recv(ch, &val)) {
        printf("Received: %d\n", val);
    }

    pthread_join(thread, NULL);
    channel_destroy(ch);
    return 0;
}
```

## Building and Testing

```bash
# Link channels.c with your project
gcc -o myapp myapp.c channels.c -lpthread

# Run test suite
make test

# Run performance benchmarks
make benchmarks
```

## Performance Analysis

Benchmarks performed on Apple M3 (single producer/consumer unless noted).

### Multi-Producer Scaling

| Producers | Throughput (ops/sec) | Scaling Efficiency |
|-----------|---------------------:|-------------------:|
| 1         | 29.24M              | 100%               |
| 2         | 12.71M              | 43%                |
| 4         | 11.27M              | 39%                |
| 8         | 2.04M               | 7%                 |

**Analysis**: Lock contention degrades performance with multiple producers.
For high-concurrency scenarios, consider using separate channels per producer
or a lock-free implementation (future work).

### Bounded vs Unbounded Channels

| Configuration    | Throughput (ops/sec) |
|------------------|---------------------:|
| Bounded (10K)    | 36.70M              |
| Unbounded        | 45.90M              |

**Analysis**: Unbounded channels avoid blocking producers at the cost of memory growth.
In comparison, Go's channels achieve ~40M ops/sec (bounded) with superior multi-producer
scaling due to its runtime scheduler integration.

### Item Size Impact

| Size (bytes) | Throughput (ops/sec) | Bandwidth (MB/s) |
|-------------:|---------------------:|-----------------:|
| 4            | 34.07M              | 130              |
| 8            | 37.66M              | 287              |
| 64           | 37.40M              | 2,283            |
| 256          | 28.32M              | 6,914            |
| 1024         | 13.53M              | 13,217           |
| 4096         | 3.65M               | 14,260           |

**Analysis**: Performance remains stable up to 256-byte items.
Larger items incur `memcpy` overhead; consider passing pointers for large data structures.

## Design Rationale

### Why Channels Over Mutexes?

Traditional shared-memory concurrency requires careful mutex placement and
risks deadlocks. Channels make communication explicit: data flows through typed
channels rather than being scattered across shared state.
This aligns with the Actor model used in Erlang, Go, and distributed systems.

### Trade-offs

**Pros:**
- Eliminates many race conditions by design
- Simplifies reasoning about concurrent code
- Natural fit for pipeline and worker pool patterns

**Cons:**
- Lock-based implementation limits scalability beyond ~2 threads
- Higher overhead than raw atomic operations for simple cases
- Memory allocation for unbounded channels

## Future Enhancements

- **Select operation**: Wait on multiple channels simultaneously
- **Non-blocking try_send/try_recv**: For polling-based patterns
- **Lock-free implementation**: Using compare-and-swap for better scaling

## Inspiration

This project was inspired by Go's channel primitives and the CSP
(Communicating Sequential Processes) model. While Go integrates channels
with its runtime scheduler for optimal performance, this library demonstrates
that similar abstractions are viable in C with reasonable performance for many applications.

## License

MIT License
