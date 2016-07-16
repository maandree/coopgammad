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
#ifndef COOPGAMMA_SERVER_CHAINING
#define COOPGAMMA_SERVER_CHAINING


#include "../types/output.h"



#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...)  __VA_ARGS__
# else
#  define GCC_ONLY(...)  /* nothing */
# endif
#endif



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
GCC_ONLY(__attribute__((nonnull)))
void apply_filter(union gamma_ramps* restrict dest, void* restrict application,
		  int depth, union gamma_ramps* restrict base);


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
 * Make identity mapping ramps
 * 
 * @param   ramps   Output parameter for the ramps
 * @param   output  The output for which the ramps shall be configured
 * @return          Zero on success, -1 on error
 */
GCC_ONLY(__attribute__((nonnull)))
int make_plain_ramps(union gamma_ramps* restrict ramps, struct output* restrict output);


#endif

