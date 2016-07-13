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
#include "ramps.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>



/**
 * The name of the process
 */
extern char* argv0;



/**
 * Marshal a ramp trio
 * 
 * @param   this        The ramps
 * @param   buf         Output buffer for the marshalled ramps,
 *                      `NULL` just measure how large the buffers
 *                      needs to be
 * @param   ramps_size  The byte-size of ramps
 * @return              The number of marshalled byte
 */
size_t gamma_ramps_marshal(const union gamma_ramps* this, void* buf, size_t ramps_size)
{
  if (buf != NULL)
    memcpy(buf, this->u8.red, ramps_size);
  return ramps_size;
}


/**
 * Unmarshal a ramp trio
 * 
 * @param   this        Output for the ramps, `.red_size`, `.green_size`,
 *                      and `.blue_size` must already be set
 * @param   buf         Buffer with the marshalled ramps
 * @param   ramps_size  The byte-size of ramps
 * @return              The number of unmarshalled bytes, 0 on error
 */
size_t gamma_ramps_unmarshal(union gamma_ramps* this, const void* buf, size_t ramps_size)
{
  size_t depth = ramps_size / (this->u8.red_size + this->u8.green_size + this->u8.blue_size);
  int r = 0;
  switch (depth)
    {
    case 1:
      r = libgamma_gamma_ramps8_initialise(&(this->u8));
      break;
    case 2:
      r = libgamma_gamma_ramps16_initialise(&(this->u16));
      break;
    case 4:
      r = libgamma_gamma_ramps32_initialise(&(this->u32));
      break;
    case 8:
      r = libgamma_gamma_ramps64_initialise(&(this->u64));
      break;
    default:
      if (depth == sizeof(float))
	r = libgamma_gamma_rampsf_initialise(&(this->f));
      else if (depth == sizeof(double))
	r = libgamma_gamma_rampsd_initialise(&(this->d));
      else
	abort();
      break;
    }
  if (r)
    {
      libgamma_perror(argv0, r);
      errno = 0;
      return 0;
    }
  memcpy(this->u8.red, buf, ramps_size);
  return ramps_size;
}


/**
 * Apply a ramp-trio on top of another ramp-trio
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
void apply(union gamma_ramps* dest, void* application, int depth, union gamma_ramps* base)
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
  
  /* TODO apply with libclut */
}

