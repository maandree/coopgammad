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
#include "crtc.h"
#include "../state.h"
#include "../communication.h"

#include <errno.h>
#include <string.h>



/**
 * Handle a ‘Command: enumerate-crtcs’ message
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @return              Zero on success (even if ignored), -1 on error,
 *                      1 if connection closed
 */
int handle_enumerate_crtcs(size_t conn, const char* restrict message_id)
{
  size_t i, n = 0, len;
  char* restrict buf;
  
  for (i = 0; i < outputs_n; i++)
    n += strlen(outputs[i].name) + 1;
  
  MAKE_MESSAGE(&buf, &n, 0,
	       "Command: crtc-enumeration\n"
	       "In response to: %s\n"
	       "Length: %zu\n"
	       "\n",
	       message_id, n);
  
  for (i = 0; i < outputs_n; i++)
    {
      len = strlen(outputs[i].name);
      memcpy(buf + n, outputs[i].name, len);
      buf[n + len] = '\n';
      n += len + 1;
    }
  
  return send_message(conn, buf, n);
}


/**
 * Get the name of a CRTC
 * 
 * @param   info  Information about the CRTC
 * @param   crtc  libgamma's state for the CRTC
 * @return        The name of the CRTC, `NULL` on error
 */
char* get_crtc_name(const libgamma_crtc_information_t* restrict info,
		    const libgamma_crtc_state_t* restrict crtc)
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


/**
 * Get partitions and CRTC:s
 * 
 * @return   Zero on success, -1 on error
 */
int initialise_crtcs(void)
{
  size_t i, j, n, n0;
  int gerror;
  
  /* Get partitions */
  if (site.partitions_available)
    if (!(partitions = calloc(site.partitions_available, sizeof(*partitions))))
      goto fail;
  for (i = 0; i < site.partitions_available; i++)
    {
      if ((gerror = libgamma_partition_initialise(partitions + i, &site, i)))
	goto fail_libgamma;
      outputs_n += partitions[i].crtcs_available;
    }
  
  /* Get CRTC:s */
  if (outputs_n)
    if (!(crtcs = calloc(outputs_n, sizeof(*crtcs))))
      goto fail;
  for (i = 0, j = n = 0; i < site.partitions_available; i++)
    for (n0 = n, n += partitions[i].crtcs_available; j < n; j++)
      if ((gerror = libgamma_crtc_initialise(crtcs + j, partitions + i, j - n0)))
	goto fail_libgamma;
  
  return 0;
  
 fail_libgamma:
  libgamma_perror(argv0, gerror);
  errno = 0;
 fail:
  return -1;
}


/**
 * Merge the new state with an old state
 * 
 * @param   old_outputs    The old `outputs`
 * @param   old_outputs_n  The old `outputs_n`
 * @return                 Zero on success, -1 on error
 */
int merge_state(struct output* restrict old_outputs, size_t old_outputs_n)
{
  struct output* restrict new_outputs = NULL;
  size_t new_outputs_n;
  size_t i, j, k;
  
  /* How many outputs does the system now have? */
  i = j = new_outputs_n = 0;
  while ((i < old_outputs_n) && (j < outputs_n))
    {
      int cmp = strcmp(old_outputs[i].name, outputs[j].name);
      if (cmp <= 0)
	new_outputs_n++;
      i += cmp >= 0;
      j += cmp <= 0;
    }
  new_outputs_n += outputs_n - j;
  
  /* Allocate output state array */
  if (new_outputs_n > 0)
    {
      new_outputs = calloc(new_outputs_n, sizeof(*new_outputs));
      if (new_outputs == NULL)
	return -1;
    }
  
  /* Merge output states */
  i = j = k = new_outputs_n = 0;
  while ((i < old_outputs_n) && (j < outputs_n))
    {
      int is_same = 0, cmp = strcmp(old_outputs[i].name, outputs[j].name);
      if (cmp == 0)
	is_same = (old_outputs[i].depth      == outputs[j].depth      &&
		   old_outputs[i].red_size   == outputs[j].red_size   &&
		   old_outputs[i].green_size == outputs[j].green_size &&
		   old_outputs[i].blue_size  == outputs[j].blue_size);
      if (is_same)
	{
	  new_outputs[k] = old_outputs[i];
	  new_outputs[k].crtc = outputs[j].crtc;
	  memset(old_outputs + i, 0, sizeof(*old_outputs));
	  outputs[j].crtc = NULL;
	  output_destroy(outputs + j);
	  k++;
	}
      else if (cmp <= 0)
	new_outputs[k++] = outputs[j];
      i += cmp >= 0;
      j += cmp <= 0;
    }
  while (j < outputs_n)
    new_outputs[k++] = outputs[j++];
  
  /* Commit merge */
  outputs   = new_outputs;
  outputs_n = new_outputs_n;
  
  return 0;
}

