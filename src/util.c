/**
 * gammad -- Cooperative gamma server
 * Copyright (C) 2016  Mattias Andrée (maandree@kth.se)
 * 
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "util.h"

#include <sys/stat.h>
#include <errno.h>
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
void* memdup(const void* src, size_t n)
{
  void* dest = malloc(n);
  if (dest == NULL)
    return NULL;
  memcpy(dest, src, n);
  return dest;
}


/**
 * Read an entire file
 * 
 * @param   fd  The file descriptor
 * @param   n   Output for the size of the file
 * @return      The read content, plus a NUL byte at
 *              the end (not counted in `*n`)
 */
void* nread(int fd, size_t* n)
{
  size_t size = 32;
  ssize_t got;
  struct stat st;
  char* buf = NULL;
  char* new;
  int saved_errno;
  
  *n = 0;
  
  if (!fstat(fd, &st))
    size = st.st_size <= 0 ? 32 : (size_t)(st.st_size);
  
  buf = malloc(size + 1);
  if (buf == NULL)
    return NULL;
  
  for (;;)
    {
      if (*n == size)
	{
	  new = realloc(buf, (size <<= 1) + 1);
	  if (new == NULL)
	    goto fail;
	  buf = new;
	}
      
      got = read(fd, buf + *n, size - *n);
      if (got < 0)
	{
	  if (errno == EINTR)
	    continue;
	  goto fail;
	}
      if (got == 0)
	break;
      *n += (size_t)got;
    }
  
  buf[*n] = '\0';
  return buf;
 fail:
  saved_errno = errno;
  free(buf);
  errno = saved_errno;
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
size_t nwrite(int fd, const void* buf, size_t n)
{
  const char* bs = buf;
  ssize_t wrote;
  size_t ptr = 0;
  
  while (ptr < n)
    {
      wrote = write(fd, bs + ptr, n - ptr);
      if (wrote <= 0)
	{
	  if ((wrote < 0) && (errno == EINTR))
	    continue;
	  return ptr;
	}
      ptr += (size_t)wrote;
    }
  
  return ptr;
}


/**
 * Duplicate a file descriptor an make sure
 * the new file descriptor's index as a
 * specified minimum value
 * 
 * @param   fd       The file descriptor
 * @param   atleast  The least acceptable new file descriptor
 * @return           The new file descriptor, -1 on error
 */
int dup2atleast(int fd, int atleast)
{
  int* stack = malloc((size_t)(atleast + 1) * sizeof(int));
  size_t stack_ptr = 0;
  int new = -1, saved_errno;
  
  if (stack == NULL)
    goto fail;
  
  for (;;)
    {
      new = dup(fd);
      if (new < 0)
	goto fail;
      if (new >= atleast)
	break;
    }
  
 fail:
  saved_errno = errno;
  while (stack_ptr--)
    close(stack[stack_ptr]);
  free(stack);
  errno = saved_errno;
  return new;
}


/**
 * Perform a timed suspention of the process.
 * The process resumes when the timer expires,
 * or when it is interrupted.
 * 
 * @param  ms  The number of milliseconds to sleep,
 *             must be less than 1000
 */
void msleep(int ms)
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
 * @param   string              The string
 * @param   allow_modified_nul  Whether Modified UTF-8 is allowed, which allows a two-byte encoding for NUL
 * @return                      Zero if good, -1 on encoding error
 */
int verify_utf8(const char* string, int allow_modified_nul)
{
  static long BYTES_TO_MIN_BITS[] = {0, 0,  8, 12, 17, 22, 37};
  static long BYTES_TO_MAX_BITS[] = {0, 7, 11, 16, 21, 26, 31};
  long bytes = 0, read_bytes = 0, bits = 0, c, character;
  
  /*                                                      min bits  max bits
    0.......                                                 0         7
    110..... 10......                                        8        11
    1110.... 10...... 10......                              12        16
    11110... 10...... 10...... 10......                     17        21
    111110.. 10...... 10...... 10...... 10......            22        26
    1111110. 10...... 10...... 10...... 10...... 10......   27        31
   */
  
  while ((c = (long)(*string++)))
    if (read_bytes == 0)
      {
	/* First byte of the character. */
	
	if ((c & 0x80) == 0x00)
	    /* Single-byte character. */
	    continue;
	
	if ((c & 0xC0) == 0x80)
	    /* Single-byte character marked as multibyte, or
	            a non-first byte in a multibyte character. */
	    return -1;
	
	/* Multibyte character. */
	while ((c & 0x80))
	    bytes++, c <<= 1;
	read_bytes = 1;
	character = c & 0x7F;
	if (bytes > 6)
	    /* 31-bit characters can be encoded with 6-bytes,
	            and UTF-8 does not cover higher code points. */
	    return -1;
      }
    else
      {
	/* Not first byte of the character. */
	
	if ((c & 0xC0) != 0x80)
	    /* Beginning of new character before a
	            multibyte character has ended. */
	    return -1;
	
	character = (character << 6) | (c & 0x7F);
	
	if (++read_bytes < bytes)
	    /* Not at last byte yet. */
	    continue;
	
	/* Check that the character is not unnecessarily long. */
	while (character)
	    character >>= 1, bits++;
	bits = ((bits == 0) && (bytes == 2) && allow_modified_nul) ? 8 : bits;
	if ((bits < BYTES_TO_MIN_BITS[bytes]) || (BYTES_TO_MAX_BITS[bytes] < bits))
	    return -1;
	
	read_bytes = bytes = bits = 0;
      }
  
  /* Make sure we did not stop at the middle of a multibyte character. */
  return read_bytes == 0 ? 0 : -1;
}

