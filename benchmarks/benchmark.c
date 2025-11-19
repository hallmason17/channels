#include "../src/channels.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct pthread_sender {
  channel_t *ch;
  int64_t val;
};

#define NUM_SENDS 10000000
void *ch_send(void *arg) {
  struct pthread_sender data = *(struct pthread_sender *)arg;
  for (int i = 0; i < NUM_SENDS; i++) {
    channel_send(data.ch, &data.val);
  }
  free(arg);
  return NULL;
}

#define NUM_THREADS 1
static atomic_int num_rcvs = 0;
int main(void) {
  channel_t *ch = channel_create(sizeof(int64_t), 10000);

  pthread_t threads[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; ++i) {
    struct pthread_sender *s = malloc(sizeof(struct pthread_sender));
    s->ch = ch;
    s->val = 1;
    pthread_create(&threads[i], NULL, ch_send, s);
  }
  clock_t start = clock();
  size_t msgs = NUM_THREADS * NUM_SENDS;
  char y;
  for (size_t i = 0; i < msgs; ++i) {
    channel_recv(ch, &y);
    atomic_fetch_add(&num_rcvs, 1);
  }
  clock_t end = clock();
  channel_close(ch);
  printf("%.2f ops/sec\n",
         (double)num_rcvs / ((double)(end - start) / CLOCKS_PER_SEC));
  for (int i = 0; i < NUM_THREADS; ++i) {
    pthread_join(threads[i], 0);
  }
  channel_destroy(ch);
}
