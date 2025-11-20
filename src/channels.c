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

/* The main channel type */
typedef struct channel_t {
  /* The size of items in the channel */
  size_t item_size;

  /* The number unread items in the channel */
  size_t count;

  /* The number of unread items that can be in the channel at a time */
  size_t capacity;

  /* Pointer to the next slot for the receiver to take data */
  size_t recv_ptr;

  /* Pointer to the next slot for senders to put data */
  size_t send_ptr;

  /* Condition variable to wake sleeping producer threads */
  pthread_cond_t send_cond;

  /* Condition variable to wake a sleeping consumer thread */
  pthread_cond_t recv_cond;

  /* Mutex for the queue and condition variables */
  pthread_mutex_t mu;

  /* Flags for state management, bounded or unbounded, open or closed */
  uint8_t flags;

  /* The buffer used by senders and receivers, whose size is item_size *
   * capacity */
  void *queue;
} channel_t;

/* Initialize a channel of size item_size * capacity and return a pointer to it
 */
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
    ch->capacity = 1 << 4;
  }

  ch->queue = calloc(ch->capacity, item_size);

  if (!ch->queue) {
    free(ch);
    return NULL;
  }

  return ch;
}

/* Send a pointer to value into the channel, place it into the queue */
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
    /* Out of room in an unbounded channel, need to increase the size */
    size_t new_cap = ch->capacity * 2;
    void *new_queue = malloc(new_cap * ch->item_size);
    if (new_queue == NULL) {
      pthread_mutex_unlock(&ch->mu);
      return false;
    }
    if (ch->recv_ptr < ch->send_ptr) {
      /* The queue is in the correct order */
      memcpy(new_queue, (char *)ch->queue + ch->recv_ptr * ch->item_size,
             ch->count * ch->item_size);
    } else {
      /* If we have wrapped around the end of the queue, need to reorganize */
      /* Ex. [0.. send_ptr.. recv_ptr.. capacity] */

      /* This grabs [recv_ptr.. capacity], puts it into the new_queue */
      size_t start_items = ch->capacity - ch->recv_ptr;
      memcpy(new_queue, (char *)ch->queue + ch->recv_ptr * ch->item_size,
             start_items * ch->item_size);

      /* Grab the rest and put it after */
      memcpy((char *)new_queue + start_items * ch->item_size, ch->queue,
             ch->send_ptr * ch->item_size);

      /* New buffer is now properly ordered! */
    }

    free(ch->queue);
    ch->queue = new_queue;
    ch->capacity = new_cap;
    ch->recv_ptr = 0;
    ch->send_ptr = ch->count;
  }

  /* Copy the value into the correct place in the buffer */
  void *slot = (char *)ch->queue + (ch->item_size * ch->send_ptr);
  memcpy(slot, value, ch->item_size);
  ch->count++;

  /* Buffer is circular for simplicity */
  ch->send_ptr = (ch->send_ptr + 1) % ch->capacity;

  /* Wake up the receiver if it is waiting */
  pthread_cond_signal(&ch->recv_cond);
  pthread_mutex_unlock(&ch->mu);
  return true;
}

/* Receive an item from the channel if available, write the data into *value */
bool channel_recv(channel_t *ch, void *value) {
  pthread_mutex_lock(&ch->mu);

  /* Go to sleep if there is nothing in the queue */
  while (ch->count == 0 && !(ch->flags & CH_CLOSED)) {
    pthread_cond_wait(&ch->recv_cond, &ch->mu);
  }

  /* Exit if the channel is closed and empty */
  if (ch->count == 0 && (ch->flags & CH_CLOSED)) {
    pthread_mutex_unlock(&ch->mu);
    return false;
  }

  /* Copy the next item to be received into *value */
  void *slot = (char *)ch->queue + (ch->item_size * ch->recv_ptr);
  memcpy(value, slot, ch->item_size);
  ch->count--;

  /* Buffer is circular for simplicity */
  ch->recv_ptr = (ch->recv_ptr + 1) % ch->capacity;

  /* Wake up a producer if it is waiting for room in the buffer */
  pthread_cond_signal(&ch->send_cond);
  pthread_mutex_unlock(&ch->mu);
  return true;
}

/* Close the channel off to further sending */
void channel_close(channel_t *ch) {
  pthread_mutex_lock(&ch->mu);

  /* Set the closed bit, wake up all the sleeping threads */
  ch->flags |= CH_CLOSED;
  pthread_cond_broadcast(&ch->send_cond);
  pthread_cond_broadcast(&ch->recv_cond);
  pthread_mutex_unlock(&ch->mu);
}

/* Cleanup resources */
void channel_destroy(channel_t *ch) {
  pthread_cond_destroy(&ch->send_cond);
  pthread_cond_destroy(&ch->recv_cond);
  pthread_mutex_destroy(&ch->mu);
  free(ch->queue);
  free(ch);
}
