#ifndef CHANNELS_H_
#define CHANNELS_H_

#include <stdbool.h>
#include <stddef.h>
typedef struct channel_t channel_t;
channel_t *channel_create(size_t item_size, size_t capacity);
bool channel_send(channel_t *ch, const void *value);
bool channel_recv(channel_t *ch, void *value);
void channel_close(channel_t *ch);
void channel_destroy(channel_t *ch);

#endif // CHANNELS_H_
