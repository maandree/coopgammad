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

#include "ramps.h"



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
   * The number of stops in the green gamma ramp
   */
  size_t green_size;
  
  /**
   * The number of stops in the blue gamma ramp
   */
  size_t blue_size;
  
  /**
   * `.red_size + .green_size + .blue_size`
   * multiplied by the byte-size of each stop
   */
  size_t ramps_size;
  
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

