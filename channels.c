#include "channels.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CH_CLOSED 1 << 0
#define CH_BOUNDED 1 << 1
typedef struct channel_t {
  size_t item_size;
  size_t count;
  size_t capacity;
  size_t recv_ptr;
  size_t send_ptr;
  void *queue;
  uint8_t flags;
  pthread_cond_t cond;
  pthread_mutex_t mu;
} channel_t;

channel_t *channel_create(size_t item_size, size_t capacity) {
  channel_t *ch = malloc(sizeof(channel_t));
  ch->item_size = item_size;
  ch->capacity = capacity;
  ch->flags = 0;
  ch->count = 0;
  ch->recv_ptr = 0;
  ch->send_ptr = 0;
  pthread_mutex_init(&ch->mu, NULL);
  pthread_cond_init(&ch->cond, NULL);
  if (capacity > 0) {
    ch->flags |= CH_BOUNDED;
    ch->queue = calloc(capacity, item_size);
    if (!ch->queue) {
      free(ch);
      perror("malloc");
      exit(1);
    }
  } else {
    ch->capacity = 1 << 4;
    ch->queue = calloc(ch->capacity, item_size);
    if (!ch->queue) {
      free(ch);
      perror("malloc");
      exit(1);
    }
  }
  return ch;
}
bool channel_send(channel_t *ch, const void *value) {
  pthread_mutex_lock(&ch->mu);
  if (ch->flags & CH_CLOSED) {
    pthread_mutex_unlock(&ch->mu);
    return false;
  }
  if (ch->flags & CH_BOUNDED) {
    while (ch->count >= ch->capacity) {
      pthread_cond_wait(&ch->cond, &ch->mu);
    }
  }
  if (!(ch->flags & CH_BOUNDED)) {
    if (ch->capacity <= ch->count) {
      ch->capacity *= 2;
      size_t new_size = ch->capacity * ch->item_size;
      void *new_queue = realloc(ch->queue, new_size);
      if (new_queue == NULL) {
        perror("realloc");
        exit(1);
      }
      ch->queue = new_queue;
    }
  }
  void *slot = (char *)ch->queue + (ch->item_size * ch->send_ptr);
  memcpy(slot, value, ch->item_size);
  ch->count++;
  ch->send_ptr = (ch->send_ptr + 1) % ch->capacity;

  pthread_cond_signal(&ch->cond);
  pthread_mutex_unlock(&ch->mu);
  return true;
}
bool channel_recv(channel_t *ch, void *value) {
  pthread_mutex_lock(&ch->mu);
  if (ch->flags & CH_CLOSED) {
    pthread_mutex_unlock(&ch->mu);
    return false;
  }
  while (ch->count == 0) {
    pthread_cond_wait(&ch->cond, &ch->mu);
  }
  void *slot = (char *)ch->queue + (ch->item_size * ch->recv_ptr);
  memcpy(value, slot, ch->item_size);
  ch->count--;
  ch->recv_ptr = (ch->recv_ptr + 1) % ch->capacity;

  pthread_cond_broadcast(&ch->cond);
  pthread_mutex_unlock(&ch->mu);
  return true;
}
void channel_close(channel_t *ch) { ch->flags &= CH_CLOSED; }
void channel_destroy(channel_t *ch) {
  pthread_cond_destroy(&ch->cond);
  pthread_mutex_destroy(&ch->mu);
  free(ch->queue);
  free(ch);
}

struct pthread_sender {
  channel_t *ch;
  int val;
};

#define NUM_SENDS 100000000
void *ch_send(void *arg) {
  struct pthread_sender data = *(struct pthread_sender *)arg;
  for (int i = 0; i < NUM_SENDS; i++) {
    channel_send(data.ch, &i);
  }
  return NULL;
}

static atomic_int num_rcvs = 0;
int main(void) {
  channel_t *ch = channel_create(sizeof(int), 0);
  int x = 10;
  int y;
  pthread_t t;
  struct pthread_sender s = (struct pthread_sender){.ch = ch, .val = x};
  pthread_create(&t, NULL, ch_send, &s);
  clock_t start = clock();
  while (channel_recv(ch, &y)) {
    int x = atomic_fetch_add(&num_rcvs, 1);
    if (x == NUM_SENDS - 1) {
      break;
    }
  }
  clock_t end = clock();
  printf("%.2f ops/sec\n",
         (double)num_rcvs / ((double)(end - start) / CLOCKS_PER_SEC));
  pthread_join(t, 0);
}
