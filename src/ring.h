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
#include <stddef.h>



/**
 * Ring buffer
 */
struct ring
{
  /**
   * Buffer for the data
   */
  char* buffer;
  
  /**
   * The first set byte in `.buffer`
   */
  size_t start;
  
  /**
   * The last set byte in `.buffer`, plus 1
   */
  size_t end;
  
  /**
   * The size of `.buffer`
   */
  size_t size;
};



/**
 * Initialise a ring buffer
 * 
 * @param  this  The ring buffer
 */
void ring_initialise(struct ring* this);

/**
 * Release resource allocated to a ring buffer
 * 
 * @param  this  The ring buffer
 */
void ring_destroy(struct ring* this);

/**
 * Marshal a ring buffer
 * 
 * @param   this  The ring buffer
 * @param   buf   Output buffer for the marshalled data,
 *                `NULL` to only measure how large buffer
 *                is needed
 * @return        The number of marshalled bytes
 */
size_t ring_marshal(const struct ring* this, void* buf);

/**
 * Unmarshal a ring buffer
 * 
 * @param   this  Output parameter for the ring buffer
 * @param   buf   Buffer with the marshalled data
 * @return        The number of unmarshalled bytes, 0 on error
 */
size_t ring_unmarshal(struct ring* this, const void* buf);

/**
 * Append data to a ring buffer
 * 
 * @param   this  The ring buffer
 * @param   data  The new data
 * @param   n     The number of bytes in `data`
 * @return        Zero on success, -1 on error
 */
int ring_push(struct ring* this, void* data, size_t n);

/**
 * Get queued data from a ring buffer
 * 
 * It can take up to two calls (with `ring_pop` between)
 * to get all queued data
 * 
 * @param   this  The ring buffer
 * @param   n     Output parameter for the length
 *                of the returned segment
 * @return        The beginning of the queued data,
 *                `NULL` if there is nothing more
 */
void* ring_peek(struct ring* this, size_t* n);

/**
 * Dequeue data from a ring bubber
 * 
 * @param  this  The ring buffer
 * @param  n     The number of bytes to dequeue
 */
void ring_pop(struct ring* this, size_t n);

