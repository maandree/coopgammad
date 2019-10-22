/* See LICENSE file for copyright and license details. */
#ifndef SERVERS_GAMMA_H
#define SERVERS_GAMMA_H

#include "types-output.h"

#include <stddef.h>

#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...) __VA_ARGS__
# else
#  define GCC_ONLY(...) /* nothing */
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
GCC_ONLY(__attribute__((__nonnull__(2))))
int handle_get_gamma_info(size_t conn, const char *restrict message_id, const char *restrict crtc);

/**
 * Set the gamma ramps on an output
 * 
 * @param  output  The output
 * @param  ramps   The gamma ramps
 */
GCC_ONLY(__attribute__((__nonnull__)))
void set_gamma(const struct output *restrict output, const union gamma_ramps *restrict ramps);

/**
 * Store all current gamma ramps
 * 
 * @return  Zero on success, -1 on error
 */
int initialise_gamma_info(void);

/**
 * Store all current gamma ramps
 */
void store_gamma(void);

/**
 * Restore all gamma ramps
 */
void restore_gamma(void);

/**
 * Reapplu all gamma ramps
 */
void reapply_gamma(void);

#endif
