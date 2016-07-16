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
#ifndef STATE_H
#define STATE_H


#include "types/message.h"
#include "types/ring.h"
#include "types/output.h"

#include <libgamma.h>

#include <stddef.h>
#include <signal.h>



#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...)  __VA_ARGS__
# else
#  define GCC_ONLY(...)  /* nothing */
# endif
#endif



/**
 * The name of the process
 */
extern char* restrict argv0;

/**
 * The real pathname of the process's binary,
 * `NULL` if `argv0` is satisfactory
 */
extern char* restrict argv0_real;

/**
 * Array of all outputs
 */
extern struct output* restrict outputs;

/**
 * The nubmer of elements in `outputs`
 */
extern size_t outputs_n;

/**
 * The server socket's file descriptor
 */
extern int socketfd;

/**
 * Has the process receive a signal
 * telling it to re-execute?
 */
extern volatile sig_atomic_t reexec;

/**
 * Has the process receive a signal
 * telling it to terminate?
 */
extern volatile sig_atomic_t terminate;

/**
 * Has the process receive a to
 * disconnect from or reconnect to
 * the site? 1 if disconnct, 2 if
 * reconnect, 0 otherwise.
 */
extern volatile sig_atomic_t connection;

/**
 * List of all client's file descriptors
 * 
 * Unused slots, with index less than `connections_used`,
 * should have the value -1 (negative)
 */
extern int* restrict connections;

/**
 * The number of elements allocated for `connections`
 */
extern size_t connections_alloc;

/**
 * The index of the first unused slot in `connections`
 */
extern size_t connections_ptr;

/**
 * The index of the last used slot in `connections`, plus 1
 */
extern size_t connections_used;

/**
 * The clients' connections' inbound-message buffers
 */
extern struct message* restrict inbound;

/**
 * The clients' connections' outbound-message buffers
 */
extern struct ring* restrict outbound;

/**
 * Is the server connect to the display?
 * 
 * Set to true before the initial connection
 */
extern int connected;

/**
 * The adjustment method, -1 for automatic
 */
extern int method;

/**
 * The site's name, may be `NULL`
 */
extern char* restrict sitename;

/**
 * The libgamma site state
 */
extern libgamma_site_state_t site;

/**
 * The libgamma partition states
 */
extern libgamma_partition_state_t* restrict partitions;

/**
 * The libgamma CRTC states
 */
extern libgamma_crtc_state_t* restrict crtcs;



/**
 * Destroy the state
 */
void state_destroy(void);

/**
 * Marshal the state
 * 
 * @param   buf  Output buffer for the marshalled data,
 *               `NULL` to only measure how many bytes
 *               this buffer needs
 * @return       The number of marshalled bytes
 */
size_t state_marshal(void* restrict buf);

/**
 * Unmarshal the state
 * 
 * @param   buf  Buffer for the marshalled data
 * @return       The number of unmarshalled bytes, 0 on error
 */
GCC_ONLY(__attribute__((nonnull)))
size_t state_unmarshal(const void* restrict buf);


#endif

