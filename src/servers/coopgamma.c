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
#include "coopgamma.h"
#include "gamma.h"
#include "../state.h"
#include "../communication.h"
#include "../util.h"
#include "../types/output.h"

#include <libclut.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



/**
 * Apply a filter on top of another filter
 * 
 * @param  dest         The output for the resulting ramp-trio, must be initialised
 * @param  application  The red, green and blue ramps, as one single raw array,
 *                      of the filter that should be applied
 * @param  depth        -1: `float` stops
 *                      -2: `double` stops
 *                      Other: the number of bits of each (integral) stop
 * @param  base         The CLUT on top of which the new filter should be applied,
 *                      this can be the same pointer as `dest`
 */
static void apply_filter(union gamma_ramps* restrict dest, void* restrict application,
			 int depth, union gamma_ramps* restrict base)
{
  union gamma_ramps app;
  size_t bytedepth;
  size_t red_width, green_width, blue_width;
  
  if (depth == -1)
    bytedepth = sizeof(float);
  else if (depth == -2)
    bytedepth = sizeof(double);
  else
    bytedepth = (size_t)depth / 8;
  
  red_width   = (app.u8.red_size   = base->u8.red_size)   * bytedepth;
  green_width = (app.u8.green_size = base->u8.green_size) * bytedepth;
  blue_width  = (app.u8.blue_size  = base->u8.blue_size)  * bytedepth;
  
  app.u8.red   = application;
  app.u8.green = app.u8.red   + red_width;
  app.u8.blue  = app.u8.green + green_width;
  
  if (dest != base)
    {
      memcpy(dest->u8.red,   base->u8.red,   red_width);
      memcpy(dest->u8.green, base->u8.green, green_width);
      memcpy(dest->u8.blue,  base->u8.blue,  blue_width);
    }
  
  switch (depth)
    {
    case 8:
      libclut_apply(&(dest->u8), UINT8_MAX, uint8_t, &(app.u8), UINT8_MAX, uint8_t, 1, 1, 1);
      break;
    case 16:
      libclut_apply(&(dest->u16), UINT16_MAX, uint16_t, &(app.u16), UINT16_MAX, uint16_t, 1, 1, 1);
      break;
    case 32:
      libclut_apply(&(dest->u32), UINT32_MAX, uint32_t, &(app.u32), UINT32_MAX, uint32_t, 1, 1, 1);
      break;
    case 64:
      libclut_apply(&(dest->u64), UINT64_MAX, uint64_t, &(app.u64), UINT64_MAX, uint64_t, 1, 1, 1);
      break;
    case -1:
      libclut_apply(&(dest->f), 1.0f, float, &(app.d), 1.0f, float, 1, 1, 1);
      break;
    case -2:
      libclut_apply(&(dest->d), (double)1, double, &(app.f), (double)1, double, 1, 1, 1);
      break;
    default:
      abort();
    }
}



/**
 * Remove a filter from an output
 * 
 * @param   out     The output
 * @param   filter  The filter
 * @return          The index of the filter, `out->table_size` if not found
 */
static ssize_t remove_filter(struct output* restrict out, struct filter* restrict filter)
{
  size_t i, n = out->table_size;
  
  for (i = 0; i < n; i++)
    if (!strcmp(filter->class, out->table_filters[i].class))
      break;
  
  if (i == out->table_size)
    {
      fprintf(stderr, "%s: ignoring attempt to removing non-existing filter on CRTC %s: %s\n",
	      argv0, out->name, filter->class);
      return (ssize_t)(out->table_size);
    }
  
  filter_destroy(out->table_filters + i);
  libgamma_gamma_ramps8_destroy(&(out->table_sums[i].u8));
  
  n = n - i - 1;
  memmove(out->table_filters + i, out->table_filters + i + 1, n * sizeof(*(out->table_filters)));
  memmove(out->table_sums    + i, out->table_sums    + i + 1, n * sizeof(*(out->table_sums)));
  out->table_size--;
  
  return (ssize_t)i;
}


/**
 * Add a filter to an output
 * 
 * @param   out     The output
 * @param   filter  The filter
 * @return          The index given to the filter, -1 on error
 */
