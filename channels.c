#include "channels.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
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
    cnd_t cond;
    mtx_t mu;
} channel_t;

channel_t *channel_create(size_t item_size, size_t capacity) {
    channel_t *ch = malloc(sizeof(channel_t));
    ch->item_size = item_size;
    ch->capacity = capacity;
    ch->flags = 0;
    ch->count = 0;
    ch->recv_ptr = 0;
    ch->send_ptr = 0;
    mtx_init(&ch->mu, 0);
    cnd_init(&ch->cond);
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
    mtx_lock(&ch->mu);
    if (ch->flags & CH_CLOSED) {
        mtx_unlock(&ch->mu);
        return false;
    }
    if (ch->flags & CH_BOUNDED) {
        while (ch->count >= ch->capacity) {
            cnd_wait(&ch->cond, &ch->mu);
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

    cnd_signal(&ch->cond);
    mtx_unlock(&ch->mu);
    return true;
}
bool channel_recv(channel_t *ch, void *value) {
    mtx_lock(&ch->mu);
    if (ch->flags & CH_CLOSED) {
        mtx_unlock(&ch->mu);
        return false;
    }
    while (ch->count == 0) {
        cnd_wait(&ch->cond, &ch->mu);
    }
    void *slot = (char *)ch->queue + (ch->item_size * ch->recv_ptr);
    memcpy(value, slot, ch->item_size);
    ch->count--;
    ch->recv_ptr = (ch->recv_ptr + 1) % ch->capacity;

    cnd_broadcast(&ch->cond);
    mtx_unlock(&ch->mu);
    return true;
}
void channel_close(channel_t *ch) { ch->flags &= CH_CLOSED; }
void channel_destroy(channel_t *ch) {
    cnd_destroy(&ch->cond);
    mtx_destroy(&ch->mu);
    free(ch->queue);
    free(ch);
}

struct thrd_sender {
    channel_t *ch;
    int val;
};

#define NUM_SENDS 10000000
int ch_send(void *arg) {
    struct thrd_sender data = *(struct thrd_sender *)arg;
    for (int i = 0; i < NUM_SENDS; i++) {
        channel_send(data.ch, &i);
    }
    return 0;
}

static atomic_int num_rcvs = 0;
int main(void) {
    channel_t *ch = channel_create(sizeof(int), 0);
    int x = 10;
    int y;
    thrd_t t;
    struct thrd_sender s = (struct thrd_sender){.ch = ch, .val = x};
    thrd_create(&t, ch_send, &s);
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
    thrd_join(t, 0);
}
