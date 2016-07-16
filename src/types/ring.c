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
#include "ring.h"

#include <stdlib.h>
#include <string.h>



/**
 * Initialise a ring buffer
 * 
 * @param  this  The ring buffer
 */
void ring_initialise(struct ring* restrict this)
{
  this->start  = 0;
  this->end    = 0;
  this->size   = 0;
  this->buffer = NULL;
}


/**
 * Release resource allocated to a ring buffer
 * 
 * @param  this  The ring buffer
 */
void ring_destroy(struct ring* restrict this)
{
  free(this->buffer);
}



#if defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wcast-align"
#endif


/**
 * Marshal a ring buffer
 * 
 * @param   this  The ring buffer
 * @param   buf   Output buffer for the marshalled data,
 *                `NULL` to only measure how large buffer
 *                is needed
 * @return        The number of marshalled bytes
 */
size_t ring_marshal(const struct ring* restrict this, void* restrict buf)
{
  size_t off = 0, n = this->end - this->start;
  char* bs = buf;
  
  if (bs != NULL)
    *(size_t*)(bs + off) = n;
  off += sizeof(size_t);
  
  if (bs != NULL)
    memcpy(bs + off, this->buffer + this->start, n);
  off += n;
  
  return off;
}


/**
 * Unmarshal a ring buffer
 * 
 * @param   this  Output parameter for the ring buffer
 * @param   buf   Buffer with the marshalled data
 * @return        The number of unmarshalled bytes, 0 on error
 */
size_t ring_unmarshal(struct ring* restrict this, const void* restrict buf)
{
  size_t off = 0;
  const char* bs = buf;
  
  ring_initialise(this);
  
  this->size = this->end = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  if (this->end > 0)
    {
      if (!(this->buffer = malloc(this->end)))
	return 0;
      
      memcpy(this->buffer, bs + off, this->end);
      off += this->end;
    }
  
  return off;
}


#if defined(__clang__)
# pragma GCC diagnostic pop
#endif



/**
 * Append data to a ring buffer
 * 
 * @param   this  The ring buffer
 * @param   data  The new data
 * @param   n     The number of bytes in `data`
 * @return        Zero on success, -1 on error
 */
int ring_push(struct ring* restrict this, void* restrict data, size_t n)
{
  size_t used = 0;
  
  if (this->start == this->end)
    {
      if (this->buffer != NULL)
	used = this->size;
    }
  else if (this->start > this->end)
    used = this->size - this->start + this->end;
  else
    used = this->start - this->end;
  
  if (used + n > this->size)
    {
      char* restrict new = malloc(used + n);
      if (new == NULL)
	return -1;
      if (this->buffer)
	{
	  if (this->start < this->end)
	    memcpy(new, this->buffer + this->start, this->end - this->start);
	  else
	    {
	      memcpy(new, this->buffer + this->start, this->size - this->start);
	      memcpy(new + this->size - this->start, this->buffer, this->end);
	    }
	}
      memcpy(new + used, data, n);
      this->buffer = new;
      this->start = 0;
      this->end = used + n;
      this->size = used + n;
    }
  else if ((this->start >= this->end) || (this->end + n <= this->size))
    {
      memcpy(this->buffer + this->end, data, n);
      this->end += n;
    }
  else
    {
      memcpy(this->buffer + this->end, data, this->size - this->end);
      data = (char*)data + (this->size - this->end);
      n -= this->size - this->end;
      memcpy(this->buffer, data, n);
      this->end = n;
    }
  
  return 0;
}


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
void* ring_peek(struct ring* restrict this, size_t* restrict n)
{
  if (this->buffer == NULL)
    return *n = 0, NULL;
  
  if (this->start < this->end)
    *n = this->end - this->start;
  else
    *n = this->size - this->start;
  return this->buffer + this->start;
}


/**
 * Dequeue data from a ring bubber
 * 
 * @param  this  The ring buffer
 * @param  n     The number of bytes to dequeue
 */
void ring_pop(struct ring* restrict this, size_t n)
{
  this->start += n;
  this->start %= this->size;
  if (this->start == this->end)
    {
      free(this->buffer);
      this->start  = 0;
      this->end    = 0;
      this->size   = 0;
      this->buffer = NULL;
    }
}

