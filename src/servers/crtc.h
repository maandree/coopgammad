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
#ifndef SERVERS_CRTC_H
#define SERVERS_CRTC_H


#include "../types/output.h"

#include <libgamma.h>



#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...)  __VA_ARGS__
# else
#  define GCC_ONLY(...)  /* nothing */
# endif
#endif



/**
 * Handle a ‘Command: enumerate-crtcs’ message
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @return              Zero on success (even if ignored), -1 on error,
 *                      1 if connection closed
 */
GCC_ONLY(__attribute__((nonnull)))
int handle_enumerate_crtcs(size_t conn, const char* restrict message_id);

/**
 * Get the name of a CRTC
 * 
 * @param   info  Information about the CRTC
 * @param   crtc  libgamma's state for the CRTC
 * @return        The name of the CRTC, `NULL` on error
 */
GCC_ONLY(__attribute__((nonnull)))
char* get_crtc_name(const libgamma_crtc_information_t* restrict info,
		    const libgamma_crtc_state_t* restrict crtc);

/**
 * Get partitions and CRTC:s
 * 
 * @return   Zero on success, -1 on error
 */
int initialise_crtcs(void);

/**
 * Merge the new state with an old state
 * 
 * @param   old_outputs    The old `outputs`
 * @param   old_outputs_n  The old `outputs_n`
 * @return                 Zero on success, -1 on error
 */
int merge_state(struct output* restrict old_outputs, size_t old_outputs_n);


#endif

