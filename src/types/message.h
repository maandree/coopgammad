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
#ifndef TYPES_MESSAGE_H
#define TYPES_MESSAGE_H


#include <stddef.h>
#include <limits.h>



#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...)  __VA_ARGS__
# else
#  define GCC_ONLY(...)  /* nothing */
# endif
#endif



/**
 * Message passed between a server and a client
 */
struct message
{
  /**
   * The headers in the message, each element in this list
   * as an unparsed header, it consists of both the header
   * name and its associated value, joined by ": ". A header
   * cannot be `NULL` (unless its memory allocation failed,)
   * but `headers` itself is `NULL` if there are no headers.
   * The "Length" header should be included in this list.
   */
  char** restrict headers;
  
  /**
   * The number of headers in the message
   */
  size_t header_count;
  
  /**
   * The payload of the message, `NULL` if none (of zero-length)
   */
  char* restrict payload;
  
  /**
   * The size of the payload
   */
  size_t payload_size;
  
  /**
   * How much of the payload that has been stored (internal data)
   */
  size_t payload_ptr;
  
  /**
   * Internal buffer for the reading function (internal data)
   */
  char* restrict buffer;
  
  /**
   * The size allocated to `buffer` (internal data)
   */
  size_t buffer_size;
  
  /**
   * The number of bytes used in `buffer` (internal data)
   */
  size_t buffer_ptr;
  
  /**
   * 0 while reading headers, 1 while reading payload, and 2 when done (internal data)
   */
  int stage;
  
#if INT_MAX != LONG_MAX
  int padding__;
#endif
  
};



/**
 * Initialise a message slot so that it can
 * be used by to read messages
 * 
 * @param   this  Memory slot in which to store the new message
 * @return        Non-zero on error, `errno` will be set accordingly
 */
GCC_ONLY(__attribute__((nonnull)))
int message_initialise(struct message* restrict this);

/**
 * Release all resources in a message, should
 * be done even if initialisation fails
 * 
 * @param  this  The message
 */
GCC_ONLY(__attribute__((nonnull)))
void message_destroy(struct message* restrict this);

/**
 * Marshal a message for state serialisation
 * 
 * @param  this  The message
 * @param  buf   Output buffer for the marshalled data,
 *               `NULL` just measure how large the buffers
 *               needs to be
 * @return       The number of marshalled byte
 */
GCC_ONLY(__attribute__((nonnull(1))))
size_t message_marshal(const struct message* restrict this, void* restrict buf);

/**
 * Unmarshal a message for state deserialisation
 * 
 * @param   this  Memory slot in which to store the new message
 * @param   buf   In buffer with the marshalled data
 * @return        The number of unmarshalled bytes, 0 on error
 */
GCC_ONLY(__attribute__((nonnull)))
size_t message_unmarshal(struct message* restrict this, const void* restrict buf);

/**
 * Read the next message from a file descriptor
 * 
 * @param   this  Memory slot in which to store the new message
 * @param   fd    The file descriptor
 * @return        0:  At least one message is available
 *                -1: Exceptional connection:
 *                  EINTR:        System call interrupted
 *                  EAGAIN:       No message is available
 *                  EWOULDBLOCK:  No message is available
 *                  ECONNRESET:   Connection closed
 *                  Other:        Failure
 *                -2: Corrupt message (unrecoverable)
 */
GCC_ONLY(__attribute__((nonnull)))
int message_read(struct message* restrict this, int fd);


#endif

