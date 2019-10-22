/* See LICENSE file for copyright and license details. */
#ifndef TYPES_RAMPS_H
#define TYPES_RAMPS_H

#include <libgamma.h>

#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...) __VA_ARGS__
# else
#  define GCC_ONLY(...) /* nothing */
# endif
#endif

/**
 * Gamma ramps union for all
 * lbigamma gamma ramps types
 */
union gamma_ramps {
	/**
	 * Ramps with 8-bit value
	 */
	libgamma_gamma_ramps8_t u8;

	/**
	 * Ramps with 16-bit value
	 */
	libgamma_gamma_ramps16_t u16;

	/**
	 * Ramps with 32-bit value
	 */
	libgamma_gamma_ramps32_t u32;

	/**
	 * Ramps with 64-bit value
	 */
	libgamma_gamma_ramps64_t u64;

	/**
	 * Ramps with `float` value
	 */
	libgamma_gamma_rampsf_t f;

	/**
	 * Ramps with `double` value
	 */
	libgamma_gamma_rampsd_t d;
};

/**
 * Marshal a ramp trio
 * 
 * @param   this        The ramps
 * @param   buf         Output buffer for the marshalled ramps,
 *                      `NULL` just measure how large the buffers
 *                      needs to be
 * @param   ramps_size  The byte-size of ramps
 * @return              The number of marshalled byte
 */
GCC_ONLY(__attribute__((__nonnull__(1))))
size_t gamma_ramps_marshal(const union gamma_ramps *restrict this, void *restrict buf, size_t ramps_size);

/**
 * Unmarshal a ramp trio
 * 
 * @param   this        Output for the ramps
 * @param   buf         Buffer with the marshalled ramps
 * @param   ramps_size  The byte-size of ramps
 * @return              The number of unmarshalled bytes, 0 on error
 */
GCC_ONLY(__attribute__((__nonnull__)))
size_t gamma_ramps_unmarshal(union gamma_ramps *restrict this, const void *restrict buf, size_t ramps_size);

#endif
