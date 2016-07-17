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
#ifndef SERVERS_KERNEL_H
#define SERVERS_KERNEL_H


#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...)  __VA_ARGS__
# else
#  define GCC_ONLY(...)  /* nothing */
# endif
#endif



/**
 * Get the pathname of the socket
 * 
 * @return  The pathname of the socket, `NULL` on error
 */
GCC_ONLY(__attribute__((malloc)))
char* get_socket_pathname(void);

/**
 * Get the pathname of the PID file
 * 
 * @return  The pathname of the PID file, `NULL` on error
 */
GCC_ONLY(__attribute__((malloc)))
char* get_pidfile_pathname(void);

/**
 * Get the pathname of the state file
 * 
 * @return  The pathname of the state file, `NULL` on error
 */
GCC_ONLY(__attribute__((malloc)))
char* get_state_pathname(void);

/**
 * Create PID file
 * 
 * @param   pidpath  The pathname of the PID file
 * @return           Zero on success, -1 on error,
 *                   -2 if the service is already running
 */
GCC_ONLY(__attribute__((nonnull)))
int create_pidfile(char* pidpath);

/**
 * Create socket and start listening
 * 
 * @param   socketpath  The pathname of the socket
 * @return              Zero on success, -1 on error
 */
GCC_ONLY(__attribute__((nonnull)))
int create_socket(const char* socketpath);

/**
 * Close and unlink the socket
 * 
 * @param  socketpath  The pathname of the socket
 */
GCC_ONLY(__attribute__((nonnull)))
void close_socket(const char* socketpath);


#endif

