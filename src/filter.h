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
#include <stdint.h>



/**
 * The lifespan of a filter
 */
enum lifespan
{
  /**
   * The filter should be applied
   * until it is explicitly removed
   */
  LIFESPAN_UNTIL_REMOVAL,
  
  /**
   * The filter should be applied
   * until the client exists
   */
  LIFESPAN_UNTIL_DEATH,
  
  /**
   * The filter should be removed now
   */
  LIFESPAN_REMOVE
};


/**
 * Information about a filter
 */
struct filter
{
  /**
   * The client that applied it. This need not be
   * set unless `.lifespan == LIFESPAN_UNTIL_DEATH`.
   * This is the file descriptor of the client's
   * connection.
   */
  int client;
  
  /**
   * Identifier for the CRTC to which this filter
   * should be applied. May be removed once applied.
   */
  char* crtc;
  
  /**
   * The priority of the filter
   */
  int64_t priority;
  
  /**
   * Identifier for the filter
   */
  char* class;
  
  /**
   * The lifespan of the filter
   */
  enum lifespan lifespan;
  
  /**
   * The gamma ramp adjustments for the filter.
   * This is raw binary data. `NULL` iff
   * `lifespan == LIFESPAN_REMOVE`.
   */
  void* ramps;
};



/**
 * Free all resources allocated to a filter.
 * The allocation of `filter` itself is not freed.
 * 
 * @param  filter  The filter.
 */
void filter_destroy(struct filter* filter);

/**
 * Marshal a filter.
 * 
 * @param   filter      The filter.
 * @param   buf         Output buffer for the marshalled filter.
 *                      `NULL` just measure how large the buffers
 *                      needs to be.
 * @param   ramps_size  The byte-size of `filter->ramps`
 * @return              The number of marshalled byte
 */
size_t filter_marshal(const struct filter* filter, char* buf, size_t ramps_size);

/**
 * Unmarshal a filter.
 * 
 * @param   filter      Output for the filter, `NULL` to skip unmarshalling
 * @param   buf         Buffer with the marshalled filter
 * @param   ramps_size  The byte-size of `filter->ramps`
 * @return              The number of unmarshalled bytes, 0 on error
 */
size_t filter_unmarshal(struct filter* filter, const char* buf, size_t ramps_size);

