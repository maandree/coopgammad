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
#include "chaining.h"
#include "../state.h"
#include "../communication.h"
#include "../util.h"
#include "../gamma-server/server.h"

#include <libclut.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>



/**
 * Handle a closed connection
 * 
 * @param   client  The file descriptor for the client
 * @return          Zero on success, -1 on error
 */
int connection_closed(int client)
{
  size_t i, j, k;
  int remove;
  
  for (i = 0; i < outputs_n; i++)
    {
      struct output* output = outputs + i;
      ssize_t updated = -1;
      for (j = k = 0; j < output->table_size; j += !remove, k++)
	{
	  remove = output->table_filters[j].client == client;
	  remove = remove && (output->table_filters[j].lifespan == LIFESPAN_UNTIL_DEATH);
	  if (remove)
	    {
	      filter_destroy(output->table_filters + j);
	      libgamma_gamma_ramps8_destroy(&(output->table_sums[j].u8));
	      output->table_size -= 1;
	      if (updated == -1)
		updated = (ssize_t)j;
	    }
	  output->table_filters[j] = output->table_filters[k];
	  output->table_sums[j]    = output->table_sums[k];
	}
      if (updated >= 0)
	if (flush_filters(output, (size_t)updated) < 0)
	  return -1;
    }
  
  return 0;
}


/**
 * Handle a ‘Command: get-gamma’ message
 * 
 * @param   conn           The index of the connection
 * @param   message_id     The value of the ‘Message ID’ header
 * @param   crtc           The value of the ‘CRTC’ header
 * @param   coalesce       The value of the ‘Coalesce’ header
 * @param   high_priority  The value of the ‘High priority’ header
 * @param   low_priority   The value of the ‘Low priority’ header
 * @return                 Zero on success (even if ignored), -1 on error,
 *                         1 if connection closed
 */
int handle_get_gamma(size_t conn, const char* restrict message_id, const char* restrict crtc,
		     const char* restrict coalesce, const char* restrict high_priority,
		     const char* restrict low_priority)
{
  struct output* restrict output;
  int64_t high, low;
  int coal;
  char* restrict buf;
  size_t start, end, len, n, i;
  char depth[3];
  char tables[sizeof("Tables: \n") + 3 * sizeof(size_t)];
  
  if (crtc          == NULL)  return send_error("protocol error: 'CRTC' header omitted");
  if (coalesce      == NULL)  return send_error("protocol error: 'Coalesce' header omitted");
  if (high_priority == NULL)  return send_error("protocol error: 'High priority' header omitted");
  if (low_priority  == NULL)  return send_error("protocol error: 'Low priority' header omitted");
  
  high = (int64_t)atoll(high_priority);
  low  = (int64_t)atoll(low_priority);
  
  if (!strcmp(coalesce, "yes"))
    coal = 1;
  else if (!strcmp(coalesce, "no"))
    coal = 0;
  else
    return send_error("protocol error: recognised value for 'Coalesce' header");
  
  output = output_find_by_name(crtc, outputs, outputs_n);
  if (output == NULL)
    return send_error("selected CRTC does not exist");
  else if (output->supported == LIBGAMMA_NO)
    return send_error("selected CRTC does not support gamma adjustments");
  
  for (start = 0; start < output->table_size; start++)
    if (output->table_filters[start].priority <= high)
      break;
  
  for (end = output->table_size; end > 0; end--)
    if (output->table_filters[end - 1].priority >= low)
      break;
  
  switch (output->depth)
    {
    case -2:  strcpy(depth, "d");  break;
    case -1:  strcpy(depth, "f");  break;
    default:
      sprintf(depth, "%i", output->depth);
      break;
    }
  
  if (coal)
    {
      *tables = '\0';
      n = output->ramps_size;
    }
  else
    {
      sprintf(tables, "Tables: %zu\n", end - start);
      n = (sizeof(int64_t) + output->ramps_size) * (end - start);
      for (i = start; i < end; i++)
	n += strlen(output->table_filters[i].class) + 1;
    }
  
  MAKE_MESSAGE(&buf, &n, 0,
	       "In response to: %s\n"
	       "Depth: %s\n"
	       "Red size: %zu\n"
	       "Green size: %zu\n"
	       "Blue size: %zu\n"
	       "%s"
	       "Length: %zu\n"
	       "\n",
	       message_id, depth, output->red_size, output->green_size,
	       output->blue_size, tables, n);
  
  if (coal)
    {
      if (start == 0)
	memcpy(buf + n, output->table_sums[end].u8.red, output->ramps_size);
      else
	{
	  union gamma_ramps ramps;
	  if (make_plain_ramps(&ramps, output))
	    {
	      int saved_errno = errno;
	      free(buf);
	      errno = saved_errno;
	      return -1;
	    }
	  for (i = start; i < end; i++)
	    apply_filter(&ramps, output->table_filters[i].ramps, output->depth, &ramps);
	  memcpy(buf + n, ramps.u8.red, output->ramps_size);
	  libgamma_gamma_ramps8_destroy(&(ramps.u8));
	}
      n += output->ramps_size;
    }
  else
    for (i = start; i < end; i++)
      {
#if defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wcast-align"
#endif
	*(int64_t*)(buf + n) = output->table_filters[i].priority;
#if defined(__clang__)
# pragma GCC diagnostic pop
#endif
	n += sizeof(int64_t);
	len = strlen(output->table_filters[i].class) + 1;
	memcpy(buf + n, output->table_filters[i].class, len);
	n += len;
	memcpy(buf + n, output->table_filters[i].ramps, output->ramps_size);
	n += output->ramps_size;
      }
  
  return send_message(conn, buf, n);
}


