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
#include "output.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>



/**
 * The name of the process
 */
extern char* argv0;



/**
 * Free all resources allocated to an output.
 * The allocation of `output` itself is not freed,
 * nor is its the libgamma destroyed.
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
size_t output_marshal(const struct output* this, void* buf)
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
size_t output_unmarshal(struct output* this, const void* buf)
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
  
  n = strlen(bs + off) + 1;
  this->name = memdup(bs + off, n);
  if (this->name == NULL)
    return 0;
  
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


/**
 * Compare to outputs by the names of their respective CRTC:s
 * 
 * @param   a  Return -1 if this one is lower
 * @param   b  Return +1 if this one is higher
 * @return     See description of `a` and `b`,
 *             0 if returned if they are the same
 */
int output_cmp_by_name(const void* a, const void* b)
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
struct output* output_find_by_name(const char* key, struct output* base, size_t n)
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


/**
 * Remove a filter from an output
 * 
 * @param   out     The output
 * @param   filter  The filter
 * @return          The index of the filter, `out->table_size` if not found
 */
static ssize_t remove_filter(struct output* out, struct filter* filter)
{
  size_t i, n = out->table_size;
  
  for (i = 0; i < n; i++)
    if (!strcmp(filter->class, out->table_filters[i].class))
      break;
  
  if (i == out->table_size)
    return (ssize_t)(out->table_size);
  
  filter_destroy(out->table_filters + i);
  libgamma_gamma_ramps8_destroy(&(out->table_sums[i].u8));
  
  n = n - i - 1;
  memmove(out->table_filters + i, out->table_filters + i + 1, n * sizeof(*(out->table_filters)));
  memmove(out->table_sums    + i, out->table_sums    + i + 1, n * sizeof(*(out->table_sums)));
  out->table_size--;
  
  return (ssize_t)i;
}


/**
 * Add a filter to an output
 * 
 * @param   out     The output
 * @param   filter  The filter
 * @return          The index given to the filter, -1 on error
 */
ssize_t add_filter(struct output* out, struct filter* filter)
{
  size_t i, n = out->table_size;
  int r = -1;
  
  if (filter->lifespan == LIFESPAN_REMOVE)
    return remove_filter(out, filter);
  
  for (i = 0; i < n; i++)
    if (filter->priority > out->table_filters[i].priority)
      break;
  
  if (n == out->table_alloc)
    {
      void* new;
      
      new = realloc(out->table_filters, (n + 10) * sizeof(*(out->table_filters)));
      if (new == NULL)
	return -1;
      out->table_filters = new;
      
      new = realloc(out->table_sums, (n + 10) * sizeof(*(out->table_sums)));
      if (new == NULL)
	return -1;
      out->table_sums = new;
      
      out->table_alloc += 10;
    }
  
  memmove(out->table_filters + i + 1, out->table_filters + i, (n - i) * sizeof(*(out->table_filters)));
  memmove(out->table_sums    + i + 1, out->table_sums    + i, (n - i) * sizeof(*(out->table_sums)));
  out->table_size++;
  
  COPY_RAMP_SIZES(&(out->table_sums[i].u8), out);
  switch (out->depth)
    {
    case  8:  r = libgamma_gamma_ramps8_initialise(&(out->table_sums[i].u8));    break;
    case 16:  r = libgamma_gamma_ramps16_initialise(&(out->table_sums[i].u16));  break;
    case 32:  r = libgamma_gamma_ramps32_initialise(&(out->table_sums[i].u32));  break;
    case 64:  r = libgamma_gamma_ramps64_initialise(&(out->table_sums[i].u64));  break;
    case -1:  r = libgamma_gamma_rampsf_initialise(&(out->table_sums[i].f));     break;
    case -2:  r = libgamma_gamma_rampsd_initialise(&(out->table_sums[i].d));     break;
    default:
      abort();
    }
  if (r < 0)
    return -1;
  
  out->table_filters[i] = *filter;
  
  return (ssize_t)i;
}


/**
 * Recalculate the resulting gamma and
 * update push the new gamma ramps to the CRTC
 * 
 * @param   output         The output
 * @param   first_updated  The index of the first added or removed filter
 * @return                 Zero on success, -1 on error
 */
int flush_filters(struct output* output, size_t first_updated)
{
  union gamma_ramps plain;
  union gamma_ramps* last;
  size_t i;
  int r = 0;
  
  if (first_updated == 0)
    {
      if (make_plain_ramps(&plain, output) < 0)
	return -1;
      last = &plain;
    }
  else
    last = output->table_sums + (first_updated - 1);
  
  for (i = first_updated; i < output->table_size; i++)
    {
      apply(output->table_sums + i, output->table_filters[i].ramps, output->depth, last);
      last = output->table_sums + i;
    }
  
  switch (output->depth)
    {
    case  8:  r = libgamma_crtc_set_gamma_ramps8(output->crtc,  last->u8);   break;
    case 16:  r = libgamma_crtc_set_gamma_ramps16(output->crtc, last->u16);  break;
    case 32:  r = libgamma_crtc_set_gamma_ramps32(output->crtc, last->u32);  break;
    case 64:  r = libgamma_crtc_set_gamma_ramps64(output->crtc, last->u64);  break;
    case -1:  r = libgamma_crtc_set_gamma_rampsf(output->crtc,  last->f);    break;
    case -2:  r = libgamma_crtc_set_gamma_rampsd(output->crtc,  last->d);    break;
    default:
      abort();
    }
  if (r)
    libgamma_perror(argv0, r); /* Not fatal */
  
  if (first_updated == 0)
    libgamma_gamma_ramps8_destroy(&(plain.u8));
  
  return 0;
}


/**
 * Make identity mapping ramps
 * 
 * @param   ramps   Output parameter for the ramps
 * @param   output  The output for which the ramps shall be configured
 * @return          Zero on success, -1 on error
 */
int make_plain_ramps(union gamma_ramps* ramps, struct output* output)
{
  COPY_RAMP_SIZES(&(ramps->u8), output);
  switch (output->depth)
    {
    case 8:
      if (libgamma_gamma_ramps8_initialise(&(ramps->u8)))
	return -1;
      break;
    case 16:
      if (libgamma_gamma_ramps16_initialise(&(ramps->u16)))
	return -1;
      break;
    case 32:
      if (libgamma_gamma_ramps32_initialise(&(ramps->u32)))
	return -1;
      break;
    case 64:
      if (libgamma_gamma_ramps64_initialise(&(ramps->u64)))
	return -1;
      break;
    case -1:
      if (libgamma_gamma_rampsf_initialise(&(ramps->f)))
	return -1;
      break;
    case -2:
      if (libgamma_gamma_rampsd_initialise(&(ramps->d)))
	return -1;
      break;
    default:
      abort();
    }
  /* TODO fill ramps (use libclut) */
  return 0;
}

