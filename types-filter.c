/* See LICENSE file for copyright and license details. */
#include "types-filter.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>


/**
 * Free all resources allocated to a filter.
 * The allocation of `filter` itself is not freed.
 * 
 * @param  this  The filter
 */
void
filter_destroy(struct filter *restrict this)
{
	free(this->class);
	free(this->ramps);
}


#if defined(__clang__)
# pragma GCC diagnostic ignored "-Wcast-align"
#endif


/**
 * Marshal a filter
 * 
 * @param   this        The filter
 * @param   buf         Output buffer for the marshalled filter,
 *                      `NULL` just measure how large the buffers
 *                      needs to be
 * @param   ramps_size  The byte-size of `this->ramps`
 * @return              The number of marshalled byte
 */
size_t
filter_marshal(const struct filter *restrict this, void *restrict buf, size_t ramps_size)
{
	size_t off = 0, n;
	char nonnulls = 0;
	char *restrict bs = buf;

	if (bs) {
		if (this->class)
			nonnulls |= 1;
		if (this->ramps)
			nonnulls |= 2;
		bs[off] = nonnulls;
	}
	off += 1;

	if (bs)
		*(int64_t *)&bs[off] = this->priority;
	off += sizeof(int64_t);

	if (bs)
		*(enum lifespan *)&bs[off] = this->lifespan;
	off += sizeof(enum lifespan);

	if (this->class) {
		n = strlen(this->class) + 1;
		if (bs)
			memcpy(&bs[off], this->class, n);
		off += n;
	}

	if (this->ramps) {
		if (bs)
			memcpy(&bs[off], this->ramps, ramps_size);
		off += ramps_size;
	}

	return off;
}


/**
 * Unmarshal a filter
 * 
 * @param   this        Output for the filter
 * @param   buf         Buffer with the marshalled filter
 * @param   ramps_size  The byte-size of `this->ramps`
 * @return              The number of unmarshalled bytes, 0 on error
 */
size_t
filter_unmarshal(struct filter *restrict this, const void *restrict buf, size_t ramps_size)
{
	size_t off = 0, n;
	char nonnulls = 0;
	const char *restrict bs = buf;

	nonnulls = bs[off];
	off += 1;

	this->class = NULL;
	this->ramps = NULL;

	this->priority = *(const int64_t *)&bs[off];
	off += sizeof(int64_t);

	this->lifespan = *(const enum lifespan *)&bs[off];
	off += sizeof(enum lifespan);

	if (nonnulls & 1) {
		n = strlen(&bs[off]) + 1;
		if (!(this->class = memdup(&bs[off], n)))
			goto fail;
		off += n;
	}

	if (nonnulls & 2) {
		if (!(this->ramps = memdup(&bs[off], ramps_size)))
			goto fail;
		off += ramps_size;
	}

	return off;

fail:
	free(this->class);
	free(this->ramps);
	return 0;
}
