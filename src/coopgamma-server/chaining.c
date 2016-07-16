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
#include "chaining.h"

#include <libclut.h>

#include <stdio.h>
#include <stdlib.h>



/**
 * The name of the process
 */
extern char* restrict argv0;



/**
 * Apply a filter on top of another filter
 * 
 * @param  dest         The output for the resulting ramp-trio, must be initialised
 * @param  application  The red, green and blue ramps, as one single raw array,
 *                      of the filter that should be applied
 * @param  depth        -1: `float` stops
 *                      -2: `double` stops
 *                      Other: the number of bits of each (integral) stop
 * @param  base         The CLUT on top of which the new filter should be applied,
 *                      this can be the same pointer as `dest`
 */
void apply_filter(union gamma_ramps* restrict dest, void* restrict application,
		  int depth, union gamma_ramps* restrict base)
{
  union gamma_ramps app;
  size_t bytedepth;
  size_t red_width, green_width, blue_width;
  
  if (depth == -1)
    bytedepth = sizeof(float);
  else if (depth == -2)
    bytedepth = sizeof(double);
  else
    bytedepth = (size_t)depth / 8;
  
  red_width   = (app.u8.red_size   = base->u8.red_size)   * bytedepth;
  green_width = (app.u8.green_size = base->u8.green_size) * bytedepth;
  blue_width  = (app.u8.blue_size  = base->u8.blue_size)  * bytedepth;
  
  app.u8.red   = application;
  app.u8.green = app.u8.red   + red_width;
  app.u8.blue  = app.u8.green + green_width;
  
  if (dest != base)
    {
      memcpy(dest->u8.red,   base->u8.red,   red_width);
      memcpy(dest->u8.green, base->u8.green, green_width);
      memcpy(dest->u8.blue,  base->u8.blue,  blue_width);
    }
  
  switch (depth)
    {
    case 8:
      libclut_apply(&(dest->u8), UINT8_MAX, uint8_t, &(app.u8), UINT8_MAX, uint8_t, 1, 1, 1);
      break;
    case 16:
      libclut_apply(&(dest->u16), UINT16_MAX, uint16_t, &(app.u16), UINT16_MAX, uint16_t, 1, 1, 1);
      break;
    case 32:
      libclut_apply(&(dest->u32), UINT32_MAX, uint32_t, &(app.u32), UINT32_MAX, uint32_t, 1, 1, 1);
      break;
    case 64:
      libclut_apply(&(dest->u64), UINT64_MAX, uint64_t, &(app.u64), UINT64_MAX, uint64_t, 1, 1, 1);
      break;
    case -1:
      libclut_apply(&(dest->f), 1.0f, float, &(app.d), 1.0f, float, 1, 1, 1);
      break;
    case -2:
      libclut_apply(&(dest->d), (double)1, double, &(app.f), (double)1, double, 1, 1, 1);
      break;
    default:
      abort();
    }
}



/**
 * Remove a filter from an output
 * 
 * @param   out     The output
 * @param   filter  The filter
 * @return          The index of the filter, `out->table_size` if not found
 */
static ssize_t remove_filter(struct output* restrict out, struct filter* restrict filter)
{
  size_t i, n = out->table_size;
  
  for (i = 0; i < n; i++)
    if (!strcmp(filter->class, out->table_filters[i].class))
      break;
  
  if (i == out->table_size)
    {
      fprintf(stderr, "%s: ignoring attempt to removing non-existing filter on CRTC %s: %s",
	      argv0, out->name, filter->class);
      return (ssize_t)(out->table_size);
    }
  
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
ssize_t add_filter(struct output* restrict out, struct filter* restrict filter)
{
  size_t i, n = out->table_size;
  int r = -1;
  
  /* Remove? */
  if (filter->lifespan == LIFESPAN_REMOVE)
    return remove_filter(out, filter);
  
  /* Update? */
  for (i = 0; i < n; i++)
    if (!strcmp(filter->class, out->table_filters[i].class))
      break;
  if (i != n)
    {
      filter_destroy(out->table_filters + i);
      out->table_filters[i] = *filter;
      return (ssize_t)i;
    }
  
  /* Add! */
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
 * Make identity mapping ramps
 * 
 * @param   ramps   Output parameter for the ramps
 * @param   output  The output for which the ramps shall be configured
 * @return          Zero on success, -1 on error
 */
int make_plain_ramps(union gamma_ramps* restrict ramps, struct output* restrict output)
{
  COPY_RAMP_SIZES(&(ramps->u8), output);
  switch (output->depth)
    {
    case 8:
      if (libgamma_gamma_ramps8_initialise(&(ramps->u8)))
	return -1;
      libclut_start_over(&(ramps->u8), UINT8_MAX, uint8_t, 1, 1, 1);
      break;
    case 16:
      if (libgamma_gamma_ramps16_initialise(&(ramps->u16)))
	return -1;
      libclut_start_over(&(ramps->u16), UINT16_MAX, uint16_t, 1, 1, 1);
      break;
    case 32:
      if (libgamma_gamma_ramps32_initialise(&(ramps->u32)))
	return -1;
      libclut_start_over(&(ramps->u32), UINT32_MAX, uint32_t, 1, 1, 1);
      break;
    case 64:
      if (libgamma_gamma_ramps64_initialise(&(ramps->u64)))
	return -1;
      libclut_start_over(&(ramps->u64), UINT64_MAX, uint64_t, 1, 1, 1);
      break;
    case -1:
      if (libgamma_gamma_rampsf_initialise(&(ramps->f)))
	return -1;
      libclut_start_over(&(ramps->f), 1.0f, float, 1, 1, 1);
      break;
    case -2:
      if (libgamma_gamma_rampsd_initialise(&(ramps->d)))
	return -1;
      libclut_start_over(&(ramps->d), (double)1, double, 1, 1, 1);
      break;
    default:
      abort();
    }
  return 0;
}

