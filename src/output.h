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
#include <stddef.h>

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
 * Information about an output
 */
struct output
{
  /**
   * -2: double
   * -1: float
   *  8: uint8_t
   * 16: uint16_t
   * 32: uint32_t
   * 64: uint64_t
   */
  signed depth;
  
  /**
   * The number of stops in the red gamma ramp
   */
  size_t red_size;
  
  /**
   * The number of stops in the red gamma ramp
   */
  size_t green_size;
  
  /**
   * The number of stops in the red gamma ramp
   */
  size_t blue_size;
  
  /**
   * Whether gamma ramps are supported
   */
  enum libgamma_decision supported;
  
  /**
   * The name of the output, will be its EDID
   * if available, otherwise it will be the
   * index of the partition, followed by a dot
   * and the index of the CRTC within the
   * partition, or if a name for the connector
   * is available: the index of the partition
   * followed by a dot and the name of the
   * connector
   */
  char* name;
  
  /**
   * The libgamma state for the output
   */
  libgamma_crtc_state_t* crtc;
  
  /**
   * Saved gamma ramps
   */
  union gamma_ramps saved_ramps;
};

