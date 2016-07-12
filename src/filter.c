/**
 * coopgammad -- Cooperative gamma server
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
#include "filter.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>



/**
 * Free all resources allocated to a filter.
 * The allocation of `filter` itself is not freed.
 * 
 * @param  this  The filter
 */
void filter_destroy(struct filter* this)
{
  free(this->crtc);
  free(this->class);
  free(this->ramps);
}


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
size_t filter_marshal(const struct filter* this, void* buf, size_t ramps_size)
{
  size_t off = 0, n;
  char nonnulls = 0;
  char* bs = buf;
  
  if (bs != NULL)
    {
      if (this->crtc  != NULL)  nonnulls |= 1;
      if (this->class != NULL)  nonnulls |= 2;
      if (this->ramps != NULL)  nonnulls |= 4;
      *(bs + off) = nonnulls;
    }
  off += 1;
  
  if (bs != NULL)
    *(int64_t*)(bs + off) = this->priority;
  off += sizeof(int64_t);
  
  if (bs != NULL)
    *(enum lifespan*)(bs + off) = this->lifespan;
  off += sizeof(enum lifespan);
  
  if (this->crtc != NULL)
    {
      n = strlen(this->crtc) + 1;
      if (bs != NULL)
	memcpy(bs + off, this->crtc, n);
      off += n;
    }
  
  if (this->class != NULL)
    {
      n = strlen(this->class) + 1;
      if (bs != NULL)
	memcpy(bs + off, this->class, n);
      off += n;
    }
  
  if (this->ramps != NULL)
    {
      if (bs != NULL)
	memcpy(bs + off, this->ramps, ramps_size);
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
size_t filter_unmarshal(struct filter* this, const void* buf, size_t ramps_size)
{
  size_t off = 0, n;
  char nonnulls = 0;
  const char* bs = buf;
  
  nonnulls = *(bs + off);
  off += 1;
  
  this->crtc  = NULL;
  this->class = NULL;
  this->ramps = NULL;
  
  this->priority = *(const int64_t*)(bs + off);
  off += sizeof(int64_t);
  
  this->lifespan = *(const enum lifespan*)(bs + off);
  off += sizeof(enum lifespan);
  
  if (nonnulls & 1)
    {
      n = strlen(bs + off) + 1;
      if (!(this->crtc = memdup(bs + off, n)))
	goto fail;
      off += n;
    }
  
  if (nonnulls & 2)
    {
      n = strlen(bs + off) + 1;
      if (!(this->class = memdup(bs + off, n)))
	goto fail;
      off += n;
    }
  
  if (nonnulls & 4)
    {
      if (!(this->ramps = memdup(bs + off, ramps_size)))
	goto fail;
      off += ramps_size;
    }
  
  return off;
  
 fail:
  free(this->crtc);
  free(this->class);
  free(this->ramps);
  return 0;
}

