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
#ifndef COMMUNICATION_H
#define COMMUNICATION_H


#include <stdio.h>
#include <stdlib.h>



#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...)  __VA_ARGS__
# else
#  define GCC_ONLY(...)  /* nothing */
# endif
#endif



/**
 * Construct a message
 * 
 * @param  bufp:char**            Output parameter for the buffer, must not have side-effects
 * @param  np:size_t*             Output parameter for the size of the buffer sans `extra`
 * @param  extra:size_t           The extra number for bytes to allocate to the buffer
 * @param  format:string-literal  Message format string
 * @param  ...                    Message formatting arguments
 */
#define MAKE_MESSAGE(bufp, np, extra, format, ...)		\
  do								\
    {								\
      ssize_t m__;						\
      snprintf(NULL, 0, format "%zn", __VA_ARGS__, &m__);	\
      *(bufp) = malloc((size_t)(extra) + (size_t)m__);		\
      if (*(bufp) == NULL)					\
	return -1;						\
      sprintf(*(bufp), format, __VA_ARGS__);			\
      *(np) = (size_t)m__;					\
    }								\
  while (0)

/**
 * Send a custom error without an error number
 * 
 * @param   ...  The error description to send
 * @return       1: Client disconnected
 *               0: Success (possibily delayed)
 *               -1: An error occurred
 */
#define send_error(...)  ((send_error)(conn, message_id, __VA_ARGS__))

/**
 * Send a standard error
 * 
 * @param   ...  The value of `errno`, 0 to indicate success
 * @return       1: Client disconnected
 *               0: Success (possibily delayed)
 *               -1: An error occurred
 */
#define send_errno(...)  ((send_errno)(conn, message_id, __VA_ARGS__))



/**
 * Send a message
 * 
 * @param   conn  The index of the connection
 * @param   buf   The data to send
 * @param   n     The size of `buf`
 * @return        Zero on success, -1 on error, 1 if disconncted
 *                EINTR, EAGAIN, EWOULDBLOCK, and ECONNRESET count
 *                as success (ECONNRESET cause 1 to be returned),
 *                and are handled appropriately.
 */
int send_message(size_t conn, char* restrict buf, size_t n);

/**
 * Send a custom error without an error number
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The ID of the message to which this message is a response
 * @param   desc        The error description to send
 * @return              1: Client disconnected
 *                      0: Success (possibily delayed)
 *                      -1: An error occurred
 */
GCC_ONLY(__attribute__((nonnull)))
int (send_error)(size_t conn, const char* restrict message_id, const char* restrict desc);

/**
 * Send a standard error
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The ID of the message to which this message is a response
 * @param   number      The value of `errno`, 0 to indicate success
 * @return              1: Client disconnected
 *                      0: Success (possibily delayed)
 *                      -1: An error occurred
 */
GCC_ONLY(__attribute__((nonnull)))
int (send_errno)(size_t conn, const char* restrict message_id, int number);

/**
 * Continue sending the queued messages
 * 
 * @param   conn  The index of the connection
 * @return        Zero on success, -1 on error, 1 if disconncted
 *                EINTR, EAGAIN, EWOULDBLOCK, and ECONNRESET count
 *                as success (ECONNRESET cause 1 to be returned),
 *                and are handled appropriately.
 */
static inline int continue_send(size_t conn)
{
  return send_message(conn, NULL, 0);
}



#endif

