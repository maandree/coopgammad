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
#include <stddef.h>

#include <libgamma.h>

#include "ramps.h"
#include "filter.h"



#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...)  __VA_ARGS__
# else
#  define GCC_ONLY(...)  /* nothing */
# endif
#endif



/**
 * Copy the ramp sizes
 * 
 * This macro supports both `struct output`
 * and `struct gamma_ramps`
 * 
 * @param  dest  The destination
 * @param  src   The source
 */
#define COPY_RAMP_SIZES(dest, src)         \
  ((dest)->red_size   = (src)->red_size,   \
   (dest)->green_size = (src)->green_size, \
   (dest)->blue_size  = (src)->blue_size)



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
   * Whether gamma ramps are supported
   */
  enum libgamma_decision supported;
  
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
   * The name of the output, will be its EDID
   * if available, otherwise it will be the
   * index of the partition, followed by a dot
   * and the index of the CRTC within the
   * partition, or if a name for the connector
   * is available: the index of the partition
   * followed by a dot and the name of the
   * connector
   */
  char* restrict name;
  
  /**
   * The libgamma state for the output
   */
  libgamma_crtc_state_t* restrict crtc;
  
  /**
   * Saved gamma ramps
   */
  union gamma_ramps saved_ramps;
  
  /**
   * The table of all applied filters
   */
  struct filter* restrict table_filters;
  
  /**
   * `.table_sums[i]` is the resulting
   * adjustment made when all filter
   * from `.table_filters[0]` up to and
   * including `.table_filters[i]` has
   * been applied
   */
  union gamma_ramps* restrict table_sums;
  
  /**
   * The number of elements allocated
   * for `.table_filters` and for `.table_sums`
   */
  size_t table_alloc;
  
  /**
   * The number of elements stored in
   * `.table_filters` and in `.table_sums`
   */
  size_t table_size;
  
};



/**
 * Free all resources allocated to an output.
 * The allocation of `output` itself is not freed,
 * nor is its the libgamma destroyed.
 * 
 * @param  this  The output
 */
GCC_ONLY(__attribute__((nonnull)))
void output_destroy(struct output* restrict this);

/**
 * Marshal an output
 * 
 * @param   this  The output
 * @param   buf   Output buffer for the marshalled output,
 *                `NULL` just measure how large the buffers
 *                needs to be
 * @return        The number of marshalled byte
 */
GCC_ONLY(__attribute__((nonnull(1))))
size_t output_marshal(const struct output* restrict this, void* restrict buf);

/**
 * Unmarshal an output
 * 
 * @param   this  Output for the output
 * @param   buf   Buffer with the marshalled output
 * @return        The number of unmarshalled bytes, 0 on error
 */
GCC_ONLY(__attribute__((nonnull)))
size_t output_unmarshal(struct output* restrict this, const void* restrict buf);

/**
 * Compare to outputs by the names of their respective CRTC:s
 * 
 * @param   a  Return -1 if this one is lower
 * @param   b  Return +1 if this one is higher
 * @return     See description of `a` and `b`,
 *             0 if returned if they are the same
 */
GCC_ONLY(__attribute__((pure, nonnull)))
int output_cmp_by_name(const void* restrict a, const void* restrict b);

/**
 * Find an output by its name
 * 
 * @param   key   The name of the output
 * @param   base  The array of outputs
 * @param   n     The number of elements in `base`
 * @return        Output find in `base`, `NULL` if not found
 */
GCC_ONLY(__attribute__((pure, nonnull)))
struct output* output_find_by_name(const char* restrict key, struct output* restrict base, size_t n);

/**
 * Add a filter to an output
 * 
 * @param   output  The output
 * @param   filter  The filter
 * @return          The index given to the filter, -1 on error
 */
GCC_ONLY(__attribute__((nonnull)))
ssize_t add_filter(struct output* restrict output, struct filter* restrict filter);

/**
 * Recalculate the resulting gamma and
 * update push the new gamma ramps to the CRTC
 * 
 * @param   output         The output
 * @param   first_updated  The index of the first added or removed filter
 * @return                 Zero on success, -1 on error
 */
GCC_ONLY(__attribute__((nonnull)))
int flush_filters(struct output* restrict output, size_t first_updated);

/**
 * Make identity mapping ramps
 * 
 * @param   ramps   Output parameter for the ramps
 * @param   output  The output for which the ramps shall be configured
 * @return          Zero on success, -1 on error
 */
GCC_ONLY(__attribute__((nonnull)))
int make_plain_ramps(union gamma_ramps* restrict ramps, struct output* restrict output);

