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
	goto fail;
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

