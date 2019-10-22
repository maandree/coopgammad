/* See LICENSE file for copyright and license details. */
#ifndef TYPES_FILTER_H
#define TYPES_FILTER_H

#include <stddef.h>
#include <stdint.h>

#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...) __VA_ARGS__
# else
#  define GCC_ONLY(...) /* nothing */
# endif
#endif

/**
 * The lifespan of a filter
 */
enum lifespan {
	/**
	 * The filter should be removed now
	 */
	LIFESPAN_REMOVE = 0,

	/**
	 * The filter should be applied
	 * until it is explicitly removed
	 */
	LIFESPAN_UNTIL_REMOVAL = 1,

	/**
	 * The filter should be applied
	 * until the client exists
	 */
	LIFESPAN_UNTIL_DEATH = 2
};

/**
 * Information about a filter
 */
struct filter {
	/**
	 * The client that applied it. This need not be
	 * set unless `.lifespan == LIFESPAN_UNTIL_DEATH`
	 * and unless the process itself added this.
	 * This is the file descriptor of the client's
	 * connection.
	 */
	int client;

	/**
	 * The lifespan of the filter
	 */
	enum lifespan lifespan;

	/**
	 * The priority of the filter
	 */
	int64_t priority;

	/**
	 * Identifier for the filter
	 */
	char *class;

	/**
	 * The gamma ramp adjustments for the filter.
	 * This is raw binary data. `NULL` iff
	 * `lifespan == LIFESPAN_REMOVE`.
	 */
	void *ramps;
};

/**
 * Free all resources allocated to a filter.
 * The allocation of `filter` itself is not freed.
 * 
 * @param  this  The filter
 */
GCC_ONLY(__attribute__((__nonnull__)))
void filter_destroy(struct filter *restrict this);

/**
 * Marshal a filter
 * 
 * @param   this        The filter
 * @param   buf         Output buffer for the marshalled filter,
 *                      `NULL` just measure how large the buffers
 *                      needs to be
 * @param   ramps_size  The byte-size of `filter->ramps`
 * @return              The number of marshalled byte
 */
GCC_ONLY(__attribute__((__nonnull__(1))))
size_t filter_marshal(const struct filter *restrict this, void *restrict buf, size_t ramps_size);

/**
 * Unmarshal a filter
 * 
 * @param   this        Output for the filter, `.red_size`, `.green_size`,
 *                      and `.blue_size` must already be set
 * @param   buf         Buffer with the marshalled filter
 * @param   ramps_size  The byte-size of `filter->ramps`
 * @return              The number of unmarshalled bytes, 0 on error
 */
GCC_ONLY(__attribute__((__nonnull__)))
size_t filter_unmarshal(struct filter *restrict this, const void *restrict buf, size_t ramps_size);

#endif
