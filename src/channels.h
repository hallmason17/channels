#ifndef CHANNELS_H_
#define CHANNELS_H_

#include <stdbool.h>
#include <stddef.h>

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

#endif // CHANNELS_H_
