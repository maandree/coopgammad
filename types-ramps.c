/* See LICENSE file for copyright and license details. */
#include "types-ramps.h"

#include <libclut.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>


/**
 * The name of the process
 */
extern char *restrict argv0;


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
size_t
gamma_ramps_marshal(const union gamma_ramps *restrict this, void *restrict buf, size_t ramps_size)
{
	if (buf)
		memcpy(buf, this->u8.red, ramps_size);
	return ramps_size;
}


/**
 * Unmarshal a ramp trio
 * 
 * @param   this        Output for the ramps, `.red_size`, `.green_size`,
 *                      and `.blue_size` must already be set
 * @param   buf         Buffer with the marshalled ramps
 * @param   ramps_size  The byte-size of ramps
 * @return              The number of unmarshalled bytes, 0 on error
 */
size_t
gamma_ramps_unmarshal(union gamma_ramps *restrict this, const void *restrict buf, size_t ramps_size)
{
	size_t depth = ramps_size / (this->u8.red_size + this->u8.green_size + this->u8.blue_size);
	int r = 0;

	switch (depth) {
	case 1:
		r = libgamma_gamma_ramps8_initialise(&this->u8);
		break;

	case 2:
		r = libgamma_gamma_ramps16_initialise(&this->u16);
		break;

	case 4:
		r = libgamma_gamma_ramps32_initialise(&this->u32);
		break;

	case 8:
		r = libgamma_gamma_ramps64_initialise(&this->u64);
		break;

	default:
		if (depth == sizeof(float))
			r = libgamma_gamma_rampsf_initialise(&this->f);
		else if (depth == sizeof(double))
			r = libgamma_gamma_rampsd_initialise(&this->d);
		else
			abort();
		break;
	}

	if (r) {
		libgamma_perror(argv0, r);
		errno = 0;
		return 0;
	}

	memcpy(this->u8.red, buf, ramps_size);
	return ramps_size;
}
