/* See LICENSE file for copyright and license details. */
#include "util.h"

#include <libclut.h>

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


/**
 * Duplicate a memory segment
 * 
 * @param   src  The memory segment, must not be `NULL`
 * @param   n    The size of the memory segment, must not be zero
 * @return       The duplicate of the memory segment,
 *               `NULL` on error
 */
void *
memdup(const void *restrict src, size_t n)
{
	void *dest = malloc(n);
	if (!dest)
		return NULL;
	memcpy(dest, src, n);
	return dest;
}


/**
 * Read an entire file
 * 
 * Not cancelled by `EINTR`
 * 
 * @param   fd  The file descriptor
 * @param   n   Output for the size of the file
 * @return      The read content, plus a NUL byte at
 *              the end (not counted in `*n`)
 */
void *
nread(int fd, size_t *restrict n)
{
	size_t size = 32;
	ssize_t got;
	struct stat st;
	char *buf, *new;

	*n = 0;

	if (!fstat(fd, &st))
		size = st.st_size <= 0 ? 32 : (size_t)(st.st_size);

	buf = malloc(size + 1);
	if (!buf)
		return NULL;

	for (;;) {
		if (*n == size) {
			new = realloc(buf, (size <<= 1) + 1);
			if (!new)
				goto fail;
			buf = new;
		}

		got = read(fd, buf + *n, size - *n);
		if (got <= 0) {
			if (!got)
				break;
			if (errno == EINTR)
				continue;
			goto fail;
		}
		*n += (size_t)got;
	}

	buf[*n] = '\0';
	return buf;

fail:
	free(buf);
	return NULL;
}


/**
 * Write an entire buffer to a file
 * 
 * Not cancelled by `EINTR`
 * 
 * @param   fd   The file descriptor
 * @param   buf  The buffer which shall be written to the fail
 * @param   n    The size of the buffer
 * @return       The number of written bytes, less than `n`
 *               on error, cannot exceed `n`
 */
size_t
nwrite(int fd, const void *restrict buf, size_t n)
{
	const char *restrict bs = buf;
	ssize_t wrote;
	size_t ptr = 0;

	while (ptr < n) {
		wrote = write(fd, bs + ptr, n - ptr);
		if (wrote <= 0) {
			if (wrote < 0 && errno == EINTR)
				continue;
			return ptr;
		}
		ptr += (size_t)wrote;
	}

	return ptr;
}


/**
 * Perform a timed suspention of the process.
 * The process resumes when the timer expires,
 * or when it is interrupted.
 * 
 * @param  ms  The number of milliseconds to sleep,
 *             must be less than 1000
 */
void
msleep(unsigned ms)
{
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = (long)ms * 1000000L;
	if (clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL) == ENOTSUP)
		nanosleep(&ts, NULL);
}


/**
 * Check whether a NUL-terminated string is encoded in UTF-8
 * 
 * @param   string  The string
 * @return          Zero if good, -1 on encoding error
 */
int
verify_utf8(const char *restrict string)
{
	static long BYTES_TO_MIN_BITS[] = {0, 0,  8, 12, 17, 22, 37};
	static long BYTES_TO_MAX_BITS[] = {0, 7, 11, 16, 21, 26, 31};
	long int bytes = 0, read_bytes = 0, bits = 0, c, character = 0;

	/*                                                      min bits  max bits
	  0.......                                                 0         7
	  110..... 10......                                        8        11
	  1110.... 10...... 10......                              12        16
	  11110... 10...... 10...... 10......                     17        21
	  111110.. 10...... 10...... 10...... 10......            22        26
	  1111110. 10...... 10...... 10...... 10...... 10......   27        31
	 */

	while ((c = (long)(*string++))) {
		if (!read_bytes) {
			/* First byte of the character. */

			if ((c & 0x80) == 0x00) {
				/* Single-byte character. */
				continue;
			}

			if ((c & 0xC0) == 0x80) {
				/* Single-byte character marked as multibyte, or
				   a non-first byte in a multibyte character. */
				return -1;
			}

			/* Multibyte character. */
			while ((c & 0x80)) {
				bytes++;
				c <<= 1;
			}
			read_bytes = 1;
			character = c & 0x7F;
			if (bytes > 6) {
				/* 31-bit characters can be encoded with 6-bytes,
				   and UTF-8 does not cover higher code points. */
				return -1;
			}
		} else {
			/* Not first byte of the character. */

			if ((c & 0xC0) != 0x80) {
				/* Beginning of new character before a
				   multibyte character has ended. */
				return -1;
			}

			character = (character << 6) | (c & 0x7F);

			if (++read_bytes < bytes) {
				/* Not at last byte yet. */
				continue;
			}

			/* Check that the character is not unnecessarily long. */
			while (character) {
				character >>= 1, bits++;
			}
			if ((bits < BYTES_TO_MIN_BITS[bytes]) || (BYTES_TO_MAX_BITS[bytes] < bits)) {
				return -1;
			}

			read_bytes = bytes = bits = 0;
		}
	}

	/* Make sure we did not stop at the middle of a multibyte character. */
	return !read_bytes ? 0 : -1;
}


/**
 * Make identity mapping ramps
 * 
 * @param   ramps   Output parameter for the ramps
 * @param   output  The output for which the ramps shall be configured
 * @return          Zero on success, -1 on error
 */
int
make_plain_ramps(union gamma_ramps *restrict ramps, struct output *restrict output)
{
	COPY_RAMP_SIZES(&ramps->u8, output);
	switch (output->depth) {
	case 8:
		if (libgamma_gamma_ramps8_initialise(&(ramps->u8)))
			return -1;
		libclut_start_over(&ramps->u8, UINT8_MAX, uint8_t, 1, 1, 1);
		break;

	case 16:
		if (libgamma_gamma_ramps16_initialise(&(ramps->u16)))
			return -1;
		libclut_start_over(&ramps->u16, UINT16_MAX, uint16_t, 1, 1, 1);
		break;

	case 32:
		if (libgamma_gamma_ramps32_initialise(&(ramps->u32)))
			return -1;
		libclut_start_over(&ramps->u32, UINT32_MAX, uint32_t, 1, 1, 1);
		break;

	case 64:
		if (libgamma_gamma_ramps64_initialise(&(ramps->u64)))
			return -1;
		libclut_start_over(&ramps->u64, UINT64_MAX, uint64_t, 1, 1, 1);
		break;

	case -1:
		if (libgamma_gamma_rampsf_initialise(&(ramps->f)))
			return -1;
		libclut_start_over(&ramps->f, 1.0f, float, 1, 1, 1);
		break;

	case -2:
		if (libgamma_gamma_rampsd_initialise(&(ramps->d)))
			return -1;
		libclut_start_over(&ramps->d, (double)1, double, 1, 1, 1);
		break;

	default:
		abort();
	}
	return 0;
}
