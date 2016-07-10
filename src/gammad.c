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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>



char* argv0;



int main(int argc, char** argv)
{
  int method, gerror, rc = 1;
  libgamma_site_state_t site;
  libgamma_partition_state_t* partitions = NULL;
  libgamma_crtc_state_t* crtcs = NULL;
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
  
  /* Done */
  rc = 0;
 done:
  if (crtcs != NULL)
    for (i = 0; i < crtcs_n; i++)
      libgamma_crtc_destroy(crtcs + i);
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
