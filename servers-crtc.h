/* See LICENSE file for copyright and license details. */
#ifndef SERVERS_CRTC_H
#define SERVERS_CRTC_H

#include "types-output.h"

#include <libgamma.h>

#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...) __VA_ARGS__
# else
#  define GCC_ONLY(...) /* nothing */
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
GCC_ONLY(__attribute__((__nonnull__)))
int handle_enumerate_crtcs(size_t conn, const char *restrict message_id);

/**
 * Get the name of a CRTC
 * 
 * @param   info  Information about the CRTC
 * @param   crtc  libgamma's state for the CRTC
 * @return        The name of the CRTC, `NULL` on error
 */
GCC_ONLY(__attribute__((__nonnull__)))
char *get_crtc_name(const libgamma_crtc_information_t *restrict info, const libgamma_crtc_state_t *restrict crtc);

/**
 * Initialise the site
 * 
 * @return   Zero on success, -1 on error
 */
int initialise_site(void);

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
int merge_state(struct output *restrict old_outputs, size_t old_outputs_n);

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
