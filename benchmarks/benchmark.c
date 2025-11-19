#include "../src/channels.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// High-resolution timing
static inline uint64_t get_nanos(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// Thread argument structure
typedef struct {
  channel_t *ch;
  size_t count;
  int id;
} bench_args_t;

// Generic producer
void *producer_func(void *arg) {
  bench_args_t *args = (bench_args_t *)arg;
  int64_t val = args->id;

  for (size_t i = 0; i < args->count; i++) {
    channel_send(args->ch, &val);
  }
  return NULL;
}

// Generic consumer
void *consumer_func(void *arg) {
  bench_args_t *args = (bench_args_t *)arg;
  int64_t val;

  for (size_t i = 0; i < args->count; i++) {
    channel_recv(args->ch, &val);
  }
  return NULL;
}

// =============================================================================
// Benchmark 1: Throughput vs Number of Producers
// =============================================================================
void bench_scaling_producers(void) {
  printf("\n======== Benchmark: Scaling Producers ========\n");
  printf("%-12s | %-18s | %-12s\n", "Producers", "Throughput", "Speedup");
  printf("-------------|--------------------|----------\n");

  const size_t ITEMS_PER_PRODUCER = 5000000;
  const size_t CAPACITY = 10000;
  double baseline = 0;

  for (int num_prod = 1; num_prod <= 8; num_prod *= 2) {
    channel_t *ch = channel_create(sizeof(int64_t), CAPACITY);

    pthread_t *producers = malloc(num_prod * sizeof(pthread_t));
    pthread_t consumer;

    bench_args_t *prod_args = malloc(num_prod * sizeof(bench_args_t));
    for (int i = 0; i < num_prod; i++) {
      prod_args[i].ch = ch;
      prod_args[i].count = ITEMS_PER_PRODUCER;
      prod_args[i].id = i;
    }

    bench_args_t cons_args = {
        .ch = ch, .count = ITEMS_PER_PRODUCER * num_prod, .id = 0};

    uint64_t start = get_nanos();

    pthread_create(&consumer, NULL, consumer_func, &cons_args);
    for (int i = 0; i < num_prod; i++) {
      pthread_create(&producers[i], NULL, producer_func, &prod_args[i]);
    }

    for (int i = 0; i < num_prod; i++) {
      pthread_join(producers[i], NULL);
    }
    channel_close(ch);
    pthread_join(consumer, NULL);

    uint64_t elapsed = get_nanos() - start;
    double ops_per_sec =
        (double)(ITEMS_PER_PRODUCER * num_prod) / (elapsed / 1e9);

    if (num_prod == 1)
      baseline = ops_per_sec;
    double speedup = ops_per_sec / baseline;

    printf("%-12d | %10.2f mil/sec | %.2fx\n", num_prod, ops_per_sec / 1e6,
           speedup);

    free(producers);
    free(prod_args);
    channel_destroy(ch);
  }
}

// =============================================================================
// Benchmark 2: Bounded vs Unbounded
// =============================================================================
void bench_bounded_vs_unbounded(void) {
  printf("\n======== Benchmark: Bounded vs Unbounded ========\n");
  printf("%-20s | %-15s\n", "Mode", "Throughput");
  printf("---------------------|-------------------\n");

  const size_t NUM_ITEMS = 100000000;
  const int NUM_PRODUCERS = 3;

  // Bounded
  {
    channel_t *ch = channel_create(sizeof(int64_t), 10000);

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumer;

    bench_args_t prod_args[NUM_PRODUCERS];
    for (int i = 0; i < NUM_PRODUCERS; i++) {
      prod_args[i].ch = ch;
      prod_args[i].count = NUM_ITEMS / NUM_PRODUCERS;
      prod_args[i].id = i;
    }

    bench_args_t cons_args = {ch, NUM_ITEMS, 0};

    uint64_t start = get_nanos();

    pthread_create(&consumer, NULL, consumer_func, &cons_args);
    for (int i = 0; i < NUM_PRODUCERS; i++) {
      pthread_create(&producers[i], NULL, producer_func, &prod_args[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
      pthread_join(producers[i], NULL);
    }
    channel_close(ch);
    pthread_join(consumer, NULL);

    uint64_t elapsed = get_nanos() - start;
    double ops_per_sec = (double)NUM_ITEMS / (elapsed / 1e9);

    printf("%-20s | %10.2f mil/sec\n", "Bounded (10000)", ops_per_sec / 1e6);

    channel_destroy(ch);
  }

  // Unbounded
  {
    channel_t *ch = channel_create(sizeof(int64_t), 0);

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumer;

    bench_args_t prod_args[NUM_PRODUCERS];
    for (int i = 0; i < NUM_PRODUCERS; i++) {
      prod_args[i].ch = ch;
      prod_args[i].count = NUM_ITEMS / NUM_PRODUCERS;
      prod_args[i].id = i;
    }

    bench_args_t cons_args = {ch, NUM_ITEMS, 0};

    uint64_t start = get_nanos();

    pthread_create(&consumer, NULL, consumer_func, &cons_args);
    for (int i = 0; i < NUM_PRODUCERS; i++) {
      pthread_create(&producers[i], NULL, producer_func, &prod_args[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
      pthread_join(producers[i], NULL);
    }
    channel_close(ch);
    pthread_join(consumer, NULL);

    uint64_t elapsed = get_nanos() - start;
    double ops_per_sec = (double)NUM_ITEMS / (elapsed / 1e9);

    printf("%-20s | %10.2f mil/sec\n", "Unbounded", ops_per_sec / 1e6);

    channel_destroy(ch);
  }
}

// =============================================================================
// Helper for item size benchmarks
// =============================================================================
static size_t g_item_size = 0;

void *sized_producer(void *arg) {
  bench_args_t *a = (bench_args_t *)arg;
  void *buf = malloc(g_item_size);
  memset(buf, 0xAB, g_item_size);
  for (size_t i = 0; i < a->count; i++) {
    channel_send(a->ch, buf);
  }
  free(buf);
  return NULL;
}

void *sized_consumer(void *arg) {
  bench_args_t *a = (bench_args_t *)arg;
  void *buf = malloc(g_item_size);
  for (size_t i = 0; i < a->count; i++) {
    channel_recv(a->ch, buf);
  }
  free(buf);
  return NULL;
}

// =============================================================================
// Benchmark 3: Different Item Sizes
// =============================================================================
void bench_item_sizes(void) {
  printf("\n======== Benchmark: Item Size Impact ==============\n");
  printf("%-15s | %-18s | %-15s\n", "Item Size", "Throughput", "Bandwidth");
  printf("----------------|--------------------|----------------\n");

  const size_t NUM_ITEMS = 50000000;
  const int NUM_PRODUCERS = 3;
  const size_t CAPACITY = 10000;

  size_t sizes[] = {4, 8, 64, 256, 1024, 4096};

  for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
    g_item_size = sizes[s];
    channel_t *ch = channel_create(g_item_size, CAPACITY);

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumer;

    bench_args_t prod_args[NUM_PRODUCERS];
    for (int i = 0; i < NUM_PRODUCERS; i++) {
      prod_args[i].ch = ch;
      prod_args[i].count = NUM_ITEMS / NUM_PRODUCERS;
      prod_args[i].id = i;
    }

    bench_args_t cons_args = {ch, NUM_ITEMS, 0};

    uint64_t start = get_nanos();

    pthread_create(&consumer, NULL, sized_consumer, &cons_args);
    for (int i = 0; i < NUM_PRODUCERS; i++) {
      pthread_create(&producers[i], NULL, sized_producer, &prod_args[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
      pthread_join(producers[i], NULL);
    }
    channel_close(ch);
    pthread_join(consumer, NULL);

    uint64_t elapsed = get_nanos() - start;
    double ops_per_sec = (double)NUM_ITEMS / (elapsed / 1e9);
    double bandwidth_mbps = (ops_per_sec * g_item_size) / (1024.0 * 1024.0);

    printf("%-15zu | %10.2f mil/sec | %10.2f MB/s\n", g_item_size,
           ops_per_sec / 1e6, bandwidth_mbps);

    channel_destroy(ch);
  }
}

// =============================================================================
// Benchmark 4: Capacity Impact on Bounded Channels
// =============================================================================
void bench_capacity_impact(void) {
  printf("\n======== Benchmark: Bounded Capacity Impact ========\n");
  printf("%-15s | %-15s\n", "Capacity", "Throughput");
  printf("----------------|--------------------\n");

  const size_t NUM_ITEMS = 10000000;
  const int NUM_PRODUCERS = 3;

  size_t capacities[] = {10, 100, 1000, 10000, 100000};

  for (size_t c = 0; c < sizeof(capacities) / sizeof(capacities[0]); c++) {
    size_t capacity = capacities[c];
    channel_t *ch = channel_create(sizeof(int64_t), capacity);

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumer;

    bench_args_t prod_args[NUM_PRODUCERS];
    for (int i = 0; i < NUM_PRODUCERS; i++) {
      prod_args[i].ch = ch;
      prod_args[i].count = NUM_ITEMS / NUM_PRODUCERS;
      prod_args[i].id = i;
    }

    bench_args_t cons_args = {ch, NUM_ITEMS, 0};

    uint64_t start = get_nanos();

    pthread_create(&consumer, NULL, consumer_func, &cons_args);
    for (int i = 0; i < NUM_PRODUCERS; i++) {
      pthread_create(&producers[i], NULL, producer_func, &prod_args[i]);
    }

    for (int i = 0; i < NUM_PRODUCERS; i++) {
      pthread_join(producers[i], NULL);
    }
    channel_close(ch);
    pthread_join(consumer, NULL);

    uint64_t elapsed = get_nanos() - start;
    double ops_per_sec = (double)NUM_ITEMS / (elapsed / 1e9);

    printf("%-15zu | %10.2f mil/sec\n", capacity, ops_per_sec / 1e6);

    channel_destroy(ch);
  }
}

// =============================================================================
// Helper for latency benchmark
// =============================================================================
typedef struct {
  channel_t *ch1;
  channel_t *ch2;
  size_t iterations;
} pong_args_t;

void *pong_thread(void *arg) {
  pong_args_t *args = (pong_args_t *)arg;
  int64_t val;
  for (size_t i = 0; i < args->iterations; i++) {
    channel_recv(args->ch1, &val);
    channel_send(args->ch2, &val);
  }
  return NULL;
}

// =============================================================================
// Benchmark 5: Latency (Ping-Pong)
// =============================================================================
void bench_latency(void) {
  printf("\n======== Benchmark: Latency (Ping-Pong) ===========\n");

  const size_t NUM_ITERATIONS = 1000000;

  channel_t *ch1 = channel_create(sizeof(int64_t), 1);
  channel_t *ch2 = channel_create(sizeof(int64_t), 1);

  pthread_t thread;
  pong_args_t args = {ch1, ch2, NUM_ITERATIONS};

  pthread_create(&thread, NULL, pong_thread, &args);

  uint64_t start = get_nanos();
  int64_t val = 0;

  for (size_t i = 0; i < NUM_ITERATIONS; i++) {
    channel_send(ch1, &val);
    channel_recv(ch2, &val);
  }

  uint64_t elapsed = get_nanos() - start;
  double avg_latency_ns = (double)elapsed / (NUM_ITERATIONS * 2);

  pthread_join(thread, NULL);

  printf("Average latency: %.2f ns per operation\n", avg_latency_ns);
  printf("Round-trip time: %.2f ns\n", avg_latency_ns * 2);

  channel_destroy(ch1);
  channel_destroy(ch2);
}

int main(void) {
  bench_scaling_producers();
  bench_bounded_vs_unbounded();
  bench_item_sizes();
  bench_capacity_impact();
  bench_latency();

  printf("\n=================================\n");
  printf("Benchmarks complete!\n");

  return 0;
}
