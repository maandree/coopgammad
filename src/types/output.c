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
#include "output.h"
#include "../util.h"

#include <stdlib.h>
#include <string.h>



/**
 * Free all resources allocated to an output.
 * The allocation of `output` itself is not freed,
 * nor is its the libgamma destroyed.
 * 
 * @param  this  The output
 */
void output_destroy(struct output* restrict this)
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



#if defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wcast-align"
#endif


/**
 * Marshal an output
 * 
 * @param   this  The output
 * @param   buf   Output buffer for the marshalled output,
 *                `NULL` just measure how large the buffers
 *                needs to be
 * @return        The number of marshalled byte
 */
size_t output_marshal(const struct output* restrict this, void* restrict buf)
{
  size_t off = 0, i, n;
  char* bs = buf;
  
  if (bs != NULL)
    *(signed*)(bs + off) = this->depth;
  off += sizeof(signed);
  
  if (bs != NULL)
    *(size_t*)(bs + off) = this->red_size;
  off += sizeof(size_t);
  
  if (bs != NULL)
    *(size_t*)(bs + off) = this->green_size;
  off += sizeof(size_t);
  
  if (bs != NULL)
    *(size_t*)(bs + off) = this->blue_size;
  off += sizeof(size_t);
  
  if (bs != NULL)
    *(size_t*)(bs + off) = this->ramps_size;
  off += sizeof(size_t);
  
  if (bs != NULL)
    *(enum libgamma_decision*)(bs + off) = this->supported;
  off += sizeof(enum libgamma_decision);
  
  if (bs != NULL)
    *(enum colourspace*)(bs + off) = this->colourspace;
  off += sizeof(enum colourspace);
  
  if (bs != NULL)
    *(int*)(bs + off) = this->name_is_edid;
  off += sizeof(int);
  
  if (bs != NULL)
    *(unsigned*)(bs + off) = this->red_x;
  off += sizeof(unsigned);
  
  if (bs != NULL)
    *(unsigned*)(bs + off) = this->red_y;
  off += sizeof(unsigned);
  
  if (bs != NULL)
    *(unsigned*)(bs + off) = this->green_x;
  off += sizeof(unsigned);
  
  if (bs != NULL)
    *(unsigned*)(bs + off) = this->green_y;
  off += sizeof(unsigned);
  
  if (bs != NULL)
    *(unsigned*)(bs + off) = this->blue_x;
  off += sizeof(unsigned);
  
  if (bs != NULL)
    *(unsigned*)(bs + off) = this->blue_y;
  off += sizeof(unsigned);
  
  if (bs != NULL)
    *(unsigned*)(bs + off) = this->white_x;
  off += sizeof(unsigned);
  
  if (bs != NULL)
    *(unsigned*)(bs + off) = this->white_y;
  off += sizeof(unsigned);
  
  n = strlen(this->name) + 1;
  if (bs != NULL)
    memcpy(bs + off, this->name, n);
  off += n;
  
  off += gamma_ramps_marshal(&(this->saved_ramps), bs ? bs + off : NULL, this->ramps_size);
  
  if (bs != NULL)
    *(size_t*)(bs + off) = this->table_size;
  off += sizeof(size_t);
  
  for (i = 0; i < this->table_size; i++)
    {
      off +=      filter_marshal(this->table_filters + i, bs ? bs + off : NULL, this->ramps_size);
      off += gamma_ramps_marshal(this->table_sums    + i, bs ? bs + off : NULL, this->ramps_size);
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
size_t output_unmarshal(struct output* restrict this, const void* restrict buf)
{
  size_t off = 0, i, n;
  const char* bs = buf;
  
  this->crtc = NULL;
  this->name = NULL;
  
  this->depth = *(const signed*)(bs + off);
  off += sizeof(signed);
  
  this->red_size = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  this->green_size = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  this->blue_size = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  this->ramps_size = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  this->supported = *(const enum libgamma_decision*)(bs + off);
  off += sizeof(enum libgamma_decision);
  
  this->colourspace = *(const enum colourspace*)(bs + off);
  off += sizeof(enum colourspace);
  
  this->name_is_edid = *(const int*)(bs + off);
  off += sizeof(int);
  
  this->red_x = *(const unsigned*)(bs + off);
  off += sizeof(unsigned);
  
  this->red_y = *(const unsigned*)(bs + off);
  off += sizeof(unsigned);
  
  this->green_x = *(const unsigned*)(bs + off);
  off += sizeof(unsigned);
  
  this->green_y = *(const unsigned*)(bs + off);
  off += sizeof(unsigned);
  
  this->blue_x = *(const unsigned*)(bs + off);
  off += sizeof(unsigned);
  
  this->blue_y = *(const unsigned*)(bs + off);
  off += sizeof(unsigned);
  
  this->white_x = *(const unsigned*)(bs + off);
  off += sizeof(unsigned);
  
  this->white_y = *(const unsigned*)(bs + off);
  off += sizeof(unsigned);
  
  n = strlen(bs + off) + 1;
  this->name = memdup(bs + off, n);
  if (this->name == NULL)
    return 0;
  
  this->saved_ramps.u8.red_size   = this->red_size;
  this->saved_ramps.u8.green_size = this->green_size;
  this->saved_ramps.u8.blue_size  = this->blue_size;
  
  off += n = gamma_ramps_unmarshal(&(this->saved_ramps), bs, this->ramps_size);
  COPY_RAMP_SIZES(&(this->saved_ramps.u8), this);
  if (n == 0)
    return 0;
  
  this->table_size = this->table_alloc = *(const size_t*)(bs + off);
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
      off += n = filter_unmarshal(this->table_filters + i, bs + off, this->ramps_size);
      if (n == 0)
	return 0;
      COPY_RAMP_SIZES(&(this->table_sums[i].u8), this);
      off += n = gamma_ramps_unmarshal(this->table_sums + i, bs + off, this->ramps_size);
      if (n == 0)
	return 0;
    }
  
  return off;
}


#if defined(__clang__)
# pragma GCC diagnostic pop
#endif



/**
 * Compare to outputs by the names of their respective CRTC:s
 * 
 * @param   a  Return -1 if this one is lower
 * @param   b  Return +1 if this one is higher
 * @return     See description of `a` and `b`,
 *             0 if returned if they are the same
 */
int output_cmp_by_name(const void* restrict a, const void* restrict b)
{
  const char* an = ((const struct output*)a)->name;
  const char* bn = ((const struct output*)b)->name;
  return strcmp(an, bn);
}


/**
 * Find an output by its name
 * 
 * @param   key   The name of the output
 * @param   base  The array of outputs
 * @param   n     The number of elements in `base`
 * @return        Output find in `base`, `NULL` if not found
 */
struct output* output_find_by_name(const char* restrict key, struct output* restrict base, size_t n)
{
  struct output k;

#if defined(__GNUC__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wcast-qual"
#endif
  k.name = (char*)key;
#if defined(__GNUC__)
# pragma GCC diagnostic pop
#endif
  
  return bsearch(&k, base, n, sizeof(*base), output_cmp_by_name);
}

