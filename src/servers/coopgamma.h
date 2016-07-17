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
#ifndef SERVERS_COOPGAMMA_H
#define SERVERS_COOPGAMMA_H


#include "../types/output.h"

#include <stddef.h>



#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...)  __VA_ARGS__
# else
#  define GCC_ONLY(...)  /* nothing */
# endif
#endif



/**
 * Handle a closed connection
 * 
 * @param   client  The file descriptor for the client
 * @return          Zero on success, -1 on error
 */
int connection_closed(int client);

/**
 * Handle a ‘Command: get-gamma’ message
 * 
 * @param   conn           The index of the connection
 * @param   message_id     The value of the ‘Message ID’ header
 * @param   crtc           The value of the ‘CRTC’ header
 * @param   coalesce       The value of the ‘Coalesce’ header
 * @param   high_priority  The value of the ‘High priority’ header
 * @param   low_priority   The value of the ‘Low priority’ header
 * @return                 Zero on success (even if ignored), -1 on error,
 *                         1 if connection closed
 */
GCC_ONLY(__attribute__((nonnull(2))))
int handle_get_gamma(size_t conn, const char* restrict message_id, const char* restrict crtc,
		     const char* restrict coalesce, const char* restrict high_priority,
		     const char* restrict low_priority);

/**
 * Handle a ‘Command: set-gamma’ message
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @param   crtc        The value of the ‘CRTC’ header
 * @param   priority    The value of the ‘Priority’ header
 * @param   class       The value of the ‘Class’ header
 * @param   lifespan    The value of the ‘Lifespan’ header
 * @return              Zero on success (even if ignored), -1 on error,
 *                      1 if connection closed
 */
GCC_ONLY(__attribute__((nonnull(2))))
int handle_set_gamma(size_t conn, const char* restrict message_id, const char* restrict crtc,
		     const char* restrict priority, const char* restrict class, const char* restrict lifespan);


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
 * Preserve current gamma ramps at priority 0 for all outputs
 * 
 * @return  Zero on success, -1 on error
 */
int preserve_gamma(void);


#endif

