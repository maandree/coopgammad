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
#include "../crtc-server/server.h"

#include <errno.h>
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
  
  if (!connected)
    return;
  
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



/**
 * Store all current gamma ramps
 * 
 * @return  Zero on success, -1 on error
 */
int initialise_gamma_info(void)
{
  libgamma_crtc_information_t info;
  int saved_errno;
  size_t i;
  
  for (i = 0; i < outputs_n; i++)
    {
      libgamma_get_crtc_information(&info, crtcs + i,
				    LIBGAMMA_CRTC_INFO_EDID |
				    LIBGAMMA_CRTC_INFO_MACRO_RAMP |
				    LIBGAMMA_CRTC_INFO_GAMMA_SUPPORT |
				    LIBGAMMA_CRTC_INFO_CONNECTOR_NAME);
      outputs[i].depth       = info.gamma_depth_error   ? 0 : info.gamma_depth;
      outputs[i].red_size    = info.gamma_size_error    ? 0 : info.red_gamma_size;
      outputs[i].green_size  = info.gamma_size_error    ? 0 : info.green_gamma_size;
      outputs[i].blue_size   = info.gamma_size_error    ? 0 : info.blue_gamma_size;
      outputs[i].supported   = info.gamma_support_error ? 0 : info.gamma_support;
      if (outputs[i].depth      == 0 || outputs[i].red_size  == 0 ||
	  outputs[i].green_size == 0 || outputs[i].blue_size == 0)
	outputs[i].supported = 0;
      outputs[i].name        = get_crtc_name(&info, crtcs + i);
      saved_errno = errno;
      outputs[i].crtc        = crtcs + i;
      libgamma_crtc_information_destroy(&info);
      outputs[i].ramps_size = outputs[i].red_size + outputs[i].green_size + outputs[i].blue_size;
      switch (outputs[i].depth)
	{
	default:
	  outputs[i].depth = 64;
	  /* Fall through */
	case  8:
	case 16:
	case 32:
	case 64:  outputs[i].ramps_size *= (size_t)(outputs[i].depth / 8);  break;
	case -2:  outputs[i].ramps_size *= sizeof(double);                  break;
	case -1:  outputs[i].ramps_size *= sizeof(float);                   break;
	}
      errno = saved_errno;
      if (outputs[i].name == NULL)
	return -1;
    }
  
  return 0;
}


/**
 * Store all current gamma ramps
 */
void store_gamma(void)
{
  int gerror;
  size_t i;
  
#define LOAD_RAMPS(SUFFIX, MEMBER) \
  do \
    { \
      libgamma_gamma_ramps##SUFFIX##_initialise(&(outputs[i].saved_ramps.MEMBER)); \
      gerror = libgamma_crtc_get_gamma_ramps##SUFFIX(outputs[i].crtc, &(outputs[i].saved_ramps.MEMBER)); \
      if (gerror) \
	{ \
	  libgamma_perror(argv0, gerror); \
	  outputs[i].supported = LIBGAMMA_NO; \
	  libgamma_gamma_ramps##SUFFIX##_destroy(&(outputs[i].saved_ramps.MEMBER)); \
	  memset(&(outputs[i].saved_ramps.MEMBER), 0, sizeof(outputs[i].saved_ramps.MEMBER)); \
	} \
    } \
  while (0)
  
  for (i = 0; i < outputs_n; i++)
    {
      if (outputs[i].supported == LIBGAMMA_NO)
	continue;
      
      switch (outputs[i].depth)
	{
	case 64:  LOAD_RAMPS(64, u64);  break;
	case 32:  LOAD_RAMPS(32, u32);  break;
	case 16:  LOAD_RAMPS(16, u16);  break;
	case  8:  LOAD_RAMPS( 8, u8);   break;
	case -2:  LOAD_RAMPS(d, d);     break;
	case -1:  LOAD_RAMPS(f, f);     break;
	default:  /* impossible */      break;
	}
    }
}


/**
 * Restore all gamma ramps
 */
void restore_gamma(void)
{
  size_t i;
  int gerror;
  
#define RESTORE_RAMPS(SUFFIX, MEMBER) \
  do \
    if (outputs[i].saved_ramps.MEMBER.red != NULL) \
      { \
	gerror = libgamma_crtc_set_gamma_ramps##SUFFIX(outputs[i].crtc, outputs[i].saved_ramps.MEMBER); \
	if (gerror) \
	    libgamma_perror(argv0, gerror); \
      } \
  while (0)
  
  for (i = 0; i < outputs_n; i++)
    {
      if (outputs[i].supported == LIBGAMMA_NO)
	continue;
      
      switch (outputs[i].depth)
	{
	case 64:  RESTORE_RAMPS(64, u64);  break;
	case 32:  RESTORE_RAMPS(32, u32);  break;
	case 16:  RESTORE_RAMPS(16, u16);  break;
	case  8:  RESTORE_RAMPS( 8, u8);   break;
	case -2:  RESTORE_RAMPS(d, d);     break;
	case -1:  RESTORE_RAMPS(f, f);     break;
	default:  /* impossible */         break;
	}
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

