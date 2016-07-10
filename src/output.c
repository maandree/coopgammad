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
#include "output.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>



/**
 * Free all resources allocated to an output.
 * The allocation of `output` itself is not freed.
 * 
 * @param  this  The output
 */
void output_destroy(struct output* this)
{
  size_t i;
  
  if (this->supported != LIBGAMMA_NO)
    switch (this->depth)
      {
      case 8:
	libgamma_gamma_ramps8_destroy(&(this->saved_ramps.u8));
	for (i = 0; i < this->table_size; i++)
	  libgamma_gamma_ramps8_destroy(&(this->table_sums[i].u8));
	break;
      case 16:
	libgamma_gamma_ramps16_destroy(&(this->saved_ramps.u16));
	for (i = 0; i < this->table_size; i++)
	  libgamma_gamma_ramps16_destroy(&(this->table_sums[i].u16));
	break;
      case 32:
	libgamma_gamma_ramps32_destroy(&(this->saved_ramps.u32));
	for (i = 0; i < this->table_size; i++)
	  libgamma_gamma_ramps32_destroy(&(this->table_sums[i].u32));
	break;
      case 64:
	libgamma_gamma_ramps64_destroy(&(this->saved_ramps.u64));
	for (i = 0; i < this->table_size; i++)
	  libgamma_gamma_ramps64_destroy(&(this->table_sums[i].u64));
	break;
      case -1:
	libgamma_gamma_rampsf_destroy(&(this->saved_ramps.f));
	for (i = 0; i < this->table_size; i++)
	  libgamma_gamma_rampsf_destroy(&(this->table_sums[i].f));
	break;
      case -2:
	libgamma_gamma_rampsd_destroy(&(this->saved_ramps.d));
	for (i = 0; i < this->table_size; i++)
	  libgamma_gamma_rampsd_destroy(&(this->table_sums[i].d));
	break;
      default:
	break; /* impossible */
      }
  
  for (i = 0; i < this->table_size; i++)
    filter_destroy(this->table_filters + i);
  
  free(this->table_filters);
  free(this->table_sums);
  free(this->name);
}


/**
 * Marshal an output
 * 
 * @param   this  The output
 * @param   buf   Output buffer for the marshalled output,
 *                `NULL` just measure how large the buffers
 *                needs to be
 * @return        The number of marshalled byte
 */
size_t output_marshal(const struct output* this, char* buf)
{
  size_t off = 0, i, n;
  
  if (buf != NULL)
    *(signed*)(buf + off) = this->depth;
  off += sizeof(signed);
  
  if (buf != NULL)
    *(size_t*)(buf + off) = this->red_size;
  off += sizeof(size_t);
  
  if (buf != NULL)
    *(size_t*)(buf + off) = this->green_size;
  off += sizeof(size_t);
  
  if (buf != NULL)
    *(size_t*)(buf + off) = this->blue_size;
  off += sizeof(size_t);
  
  if (buf != NULL)
    *(size_t*)(buf + off) = this->ramps_size;
  off += sizeof(size_t);
  
  if (buf != NULL)
    *(enum libgamma_decision*)(buf + off) = this->supported;
  off += sizeof(enum libgamma_decision);
  
  n = strlen(this->name) + 1;
  if (buf != NULL)
    memcpy(buf + off, this->name, n);
  off += n;
  
  off += gamma_ramps_marshal(&(this->saved_ramps), buf ? buf + off : NULL, this->ramps_size);
  
  if (buf != NULL)
    *(size_t*)(buf + off) = this->table_size;
  off += sizeof(size_t);
  
  for (i = 0; i < this->table_size; i++)
    {
      off +=      filter_marshal(this->table_filters + i, buf ? buf + off : NULL, this->ramps_size);
      off += gamma_ramps_marshal(this->table_sums    + i, buf ? buf + off : NULL, this->ramps_size);
    }
  
  return off;
}


/**
 * Unmarshal an output
 * 
 * @param   this  Output for the output
 * @param   buf   Buffer with the marshalled output
 * @return        The number of unmarshalled bytes, 0 on error
 */
size_t output_unmarshal(struct output* this, const char* buf)
{
  size_t off = 0, i, n;
  
  this->crtc = NULL;
  this->name = NULL;
  
  this->depth = *(const signed*)(buf + off);
  off += sizeof(signed);
  
  this->red_size = *(const size_t*)(buf + off);
  off += sizeof(size_t);
  
  this->green_size = *(const size_t*)(buf + off);
  off += sizeof(size_t);
  
  this->blue_size = *(const size_t*)(buf + off);
  off += sizeof(size_t);
  
  this->ramps_size = *(const size_t*)(buf + off);
  off += sizeof(size_t);
  
  this->supported = *(const enum libgamma_decision*)(buf + off);
  off += sizeof(enum libgamma_decision);
  
  n = strlen(buf + off) + 1;
  this->name = memdup(buf + off, n);
  if (this->name == NULL)
    return 0;
  
  off += n = gamma_ramps_unmarshal(&(this->saved_ramps), buf, this->ramps_size);
  COPY_RAMP_SIZES(&(this->saved_ramps.u8), this);
  if (n == 0)
    return 0;
  
  this->table_size = this->table_alloc = *(const size_t*)(buf + off);
  off += sizeof(size_t);
  if (this->table_size > 0)
    {
      this->table_filters = calloc(this->table_size, sizeof(*(this->table_filters)));
      if (this->table_filters == NULL)
	return 0;
      this->table_sums = calloc(this->table_size, sizeof(*(this->table_sums)));
      if (this->table_sums == NULL)
	return 0;
    }
  
  for (i = 0; i < this->table_size; i++)
    {
      off += n = filter_unmarshal(this->table_filters + i, buf + off, this->ramps_size);
      if (n == 0)
	return 0;
      COPY_RAMP_SIZES(&(this->table_sums[i].u8), this);
      off += n = gamma_ramps_unmarshal(this->table_sums + i, buf + off, this->ramps_size);
      if (n == 0)
	return 0;
    }
  
  return off;
}

