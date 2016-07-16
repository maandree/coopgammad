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
#include "server.h"
#include "../state.h"
#include "../communication.h"

#include <string.h>



#if defined(__clang__)
# pragma GCC diagnostic ignored "-Wswitch-enum"
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
int handle_get_gamma_info(size_t conn, const char* restrict message_id, const char* restrict crtc)
{
  struct output* restrict output;
  char* restrict buf;
  char depth[3];
  const char* supported;
  size_t n;
  
  if (crtc == NULL)  return send_error("protocol error: 'CRTC' header omitted");
  
  output = output_find_by_name(crtc, outputs, outputs_n);
  if (output == NULL)
    return send_error("selected CRTC does not exist");
  
  switch (output->depth)
    {
    case -2:  strcpy(depth, "d");  break;
    case -1:  strcpy(depth, "f");  break;
    default:
      sprintf(depth, "%i", output->depth);
      break;
    }
  
  switch (output->supported)
    {
    case LIBGAMMA_YES:    supported = "yes";    break;
    case LIBGAMMA_NO:     supported = "no";     break;
    default:              supported = "maybe";  break;
    }
  
  MAKE_MESSAGE(&buf, &n, 0,
	       "In response to: %s\n"
	       "Cooperative: yes\n" /* In mds: say ‘no’, mds-coopgamma changes to ‘yes’.” */
	       "Depth: %s\n"
	       "Red size: %zu\n"
	       "Green size: %zu\n"
	       "Blue size: %zu\n"
	       "Gamma support: %s\n"
	       "\n",
	       message_id, depth, output->red_size, output->green_size,
	       output->blue_size, supported);
  
  return send_message(conn, buf, n);
}


/**
 * Set the gamma ramps on an output
 * 
 * @param  output  The output
 * @param  ramps   The gamma ramps
 */
void set_gamma(const struct output* restrict output, const union gamma_ramps* restrict ramps)
{
  int r = 0;
  
  if (connected)
    {
      switch (output->depth)
	{
	case  8:  r = libgamma_crtc_set_gamma_ramps8(output->crtc,  ramps->u8);   break;
	case 16:  r = libgamma_crtc_set_gamma_ramps16(output->crtc, ramps->u16);  break;
	case 32:  r = libgamma_crtc_set_gamma_ramps32(output->crtc, ramps->u32);  break;
	case 64:  r = libgamma_crtc_set_gamma_ramps64(output->crtc, ramps->u64);  break;
	case -1:  r = libgamma_crtc_set_gamma_rampsf(output->crtc,  ramps->f);    break;
	case -2:  r = libgamma_crtc_set_gamma_rampsd(output->crtc,  ramps->d);    break;
	default:
	  abort();
	}
      if (r)
	libgamma_perror(argv0, r); /* Not fatal */
    }
}


/**
 * Disconnect from the site
 * 
 * @return  Zero on success, -1 on error
 */
int disconnect(void)
{
  if (!connected)
    return 0;
  
  connected = 0;
  return 0; /* TODO disconnect() */
}


/**
 * Reconnect to the site
 * 
 * @return  Zero on success, -1 on error
 */
int reconnect(void)
{
  if (connected)
    return 0;
  
  connected = 1;
  return 0; /* TODO reconnect() */
}

