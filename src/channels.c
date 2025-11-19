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
  pthread_cond_t send_cond;
  pthread_cond_t recv_cond;
  pthread_mutex_t mu;
} channel_t;

channel_t *channel_create(size_t item_size, size_t capacity) {
  channel_t *ch = malloc(sizeof(channel_t));

  ch->item_size = item_size;
  ch->capacity = capacity;
  ch->flags = (capacity > 0) ? CH_BOUNDED : 0;
  ch->count = 0;
  ch->recv_ptr = 0;
  ch->send_ptr = 0;

  pthread_mutex_init(&ch->mu, NULL);
  pthread_cond_init(&ch->recv_cond, NULL);
  pthread_cond_init(&ch->send_cond, NULL);

  if (capacity == 0) {
    ch->capacity = 1 << 12;
  }

  ch->queue = calloc(ch->capacity, item_size);

  if (!ch->queue) {
    free(ch);
    return NULL;
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
    while (ch->count >= ch->capacity && !(ch->flags & CH_CLOSED)) {
      pthread_cond_wait(&ch->send_cond, &ch->mu);
    }
    if (ch->flags & CH_CLOSED) {
      pthread_mutex_unlock(&ch->mu);
      return false;
    }
  } else if (ch->capacity <= ch->count) {
    size_t new_cap = ch->capacity * 2;
    void *new_queue = malloc(new_cap * ch->item_size);
    if (new_queue == NULL) {
      pthread_mutex_unlock(&ch->mu);
      return false;
    }
    if (ch->recv_ptr < ch->send_ptr) {
      memcpy(new_queue, (char *)ch->queue + ch->recv_ptr * ch->item_size,
             ch->count * ch->item_size);
    } else {
      size_t start = ch->capacity - ch->recv_ptr;
      memcpy(new_queue, (char *)ch->queue + ch->recv_ptr * ch->item_size,
             start * ch->item_size);
      memcpy((char *)new_queue + start * ch->item_size, ch->queue,
             ch->send_ptr * ch->item_size);
    }

    free(ch->queue);
    ch->queue = new_queue;
    ch->capacity = new_cap;
    ch->recv_ptr = 0;
    ch->send_ptr = ch->count;
  }
  void *slot = (char *)ch->queue + (ch->item_size * ch->send_ptr);
  memcpy(slot, value, ch->item_size);
  ch->count++;
  ch->send_ptr = (ch->send_ptr + 1) % ch->capacity;
  pthread_cond_signal(&ch->recv_cond);
  pthread_mutex_unlock(&ch->mu);
  return true;
}
bool channel_recv(channel_t *ch, void *value) {
  pthread_mutex_lock(&ch->mu);
  while (ch->count == 0 && !(ch->flags & CH_CLOSED)) {
    pthread_cond_wait(&ch->recv_cond, &ch->mu);
  }
  if (ch->count == 0 && (ch->flags & CH_CLOSED)) {
    pthread_mutex_unlock(&ch->mu);
    return false;
  }
  void *slot = (char *)ch->queue + (ch->item_size * ch->recv_ptr);
  memcpy(value, slot, ch->item_size);
  ch->count--;
  ch->recv_ptr = (ch->recv_ptr + 1) % ch->capacity;

  pthread_cond_signal(&ch->send_cond);
  pthread_mutex_unlock(&ch->mu);
  return true;
}
void channel_close(channel_t *ch) {
  pthread_mutex_lock(&ch->mu);
  ch->flags |= CH_CLOSED;
  pthread_cond_broadcast(&ch->send_cond);
  pthread_cond_broadcast(&ch->recv_cond);
  pthread_mutex_unlock(&ch->mu);
}
void channel_destroy(channel_t *ch) {
  pthread_cond_destroy(&ch->send_cond);
  pthread_cond_destroy(&ch->recv_cond);
  pthread_mutex_destroy(&ch->mu);
  free(ch->queue);
  free(ch);
}
