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
#ifndef GAMMA_SERVER_SERVER_H
#define GAMMA_SERVER_SERVER_H


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
 * Handle a ‘Command: set-gamma’ message
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @param   crtc        The value of the ‘CRTC’ header
 * @return              Zero on success (even if ignored), -1 on error,
 *                      1 if connection closed
 */
GCC_ONLY(__attribute__((nonnull(2))))
int handle_get_gamma_info(size_t conn, const char* restrict message_id, const char* restrict crtc);

/**
 * Set the gamma ramps on an output
 * 
 * @param  output  The output
 * @param  ramps   The gamma ramps
 */
GCC_ONLY(__attribute__((nonnull)))
void set_gamma(const struct output* restrict output, const union gamma_ramps* restrict ramps);

/**
 * Disconnect from the site
 * 
 * @return  Zero on success, -1 on error
 */
int disconnect(void);

/**
 * Reconnect to the site
 * 
 * @return  Zero on success, -1 on error
 */
int reconnect(void);


#endif

