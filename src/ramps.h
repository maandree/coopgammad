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
#include <libgamma.h>



/**
 * Gamma ramps union for all
 * lbigamma gamma ramps types
 */
union gamma_ramps
{
  /**
   * Ramps with 8-bit value
   */
  libgamma_gamma_ramps8_t u8;
  
  /**
   * Ramps with 16-bit value
   */
  libgamma_gamma_ramps16_t u16;
  
  /**
   * Ramps with 32-bit value
   */
  libgamma_gamma_ramps32_t u32;
  
  /**
   * Ramps with 64-bit value
   */
  libgamma_gamma_ramps64_t u64;
  
  /**
   * Ramps with `float` value
   */
  libgamma_gamma_rampsf_t f;
  
  /**
   * Ramps with `double` value
   */
  libgamma_gamma_rampsd_t d;
  
};



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
size_t gamma_ramps_marshal(const union gamma_ramps* this, void* buf, size_t ramps_size);

/**
 * Unmarshal a ramp trio
 * 
 * @param   this        Output for the ramps
 * @param   buf         Buffer with the marshalled ramps
 * @param   ramps_size  The byte-size of ramps
 * @return              The number of unmarshalled bytes, 0 on error
 */
size_t gamma_ramps_unmarshal(union gamma_ramps* this, const void* buf, size_t ramps_size);

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
void apply(union gamma_ramps* dest, void* application, int depth, union gamma_ramps* base);

