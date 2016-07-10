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
#define _GNU_SOURCE
#include "filter.h"

#include <string.h>



/**
 * Free all resources allocated to a filter.
 * The allocation of `filter` itself is not freed.
 * 
 * @param  filter  The filter.
 */
void filter_destroy(struct filter* filter)
{
  free(filter->crtc);
  free(filter->class);
  free(filter->ramps);
}


/**
 * Marshal a filter.
 * 
 * @param   filter      The filter.
 * @param   buf         Output buffer for the marshalled filter.
 *                      `NULL` just measure how large the buffers
 *                      needs to be.
 * @param   ramps_size  The byte-size of `filter->ramps`
 * @return              The number of marshalled byte
 */
size_t filter_marshal(const struct filter* filter, char* buf, size_t ramps_size)
{
  size_t off = 0, n;
  char nonnulls = 0;
  
  if (buf != NULL)
    {
      if (filter->crtc  != NULL)  nonnulls |= 1;
      if (filter->class != NULL)  nonnulls |= 2;
      if (filter->ramps != NULL)  nonnulls |= 4;
      *(buf + off) = nonnulls;
    }
  off += 1;
  
  if (buf != NULL)
    *(int64_t*)(buf + off) = filter->priority;
  off += sizeof(int64_t);
  
  if (buf != NULL)
    *(enum lifespan*)(buf + off) = filter->lifespan;
  off += sizeof(enum lifespan);
  
  if (filter->crtc != NULL)
    {
      n = strlen(filter->crtc) + 1;
      if (buf != NULL)
	memcpy(buf + off, filter->crtc, n);
      off += n;
    }
  
  if (filter->class != NULL)
    {
      n = strlen(filter->class) + 1;
      if (buf != NULL)
	memcpy(buf + off, filter->class, n);
      off += n;
    }
  
  if (filter->ramps != NULL)
    {
      if (buf != NULL)
	memcpy(buf + off, filter->ramps, ramps_size);
      off += n;
    }
  
  return off;
}


/**
 * Unmarshal a filter.
 * 
 * @param   filter      Output for the filter, `NULL` to skip unmarshalling
 * @param   buf         Buffer with the marshalled filter
 * @param   ramps_size  The byte-size of `filter->ramps`
 * @return              The number of unmarshalled bytes, 0 on error
 */
size_t filter_unmarshal(struct filter* filter, const char* buf, size_t ramps_size)
{
  size_t off = 0, n;
  char nonnulls = 0;
  
  nonnulls = *(buf + off);
  off += 1;
  
  if (filter != NULL)
    {
      filter->crtc  = NULL;
      filter->class = NULL;
      filter->ramps = NULL;
    }
  
  if (filter != NULL)
    filter->priority = *(int64_t*)(buf + off);
  off += sizeof(int64_t);
  
  if (filter != NULL)
    filter->lifespan = *(enum lifespan*)(buf + off);
  off += sizeof(enum lifespan);
  
  if (nonnulls & 1)
    {
      n = strlen(buf + off) + 1;
      if ((filter != NULL) && (!(filter->crtc = memdup(buf + off, n))))
	goto fail;
      off += n;
    }
  
  if (nonnulls & 2)
    {
      n = strlen(buf + off) + 1;
      if ((filter != NULL) && (!(filter->class = memdup(buf + off, n))))
	goto fail;
      off += n;
    }
  
  if (nonnulls & 4)
    {
      if ((filter != NULL) && (!(filter->ramps = memdup(buf + off, ramps_size))))
	goto fail;
      off += n;
    }
  
  return off;
  
 fail:
  free(filter->crtc);
  free(filter->class);
  free(filter->ramps);
  return 0;
}