static ssize_t add_filter(struct output* restrict out, struct filter* restrict filter)
{
  size_t i, n = out->table_size;
  int r = -1;
  
  /* Remove? */
  if (filter->lifespan == LIFESPAN_REMOVE)
    return remove_filter(out, filter);
  
  /* Update? */
  for (i = 0; i < n; i++)
    if (!strcmp(filter->class, out->table_filters[i].class))
      break;
  if (i != n)
    {
      filter_destroy(out->table_filters + i);
      out->table_filters[i] = *filter;
      filter->class = NULL;
      filter->ramps = NULL;
      return (ssize_t)i;
    }
  
  /* Add! */
  for (i = 0; i < n; i++)
    if (filter->priority > out->table_filters[i].priority)
      break;
  
  if (n == out->table_alloc)
    {
      void* new;
      
      new = realloc(out->table_filters, (n + 10) * sizeof(*(out->table_filters)));
      if (new == NULL)
	return -1;
      out->table_filters = new;
      
      new = realloc(out->table_sums, (n + 10) * sizeof(*(out->table_sums)));
      if (new == NULL)
	return -1;
      out->table_sums = new;
      
      out->table_alloc += 10;
    }
  
  memmove(out->table_filters + i + 1, out->table_filters + i, (n - i) * sizeof(*(out->table_filters)));
  memmove(out->table_sums    + i + 1, out->table_sums    + i, (n - i) * sizeof(*(out->table_sums)));
  out->table_size++;
  
  out->table_filters[i] = *filter;
  filter->class = NULL;
  filter->ramps = NULL;
  
  COPY_RAMP_SIZES(&(out->table_sums[i].u8), out);
  switch (out->depth)
    {
    case  8:  r = libgamma_gamma_ramps8_initialise(&(out->table_sums[i].u8));    break;
    case 16:  r = libgamma_gamma_ramps16_initialise(&(out->table_sums[i].u16));  break;
    case 32:  r = libgamma_gamma_ramps32_initialise(&(out->table_sums[i].u32));  break;
    case 64:  r = libgamma_gamma_ramps64_initialise(&(out->table_sums[i].u64));  break;
    case -1:  r = libgamma_gamma_rampsf_initialise(&(out->table_sums[i].f));     break;
    case -2:  r = libgamma_gamma_rampsd_initialise(&(out->table_sums[i].d));     break;
    default:
      abort();
    }
  if (r < 0)
    return -1;
  
  return (ssize_t)i;
}


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
	  if (j != k)
	    {
	      output->table_filters[j] = output->table_filters[k];
	      output->table_sums[j]    = output->table_sums[k];
	    }
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
  
  MAKE_MESSAGE(&buf, &n, n,
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
      if ((start == 0) && (start < end))
	memcpy(buf + n, output->table_sums[end - 1].u8.red, output->ramps_size);
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
  if (flush_filters(output, (size_t)r))
    goto fail;
  
  free(filter.class);
  free(filter.ramps);
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



/**
 * Preserve current gamma ramps at priority 0 for all outputs
 * 
 * @return  Zero on success, -1 on error
 */
int preserve_gamma(void)
{
  size_t i;
  
  for (i = 0; i < outputs_n; i++)
    {
      struct filter filter = {
	.client   = -1,
	.priority = 0,
	.class    = NULL,
	.lifespan = LIFESPAN_UNTIL_REMOVAL,
	.ramps    = NULL
      };
      outputs[i].table_filters = calloc(4, sizeof(*(outputs[i].table_filters)));
      outputs[i].table_sums = calloc(4, sizeof(*(outputs[i].table_sums)));
      outputs[i].table_alloc = 4;
      outputs[i].table_size = 1;
      filter.class = memdup(PKGNAME "::" COMMAND "::preserved", sizeof(PKGNAME "::" COMMAND "::preserved"));
      if (filter.class == NULL)
	return -1;
      filter.ramps = memdup(outputs[i].saved_ramps.u8.red, outputs[i].ramps_size);
      if (filter.ramps == NULL)
	return -1;
      outputs[i].table_filters[0] = filter;
      COPY_RAMP_SIZES(&(outputs[i].table_sums[0].u8), outputs + i);
      if (!gamma_ramps_unmarshal(outputs[i].table_sums, outputs[i].saved_ramps.u8.red, outputs[i].ramps_size))
	return -1;
    }
  
  return 0;
}

