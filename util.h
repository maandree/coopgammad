/* See LICENSE file for copyright and license details. */
#ifndef UTIL_H
#define UTIL_H

#include "types-output.h"

#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...) __VA_ARGS__
# else
#  define GCC_ONLY(...) /* nothing */
# endif
#endif

/**
 * Duplicate a memory segment
 * 
 * @param   src  The memory segment, must not be `NULL`
 * @param   n    The size of the memory segment, must not be zero
 * @return       The duplicate of the memory segment,
 *               `NULL` on error
 */
GCC_ONLY(__attribute__((__malloc__, __nonnull__)))
void *memdup(const void *restrict src, size_t n);

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
GCC_ONLY(__attribute__((__malloc__)))
void *nread(int fd, size_t *restrict n);

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
size_t nwrite(int fd, const void *restrict buf, size_t n);

/**
 * Perform a timed suspention of the process.
 * The process resumes when the timer expires,
 * or when it is interrupted.
 * 
 * @param  ms  The number of milliseconds to sleep,
 *             must be less than 1000
 */
void msleep(unsigned ms);

/**
 * Check whether a NUL-terminated string is encoded in UTF-8
 * 
 * @param   string  The string
 * @return          Zero if good, -1 on encoding error
 */
GCC_ONLY(__attribute__((__pure__, __nonnull__)))
int verify_utf8(const char *restrict string);

/**
 * Make identity mapping ramps
 * 
 * @param   ramps   Output parameter for the ramps
 * @param   output  The output for which the ramps shall be configured
 * @return          Zero on success, -1 on error
 */
GCC_ONLY(__attribute__((__nonnull__)))
int make_plain_ramps(union gamma_ramps *restrict ramps, struct output *restrict output);

#endif
