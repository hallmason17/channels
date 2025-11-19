#include "../src/channels.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Test counter
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
  static void name(void);                                                      \
  static void run_##name(void) {                                               \
    printf("Running %s... ", #name);                                           \
    fflush(stdout);                                                            \
    name();                                                                    \
    tests_passed++;                                                            \
    printf("PASS\n");                                                          \
  }                                                                            \
  static void name(void)

#define ASSERT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("FAIL\n  %s:%d: %s\n", __FILE__, __LINE__, msg);                  \
      tests_failed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(a, b, msg)                                                   \
  do {                                                                         \
    if ((a) != (b)) {                                                          \
      printf("✗ FAIL\n  %s:%d: %s (expected %ld, got %ld)\n", __FILE__,        \
             __LINE__, msg, (long)(b), (long)(a));                             \
      tests_failed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

// =============================================================================
// Basic Functionality Tests
// =============================================================================

TEST(test_create_destroy) {
  channel_t *ch = channel_create(sizeof(int), 10);
  ASSERT(ch != NULL, "Channel creation failed");
  channel_destroy(ch);
}

TEST(test_send_recv_single_item) {
  channel_t *ch = channel_create(sizeof(int), 10);
  ASSERT(ch != NULL, "Channel creation failed");

  int send_val = 42;
  int recv_val = 0;

  ASSERT(channel_send(ch, &send_val), "Send failed");
  ASSERT(channel_recv(ch, &recv_val), "Receive failed");
  ASSERT_EQ(recv_val, 42, "Received wrong value");

  channel_destroy(ch);
}

TEST(test_send_recv_multiple_items) {
  channel_t *ch = channel_create(sizeof(int), 10);

  for (int i = 0; i < 10; i++) {
    ASSERT(channel_send(ch, &i), "Send failed");
  }

  for (int i = 0; i < 10; i++) {
    int val;
    ASSERT(channel_recv(ch, &val), "Receive failed");
    ASSERT_EQ(val, i, "Received wrong value");
  }

  channel_destroy(ch);
}

TEST(test_fifo_order) {
  channel_t *ch = channel_create(sizeof(int), 100);

  // Send 0-99
  for (int i = 0; i < 100; i++) {
    ASSERT(channel_send(ch, &i), "Send failed");
  }

  // Should receive in same order
  for (int i = 0; i < 100; i++) {
    int val;
    ASSERT(channel_recv(ch, &val), "Receive failed");
    ASSERT_EQ(val, i, "FIFO order violated");
  }

  channel_destroy(ch);
}

TEST(test_different_types) {
  // Test with different data types

  // Strings (pointers)
  channel_t *ch1 = channel_create(sizeof(char *), 10);
  char *str = "hello";
  char *recv_str;
  channel_send(ch1, &str);
  channel_recv(ch1, &recv_str);
  ASSERT(strcmp(recv_str, "hello") == 0, "String mismatch");
  channel_destroy(ch1);

  // Structs
  typedef struct {
    int x;
    int y;
  } point_t;
  channel_t *ch2 = channel_create(sizeof(point_t), 10);
  point_t p1 = {10, 20};
  point_t p2;
  channel_send(ch2, &p1);
  channel_recv(ch2, &p2);
  ASSERT_EQ(p2.x, 10, "Struct field mismatch");
  ASSERT_EQ(p2.y, 20, "Struct field mismatch");
  channel_destroy(ch2);
}

// =============================================================================
// Bounded Channel Tests
// =============================================================================

TEST(test_bounded_capacity) {
  channel_t *ch = channel_create(sizeof(int), 5);

  // Fill to capacity
  for (int i = 0; i < 5; i++) {
    ASSERT(channel_send(ch, &i), "Send failed");
  }

  // Next send would block (we can't easily test blocking in unit test)
  // Just verify we can drain and refill
  for (int i = 0; i < 5; i++) {
    int val;
    ASSERT(channel_recv(ch, &val), "Receive failed");
  }

  // Should be able to send again
  int val = 99;
  ASSERT(channel_send(ch, &val), "Send after drain failed");

  channel_destroy(ch);
}

TEST(test_bounded_wraparound) {
  channel_t *ch = channel_create(sizeof(int), 5);

  // Send and receive to force wraparound in ring buffer
  for (int round = 0; round < 3; round++) {
    for (int i = 0; i < 5; i++) {
      int val = round * 100 + i;
      ASSERT(channel_send(ch, &val), "Send failed");
    }

    for (int i = 0; i < 5; i++) {
      int val;
      ASSERT(channel_recv(ch, &val), "Receive failed");
      ASSERT_EQ(val, round * 100 + i, "Wrong value after wraparound");
    }
  }

  channel_destroy(ch);
}

// =============================================================================
// Unbounded Channel Tests
// =============================================================================

TEST(test_unbounded_growth) {
  channel_t *ch = channel_create(sizeof(int), 0); // Unbounded

  // Send many items (should trigger growth)
  for (int i = 0; i < 10000; i++) {
    ASSERT(channel_send(ch, &i), "Send failed during growth");
  }

  // Receive all
  for (int i = 0; i < 10000; i++) {
    int val;
    ASSERT(channel_recv(ch, &val), "Receive failed");
    ASSERT_EQ(val, i, "Wrong value after growth");
  }

  channel_destroy(ch);
}

// =============================================================================
// Close Semantics Tests
// =============================================================================

TEST(test_close_empty_channel) {
  channel_t *ch = channel_create(sizeof(int), 10);

  channel_close(ch);

  int val = 42;
  ASSERT(!channel_send(ch, &val), "Send to closed channel should fail");
  ASSERT(!channel_recv(ch, &val),
         "Receive from closed empty channel should fail");

  channel_destroy(ch);
}

TEST(test_close_with_data) {
  channel_t *ch = channel_create(sizeof(int), 10);

  // Send some data
  for (int i = 0; i < 5; i++) {
    channel_send(ch, &i);
  }

  channel_close(ch);

  // Should still be able to receive pending data
  for (int i = 0; i < 5; i++) {
    int val;
    ASSERT(channel_recv(ch, &val), "Should receive pending data after close");
    ASSERT_EQ(val, i, "Wrong value");
  }

  // But no more data available
  int val;
  ASSERT(!channel_recv(ch, &val), "Receive should fail when closed and empty");

  channel_destroy(ch);
}

TEST(test_send_after_close) {
  channel_t *ch = channel_create(sizeof(int), 10);
  channel_close(ch);

  int val = 42;
  ASSERT(!channel_send(ch, &val), "Send to closed channel should fail");

  channel_destroy(ch);
}

// =============================================================================
// Multi-threaded Tests
// =============================================================================

typedef struct {
  channel_t *ch;
  int start;
  int count;
} thread_args_t;

void *producer_thread(void *arg) {
  thread_args_t *args = (thread_args_t *)arg;

  for (int i = 0; i < args->count; i++) {
    int val = args->start + i;
    if (!channel_send(args->ch, &val)) {
      break; // Channel closed
    }
  }

  return NULL;
}

void *consumer_thread(void *arg) {
  thread_args_t *args = (thread_args_t *)arg;
  int received = 0;
  int val;

  while (received < args->count && channel_recv(args->ch, &val)) {
    received++;
  }

  int *result = malloc(sizeof(int));
  *result = received;
  return result;
}

TEST(test_single_producer_single_consumer) {
  channel_t *ch = channel_create(sizeof(int), 100);

  pthread_t prod, cons;
  thread_args_t prod_args = {ch, 0, 1000};
  thread_args_t cons_args = {ch, 0, 1000};

  pthread_create(&cons, NULL, consumer_thread, &cons_args);
  pthread_create(&prod, NULL, producer_thread, &prod_args);

  pthread_join(prod, NULL);
  channel_close(ch);

  int *received;
  pthread_join(cons, (void **)&received);

  ASSERT_EQ(*received, 1000, "Consumer didn't receive all messages");

  free(received);
  channel_destroy(ch);
}

TEST(test_multiple_producers_single_consumer) {
  channel_t *ch = channel_create(sizeof(int), 100);

  const int NUM_PRODUCERS = 3;
  const int ITEMS_PER_PRODUCER = 1000;

  pthread_t producers[NUM_PRODUCERS];
  pthread_t consumer;

  thread_args_t prod_args[NUM_PRODUCERS];
  for (int i = 0; i < NUM_PRODUCERS; i++) {
    prod_args[i].ch = ch;
    prod_args[i].start = i * 10000;
    prod_args[i].count = ITEMS_PER_PRODUCER;
    pthread_create(&producers[i], NULL, producer_thread, &prod_args[i]);
  }

  thread_args_t cons_args = {ch, 0, NUM_PRODUCERS * ITEMS_PER_PRODUCER};
  pthread_create(&consumer, NULL, consumer_thread, &cons_args);

  for (int i = 0; i < NUM_PRODUCERS; i++) {
    pthread_join(producers[i], NULL);
  }

  channel_close(ch);

  int *received;
  pthread_join(consumer, (void **)&received);

  ASSERT_EQ(*received, NUM_PRODUCERS * ITEMS_PER_PRODUCER,
            "Consumer didn't receive all messages");

  free(received);
  channel_destroy(ch);
}

TEST(test_concurrent_send_recv) {
  channel_t *ch = channel_create(sizeof(int), 10);

  pthread_t prod, cons;
  thread_args_t args = {ch, 0, 10000};

  // Start both simultaneously
  pthread_create(&cons, NULL, consumer_thread, &args);
  pthread_create(&prod, NULL, producer_thread, &args);

  pthread_join(prod, NULL);
  channel_close(ch);

  int *received;
  pthread_join(cons, (void **)&received);

  ASSERT_EQ(*received, 10000, "Concurrent send/recv failed");

  free(received);
  channel_destroy(ch);
}

// =============================================================================
// Stress Tests
// =============================================================================

TEST(test_high_volume) {
  channel_t *ch = channel_create(sizeof(int), 1000);

  const int VOLUME = 100000;

  pthread_t prod, cons;
  thread_args_t args = {ch, 0, VOLUME};

  pthread_create(&cons, NULL, consumer_thread, &args);
  pthread_create(&prod, NULL, producer_thread, &args);

  pthread_join(prod, NULL);
  channel_close(ch);

  int *received;
  pthread_join(cons, (void **)&received);

  ASSERT_EQ(*received, VOLUME, "High volume test failed");

  free(received);
  channel_destroy(ch);
}

TEST(test_many_producers) {
  channel_t *ch = channel_create(sizeof(int), 100);

  const int NUM_PRODUCERS = 10;
  const int ITEMS_PER = 1000;

  pthread_t producers[NUM_PRODUCERS];
  pthread_t consumer;

  thread_args_t prod_args[NUM_PRODUCERS];
  for (int i = 0; i < NUM_PRODUCERS; i++) {
    prod_args[i].ch = ch;
    prod_args[i].start = i * 100000;
    prod_args[i].count = ITEMS_PER;
    pthread_create(&producers[i], NULL, producer_thread, &prod_args[i]);
  }

  thread_args_t cons_args = {ch, 0, NUM_PRODUCERS * ITEMS_PER};
  pthread_create(&consumer, NULL, consumer_thread, &cons_args);

  for (int i = 0; i < NUM_PRODUCERS; i++) {
    pthread_join(producers[i], NULL);
  }

  channel_close(ch);

  int *received;
  pthread_join(consumer, (void **)&received);

  ASSERT_EQ(*received, NUM_PRODUCERS * ITEMS_PER, "Many producers test failed");

  free(received);
  channel_destroy(ch);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST(test_zero_capacity_unbounded) {
  channel_t *ch = channel_create(sizeof(int), 0);
  ASSERT(ch != NULL, "Unbounded channel creation failed");

  int val = 42;
  ASSERT(channel_send(ch, &val), "Send to unbounded channel failed");

  int recv;
  ASSERT(channel_recv(ch, &recv), "Receive from unbounded channel failed");
  ASSERT_EQ(recv, 42, "Wrong value");

  channel_destroy(ch);
}

TEST(test_large_items) {
  typedef struct {
    char data[1024];
  } large_item_t;

  channel_t *ch = channel_create(sizeof(large_item_t), 10);

  large_item_t item;
  memset(item.data, 'A', sizeof(item.data));
  item.data[1023] = '\0';

  ASSERT(channel_send(ch, &item), "Send large item failed");

  large_item_t recv;
  ASSERT(channel_recv(ch, &recv), "Receive large item failed");
  ASSERT(recv.data[0] == 'A' && recv.data[1022] == 'A', "Large item corrupted");

  channel_destroy(ch);
}

TEST(test_empty_channel_recv_fails) {
  channel_t *ch = channel_create(sizeof(int), 10);
  channel_close(ch);

  int val;
  ASSERT(!channel_recv(ch, &val),
         "Receive from empty closed channel should fail");

  channel_destroy(ch);
}

// =============================================================================
// Test Runner
// =============================================================================

int main(void) {
  printf("Running MPSC Channel Unit Tests\n");
  printf("================================\n\n");

  // Basic tests
  run_test_create_destroy();
  run_test_send_recv_single_item();
  run_test_send_recv_multiple_items();
  run_test_fifo_order();
  run_test_different_types();

  // Bounded tests
  run_test_bounded_capacity();
  run_test_bounded_wraparound();

  // Unbounded tests
  run_test_unbounded_growth();

  // Close semantics
  run_test_close_empty_channel();
  run_test_close_with_data();
  run_test_send_after_close();

  // Multi-threaded tests
  run_test_single_producer_single_consumer();
  run_test_multiple_producers_single_consumer();
  run_test_concurrent_send_recv();

  // Stress tests
  run_test_high_volume();
  run_test_many_producers();

  // Edge cases
  run_test_zero_capacity_unbounded();
  run_test_large_items();
  run_test_empty_channel_recv_fails();

  // Summary
  printf("\n================================\n");
  printf("Tests passed: %d\n", tests_passed);
  printf("Tests failed: %d\n", tests_failed);

  return tests_failed > 0 ? 1 : 0;
}
