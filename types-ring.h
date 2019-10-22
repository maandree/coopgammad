/* See LICENSE file for copyright and license details. */
#ifndef TYPES_RING_H
#define TYPES_RING_H

#include <stdlib.h>

#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...) __VA_ARGS__
# else
#  define GCC_ONLY(...) /* nothing */
# endif
#endif

/**
 * Ring buffer
 */
struct ring {
	/**
	 * Buffer for the data
	 */
	char *restrict buffer;

	/**
	 * The first set byte in `.buffer`
	 */
	size_t start;

	/**
	 * The last set byte in `.buffer`, plus 1
	 */
	size_t end;

	/**
	 * The size of `.buffer`
	 */
	size_t size;
};

/**
 * Initialise a ring buffer
 * 
 * @param  this  The ring buffer
 */
GCC_ONLY(__attribute__((__nonnull__)))
void ring_initialise(struct ring *restrict this);

/**
 * Marshal a ring buffer
 * 
 * @param   this  The ring buffer
 * @param   buf   Output buffer for the marshalled data,
 *                `NULL` to only measure how large buffer
 *                is needed
 * @return        The number of marshalled bytes
 */
GCC_ONLY(__attribute__((__nonnull__(1))))
size_t ring_marshal(const struct ring *restrict this, void *restrict buf);

/**
 * Unmarshal a ring buffer
 * 
 * @param   this  Output parameter for the ring buffer
 * @param   buf   Buffer with the marshalled data
 * @return        The number of unmarshalled bytes, 0 on error
 */
GCC_ONLY(__attribute__((__nonnull__)))
size_t ring_unmarshal(struct ring *restrict this, const void *restrict buf);

/**
 * Append data to a ring buffer
 * 
 * @param   this  The ring buffer
 * @param   data  The new data
 * @param   n     The number of bytes in `data`
 * @return        Zero on success, -1 on error
 */
GCC_ONLY(__attribute__((__nonnull__(1))))
int ring_push(struct ring *restrict this, void *restrict data, size_t n);

/**
 * Get queued data from a ring buffer
 * 
 * It can take up to two calls (with `ring_pop` between)
 * to get all queued data
 * 
 * @param   this  The ring buffer
 * @param   n     Output parameter for the length
 *                of the returned segment
 * @return        The beginning of the queued data,
 *                `NULL` if there is nothing more
 */
GCC_ONLY(__attribute__((__nonnull__)))
void *ring_peek(struct ring *restrict this, size_t *restrict n);

/**
 * Dequeue data from a ring bubber
 * 
 * @param  this  The ring buffer
 * @param  n     The number of bytes to dequeue
 */
GCC_ONLY(__attribute__((__nonnull__)))
void ring_pop(struct ring *restrict this, size_t n);

/**
 * Release resource allocated to a ring buffer
 * 
 * @param  this  The ring buffer
 */
GCC_ONLY(__attribute__((__nonnull__)))
static inline void
ring_destroy(struct ring *restrict this)
{
	free(this->buffer);
}

/**
 * Check whether there is more data waiting
 * in a ring buffer
 * 
 * @param   this  The ring buffer
 * @return        1 if there is more data, 0 otherwise
 */
GCC_ONLY(__attribute__((__nonnull__)))
static inline int
ring_have_more(struct ring *restrict this)
{
	return !!this->buffer;
}

#endif
