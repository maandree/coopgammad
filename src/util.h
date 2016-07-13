/**
 * coopgammad -- Cooperative gamma server
 * Copyright (C) 2016  Mattias Andrée (maandree@kth.se)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stddef.h>



/**
 * Duplicate a memory segment
 * 
 * @param   src  The memory segment, must not be `NULL`
 * @param   n    The size of the memory segment, must not be zero
 * @return       The duplicate of the memory segment,
 *               `NULL` on error
 */
void* memdup(const void* src, size_t n);


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
void* nread(int fd, size_t* n);


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
size_t nwrite(int fd, const void* buf, size_t n);


/**
 * Duplicate a file descriptor an make sure
 * the new file descriptor's index as a
 * specified minimum value
 * 
 * @param   fd       The file descriptor
 * @param   atleast  The least acceptable new file descriptor
 * @return           The new file descriptor, -1 on error
 */
int dup2atleast(int fd, int atleast);


/**
 * Perform a timed suspention of the process.
 * The process resumes when the timer expires,
 * or when it is interrupted.
 * 
 * @param  ms  The number of milliseconds to sleep,
 *             must be less than 1000
 */
void msleep(int ms);


/**
 * Check whether a NUL-terminated string is encoded in UTF-8
 * 
 * @param   string              The string
 * @param   allow_modified_nul  Whether Modified UTF-8 is allowed, which allows a two-byte encoding for NUL
 * @return                      Zero if good, -1 on encoding error
 */
#if defined(__GNUC__)
__attribute__((pure))
#endif
int verify_utf8(const char* string, int allow_modified_nul);

