/* See LICENSE file for copyright and license details. */
#ifndef SERVERS_KERNEL_H
#define SERVERS_KERNEL_H

#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...) __VA_ARGS__
# else
#  define GCC_ONLY(...) /* nothing */
# endif
#endif

/**
 * Get the pathname of the socket
 * 
 * @return  The pathname of the socket, `NULL` on error
 */
GCC_ONLY(__attribute__((__malloc__)))
char *get_socket_pathname(void);

/**
 * Get the pathname of the PID file
 * 
 * @return  The pathname of the PID file, `NULL` on error
 */
GCC_ONLY(__attribute__((__malloc__)))
char *get_pidfile_pathname(void);

/**
 * Get the pathname of the state file
 * 
 * @return  The pathname of the state file, `NULL` on error
 */
GCC_ONLY(__attribute__((__malloc__)))
char *get_state_pathname(void);

/**
 * Create PID file
 * 
 * @param   pidpath  The pathname of the PID file
 * @return           Zero on success, -1 on error,
 *                   -2 if the service is already running
 */
GCC_ONLY(__attribute__((__nonnull__)))
int create_pidfile(char *pidpath);

/**
 * Create socket and start listening
 * 
 * @param   socketpath  The pathname of the socket
 * @return              Zero on success, -1 on error
 */
GCC_ONLY(__attribute__((__nonnull__)))
int create_socket(const char *socketpath);

/**
 * Close and unlink the socket
 * 
 * @param  socketpath  The pathname of the socket
 */
GCC_ONLY(__attribute__((__nonnull__)))
void close_socket(const char *socketpath);

#endif
