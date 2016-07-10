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
size_t filter_marshal(const struct filter* this, char* buf, size_t ramps_size)
{
  size_t off = 0, n;
  char nonnulls = 0;
  
  if (buf != NULL)
    {
      if (this->crtc  != NULL)  nonnulls |= 1;
      if (this->class != NULL)  nonnulls |= 2;
      if (this->ramps != NULL)  nonnulls |= 4;
      *(buf + off) = nonnulls;
    }
  off += 1;
  
  if (buf != NULL)
    *(int64_t*)(buf + off) = this->priority;
  off += sizeof(int64_t);
  
  if (buf != NULL)
    *(enum lifespan*)(buf + off) = this->lifespan;
  off += sizeof(enum lifespan);
  
  if (this->crtc != NULL)
    {
      n = strlen(this->crtc) + 1;
      if (buf != NULL)
	memcpy(buf + off, this->crtc, n);
      off += n;
    }
  
  if (this->class != NULL)
    {
      n = strlen(this->class) + 1;
      if (buf != NULL)
	memcpy(buf + off, this->class, n);
      off += n;
    }
  
  if (this->ramps != NULL)
    {
      if (buf != NULL)
	memcpy(buf + off, this->ramps, ramps_size);
      off += n;
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
size_t filter_unmarshal(struct filter* this, const char* buf, size_t ramps_size)
{
  size_t off = 0, n;
  char nonnulls = 0;
  
  nonnulls = *(buf + off);
  off += 1;
  
  this->crtc  = NULL;
  this->class = NULL;
  this->ramps = NULL;
  
  this->priority = *(int64_t*)(buf + off);
  off += sizeof(int64_t);
  
  this->lifespan = *(enum lifespan*)(buf + off);
  off += sizeof(enum lifespan);
  
  if (nonnulls & 1)
    {
      n = strlen(buf + off) + 1;
      if (!(this->crtc = memdup(buf + off, n)))
	goto fail;
      off += n;
    }
  
  if (nonnulls & 2)
    {
      n = strlen(buf + off) + 1;
      if (!(this->class = memdup(buf + off, n)))
	goto fail;
      off += n;
    }
  
  if (nonnulls & 4)
    {
      if (!(this->ramps = memdup(buf + off, ramps_size)))
	goto fail;
      off += n;
    }
  
  return off;
  
 fail:
  free(this->crtc);
  free(this->class);
  free(this->ramps);
  return 0;
}