/**
 * Handle a ‘Command: set-gamma’ message
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @param   crtc        The value of the ‘CRTC’ header
 * @param   priority    The value of the ‘Priority’ header
 * @param   class       The value of the ‘Class’ header
 * @param   lifespan    The value of the ‘Lifespan’ header
 * @return              Zero on success (even if ignored), -1 on error,
 *                      1 if connection closed
 */
int handle_set_gamma(size_t conn, const char* restrict message_id, const char* restrict crtc,
		     const char* restrict priority, const char* restrict class, const char* restrict lifespan)
{
  struct message* restrict msg = inbound + conn;
  struct output* restrict output = NULL;
  struct filter filter;
  char* restrict p;
  char* restrict q;
  int saved_errno;
  ssize_t r;
  
  if (crtc     == NULL)  return send_error("protocol error: 'CRTC' header omitted");
  if (class    == NULL)  return send_error("protocol error: 'Class' header omitted");
  if (lifespan == NULL)  return send_error("protocol error: 'Lifespan' header omitted");
  
  filter.client   = connections[conn];
  filter.priority = priority == NULL ? 0 : (int64_t)atoll(priority);
  filter.ramps    = NULL;
  
  output = output_find_by_name(crtc, outputs, outputs_n);
  if (output == NULL)
    return send_error("CRTC does not exists");
  
  p = strstr(class, "::");
  if ((p == NULL) || (p == class))
    return send_error("protocol error: malformatted value for 'Class' header");
  q = strstr(p + 2, "::");
  if ((q == NULL) || (q == p))
    return send_error("protocol error: malformatted value for 'Class' header");
  
  if (!strcmp(lifespan, "until-removal"))
    filter.lifespan = LIFESPAN_UNTIL_REMOVAL;
  else if (!strcmp(lifespan, "until-death"))
    filter.lifespan = LIFESPAN_UNTIL_DEATH;
  else if (!strcmp(lifespan, "remove"))
    filter.lifespan = LIFESPAN_REMOVE;
  else
    return send_error("protocol error: recognised value for 'Lifespan' header");
  
  if (filter.lifespan == LIFESPAN_REMOVE)
    {
      if (msg->payload_size)
	fprintf(stderr, "%s: ignoring superfluous payload on Command: set-gamma message with "
			"Lifespan: remove\n", argv0);
      if (priority != NULL)
	fprintf(stderr, "%s: ignoring superfluous Priority header on Command: set-gamma message with "
			"Lifespan: remove\n", argv0);
    }
  else if (msg->payload_size != output->ramps_size)
    return send_error("invalid payload: size of message payload does matched the expectancy");
  else if (priority == NULL)
    return send_error("protocol error: 'Priority' header omitted");
  
  filter.class = memdup(class, strlen(class) + 1);
  if (filter.class == NULL)
    goto fail;
  
  if (filter.lifespan != LIFESPAN_REMOVE)
    {
      filter.ramps = memdup(msg->payload, msg->payload_size);
      if (filter.ramps == NULL)
	goto fail;
    }
  
  if ((r = add_filter(output, &filter)) < 0)
    goto fail;
  filter.class = NULL;
  filter.ramps = NULL;
  if (flush_filters(output, (size_t)r))
    goto fail;
  
  return send_errno(0);
  
 fail:
  saved_errno = errno;
  send_errno(saved_errno);
  free(filter.class);
  free(filter.ramps);
  errno = saved_errno;
  return -1;
}



/**
 * Recalculate the resulting gamma and
 * update push the new gamma ramps to the CRTC
 * 
 * @param   output         The output
 * @param   first_updated  The index of the first added or removed filter
 * @return                 Zero on success, -1 on error
 */
int flush_filters(struct output* restrict output, size_t first_updated)
{
  union gamma_ramps plain;
  union gamma_ramps* last;
  size_t i;
  
  if (first_updated == 0)
    {
      if (make_plain_ramps(&plain, output) < 0)
	return -1;
      last = &plain;
    }
  else
    last = output->table_sums + (first_updated - 1);
  
  for (i = first_updated; i < output->table_size; i++)
    {
      apply_filter(output->table_sums + i, output->table_filters[i].ramps, output->depth, last);
      last = output->table_sums + i;
    }
  
  set_gamma(output, last);
  
  if (first_updated == 0)
    libgamma_gamma_ramps8_destroy(&(plain.u8));
  
  return 0;
}

