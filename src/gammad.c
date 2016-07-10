/**
 * gammad -- Cooperative gamma server
 * Copyright (C) 2016  Mattias Andrée (maandree@kth.se)
 * 
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <libgamma.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "output.h"



/**
 * The name of the process
 */
char* argv0;



/**
 * Get the name of a CRTC
 * 
 * @param   info  Information about the CRTC
 * @param   crtc  libgamma's state for the CRTC
 * @return        The name of the CRTC, `NULL` on error
 */
static char* getname(libgamma_crtc_information_t* info, libgamma_crtc_state_t* crtc)
{
  if ((info->edid_error == 0) && (info->edid != NULL))
    return libgamma_behex_edid(info->edid, info->edid_length);
  else if ((info->connector_name_error == 0) && (info->connector_name != NULL))
    {
      char* name = malloc(3 * sizeof(size_t) + strlen(info->connector_name) + 2);
      if (name != NULL)
	sprintf(name, "%zu.%s", crtc->partition->partition, info->connector_name);
      return name;
    }
  else
    {
      char* name = malloc(2 * 3 * sizeof(size_t) + 2);
      if (name != NULL)
	sprintf(name, "%zu.%zu", crtc->partition->partition, crtc->crtc);
      return name;
    }
}


int main(int argc, char** argv)
{
  int method, gerror, rc = 1;
  libgamma_site_state_t site;
  libgamma_partition_state_t* partitions = NULL;
  libgamma_crtc_state_t* crtcs = NULL;
  struct output* outputs = NULL;
  size_t i, j, n, n0, crtcs_n = 0;
  
  argv0 = argv[0];
  
  memset(&site, 0, sizeof(site));
  
  /* Get method */
  if (libgamma_list_methods(&method, 1, 0) < 1)
    return fprintf(stderr, "%s: no adjustment method available\n", argv0), 1;
  
  /* Get site */
  if ((gerror = libgamma_site_initialise(&site, method, NULL)))
    goto fail_libgamma;
  
  /* Get partitions */
  if (site.partitions_available)
    if (!(partitions = calloc(site.partitions_available, sizeof(*partitions))))
      goto fail;
  for (i = 0; i < site.partitions_available; i++)
    {
      if ((gerror = libgamma_partition_initialise(partitions + i, &site, i)))
	goto fail_libgamma;
      crtcs_n += partitions[i].crtcs_available;
    }
  
  /* Get CRTC:s */
  if (crtcs_n)
    if (!(crtcs = calloc(crtcs_n, sizeof(*crtcs))))
      goto fail;
  for (i = 0, j = n = 0; i < site.partitions_available; i++)
    for (n0 = n, n += partitions[i].crtcs_available; j < n; j++)
      if ((gerror = libgamma_crtc_initialise(crtcs + j, partitions + i, j - n0)))
	goto fail_libgamma;
  
  /* Get CRTC information */
  if (crtcs_n)
    if (!(outputs = calloc(crtcs_n, sizeof(*outputs))))
      goto fail;
  for (i = 0; i < crtcs_n; i++)
    {
      libgamma_crtc_information_t info;
      int saved_errno;
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
      if (outputs[i].depth      == 0 ||
	  outputs[i].red_size   == 0 ||
	  outputs[i].green_size == 0 ||
	  outputs[i].blue_size  == 0)
	outputs[i].supported = 0;
      outputs[i].name        = getname(&info, crtcs + i);
      saved_errno = errno;
      outputs[i].crtc        = crtcs + i;
      libgamma_crtc_information_destroy(&info);
      outputs[i].ramps_size = outputs[i].red_size + outputs[i].green_size + outputs[i].blue_size;
      /* outputs[i].ramps_size will be multipled by the stop-size later */
      errno = saved_errno;
      if (outputs[i].name == NULL)
	goto fail;
    }
  
  /* Load current gamma ramps */
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
  for (i = 0; i < crtcs_n; i++)
    if (outputs[i].supported != LIBGAMMA_NO)
      switch (outputs[i].depth)
	{
	case 8:
	  outputs[i].ramps_size *= sizeof(uint8_t);
	  LOAD_RAMPS(8, u8);
	  break;
	case 16:
	  outputs[i].ramps_size *= sizeof(uint16_t);
	  LOAD_RAMPS(16, u16);
	  break;
	case 32:
	  outputs[i].ramps_size *= sizeof(uint32_t);
	  LOAD_RAMPS(32, u32);
	  break;
	default:
	  outputs[i].depth = 64;
	  /* fall through */
	case 64:
	  outputs[i].ramps_size *= sizeof(uint64_t);
	  LOAD_RAMPS(64, u64);
	  break;
	case -1:
	  outputs[i].ramps_size *= sizeof(float);
	  LOAD_RAMPS(f, f);
	  break;
	case -2:
	  outputs[i].ramps_size *= sizeof(double);
	  LOAD_RAMPS(d, d);
	  break;
	}
  
  /* Done */
  rc = 0;
 done:
#define RESTORE_RAMPS(SUFFIX, MEMBER) \
  do \
    if (outputs[i].saved_ramps.MEMBER.red != NULL) \
      { \
	gerror = libgamma_crtc_set_gamma_ramps##SUFFIX(outputs[i].crtc, outputs[i].saved_ramps.MEMBER); \
	if (gerror) \
	    libgamma_perror(argv0, gerror); \
      } \
  while (0)
  if (crtcs != NULL)
    for (i = 0; i < crtcs_n; i++)
      {
	if (outputs[i].supported != LIBGAMMA_NO)
	  switch (outputs[i].depth)
	    {
	    case 8:
	      RESTORE_RAMPS(8, u8);
	      break;
	    case 16:
	      RESTORE_RAMPS(16, u16);
	      break;
	    case 32:
	      RESTORE_RAMPS(32, u32);
	      break;
	    case 64:
	      RESTORE_RAMPS(64, u64);
		break;
	    case -1:
	      RESTORE_RAMPS(f, f);
	      break;
	    case -2:
	      RESTORE_RAMPS(d, d);
	      break;
	    default:
	      break; /* impossible */
	    }
	output_destroy(outputs + i);
	libgamma_crtc_destroy(crtcs + i);
      }
  free(crtcs);
  if (partitions != NULL)
    for (i = 0; i < site.partitions_available; i++)
      libgamma_partition_destroy(partitions + i);
  free(partitions);
  libgamma_site_destroy(&site);
  return rc;
  /* Fail */
 fail:
  perror(argv0);
  goto done;
 fail_libgamma:
  libgamma_perror(argv0, gerror);
  goto done;
}

