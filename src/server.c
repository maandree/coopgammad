/**
 * coopgammad -- Cooperative gamma server
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
#include "server.h"
#include "output.h"
#include "util.h"

#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



/**
 * List of all client's file descriptors
 * 
 * Unused slots, with index less than `connections_used`,
 * should have the value -1 (negative)
 */
int* connections = NULL;

/**
 * The number of elements allocated for `connections`
 */
size_t connections_alloc = 0;

/**
 * The index of the first unused slot in `connections`
 */
size_t connections_ptr = 0;

/**
 * The index of the last used slot in `connections`, plus 1
 */
size_t connections_used = 0;

/**
 * The clients' connections' message buffers
 */
struct message* client_messages = NULL;


/**
 * The name of the process
 */
extern char* argv0;

/**
 * The server socket's file descriptor
 */
extern int socketfd;

/**
 * Has the process receive a signal
 * telling it to re-execute?
 */
extern volatile sig_atomic_t reexec;

/**
 * Has the process receive a signal
 * telling it to terminate?
 */
extern volatile sig_atomic_t terminate;

/**
 * Array of all outputs
 */
extern struct output* outputs;

/**
 * The nubmer of elements in `outputs`
 */
extern size_t outputs_n;



/**
 * Destroy the state of the connections
 * 
 * @param  disconnect  Disconnect all connections?
 */
void server_destroy(int disconnect)
{
  size_t i;
  for (i = 0; i < connections_used; i++)
    if (connections[i] >= 0)
      {
	if (disconnect)
	  {
	    shutdown(connections[i], SHUT_RDWR);
	    close(connections[i]);
	  }
	message_destroy(client_messages + i);
      }
  free(client_messages);
  free(connections);
}


/**
 * Marshal the state of the connections
 * 
 * @param   buf  Output buffer for the marshalled data,
 *               `NULL` to only measure how many bytes
 *               this buffer needs
 * @return       The number of marshalled bytes
 */
size_t server_marshal(void* buf)
{
  size_t i, off = 0;
  char* bs = buf;
  
  if (bs != NULL)
    *(size_t*)(bs + off) = connections_ptr;
  off += sizeof(size_t);
  
  if (bs != NULL)
    *(size_t*)(bs + off) = connections_used;
  off += sizeof(size_t);
  
  if (bs != NULL)
    memcpy(bs + off, connections, connections_used * sizeof(*connections));
  off += connections_used * sizeof(*connections);
  
  for (i = 0; i < connections_used; i++)
    if (connections[i] >= 0)
      off += message_marshal(client_messages + i, bs ? bs + off : NULL);
  
  return off;
}


/**
 * Unmarshal the state of the connections
 * 
 * @param   buf  Buffer for the marshalled data
 * @return       The number of unmarshalled bytes, 0 on error
 */
size_t server_unmarshal(const void* buf)
{
  size_t off = 0, i, n;
  const char* bs = buf;
  
  connections = NULL;
  client_messages = NULL;
  
  connections_ptr = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  connections_alloc = connections_used = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  if (connections_alloc > 0)
    {
      connections = memdup(bs + off, connections_alloc * sizeof(*connections));
      if (connections == NULL)
	return 0;
      off += connections_used * sizeof(*connections);
      
      client_messages = malloc(connections_alloc * sizeof(*client_messages));
      if (client_messages == NULL)
	return 0;
    }
  
  for (i = 0; i < connections_used; i++)
    if (connections[i] >= 0)
      {
	off += n = message_unmarshal(client_messages + i, bs ? bs + off : NULL);
	if (n == 0)
	  return 0;
      }
  
  return off;
}


/**
 * Sets the file descriptor set that includes
 * the server socket and all connections
 * 
 * @param   fds  The file descritor set
 * @return       The highest set file descritor plus 1
 */
static int update_fdset(fd_set* fds)
{
  int fdmax = socketfd;
  size_t i;
  FD_ZERO(fds);
  FD_SET(socketfd, fds);
  for (i = 0; i < connections_used; i++)
    if (connections[i] >= 0)
      {
	FD_SET(connections[i], fds);
	if (fdmax < connections[i])
	  fdmax = connections[i];
      }
  return fdmax + 1;
}


/**
 * Handle event on the server socket
 * 
 * @return  1: New connection accepted
 *          0: Successful
 *          -1: Failure
 */
static int handle_server(void)
{
  int fd, flags, saved_errno;
  
  fd = accept(socketfd, NULL, NULL);
  if (fd < 0)
    switch (errno)
      {
      case EINTR:
	return 0;
      case ECONNABORTED:
      case EINVAL:
	terminate = 1;
	return 0;
      default:
	return -1;
      }
  
  flags = fcntl(fd, F_GETFL);
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    goto fail;
 
  if (connections_ptr == connections_alloc)
    {
      void* new;
      
      new = realloc(connections, (connections_alloc + 10) * sizeof(*connections));
      if (new == NULL)
	goto fail;
      connections = new;
      connections[connections_ptr] = fd;
      
      new = realloc(client_messages, (connections_alloc + 10) * sizeof(*client_messages));
      if (new == NULL)
	goto fail;
      client_messages = new;
      connections_alloc += 10;
      if (message_initialise(client_messages + connections_ptr))
	goto fail;
    }
  
  connections_ptr++;
  while (connections_ptr < connections_used)
    if (connections[connections_ptr] >= 0)
      connections_ptr++;
  if (connections_used < connections_ptr)
    connections_used = connections_ptr;
  
  return 1;
 fail:
  saved_errno = errno;
  shutdown(fd, SHUT_RDWR);
  close(fd);
  errno = saved_errno;
  return -1;
}


/**
 * Handle a closed connection
 * 
 * @param   client  The file descriptor for the client
 * @retunr          Zero on success, -1 on error
 */
static int connection_closed(int client)
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
	flush_filters(output, (size_t)updated);
    }
  
  return 0;
}


/**
 * Handle a ‘Command: enumerate-crtcs’ message
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @return              Zero on success (even if ignored), -1 on error
 */
static int enumerate_crtcs(size_t conn, char* message_id)
{
  size_t i, n = 0, len;
  ssize_t m;
  char* buf;
  
  if (message_id == NULL)
    return fprintf(stderr, "%s: ignoring incomplete Command: enumerate-crtcs message\n", argv0), 0;
  
  for (i = 0; i < outputs_n; i++)
    n += strlen(outputs[i].name) + 1;
  
  snprintf(NULL, 0,
	   "In response to: %s\n"
	   "Length: %zu\n"
	   "\n%zn",
	   message_id, n, &m);
  
  if (!(buf = malloc(n + (size_t)m)))
    return -1;
  
  sprintf(buf,
	  "In response to: %s\n"
	  "Length: %zu\n"
	  "\n",
	  message_id, n);
  
  n = (size_t)m;
  
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
 * Handle a ‘Command: set-gamma’ message
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @param   crtc        The value of the ‘CRTC’ header
 * @return              Zero on success (even if ignored), -1 on error
 */
static int get_gamma_info(size_t conn, char* message_id, char* crtc)
{
  struct output* output;
  char* buf;
  char depth[3];
  const char* supported;
  ssize_t n;
  
  if ((message_id == NULL) || (crtc == NULL))
    return fprintf(stderr, "%s: ignoring incomplete Command: get-gamma-info message\n", argv0), 0;
  
  output = output_find_by_name(crtc, outputs, outputs_n);
  if (output == NULL)
    {
      snprintf(NULL, 0,
	       "Command: error\n"
	       "In response to: %s\n"
	       "Error: custom\n"
	       "Length: 13\n"
	       "\n"
	       "No such CRTC\n%zn",
	       message_id, &n);
      
      if (!(buf = malloc((size_t)n)))
	return -1;
      
      sprintf(buf,
	      "Command: error\n"
	      "In response to: %s\n"
	      "Error: custom\n"
	      "Length: 13\n"
	      "\n"
	      "No such CRTC\n",
	      message_id);
      
      return send_message(conn, buf, (size_t)n);
    }
  
  switch (output->depth)
    {
    case -2:
      sprintf(depth, "%s", "d");
      break;
    case -1:
      sprintf(depth, "%s", "f");
      break;
    default:
      sprintf(depth, "%i", output->depth);
      break;
    }
  
  switch (output->supported)
    {
    case LIBGAMMA_YES:  supported = "yes";    break;
    case LIBGAMMA_NO:   supported = "no";     break;
    default:            supported = "maybe";  break;
    }
  
  snprintf(NULL, 0,
	   "In response to: %s\n"
	   "Cooperative: yes\n"
	   "Depth: %s\n"
	   "Red size: %zu\n"
	   "Green size: %zu\n"
	   "Blue size: %zu\n"
	   "Gamma support: %s\n"
	   "\n%zn",
	   message_id, depth, output->red_size, output->green_size,
	   output->blue_size, supported, &n);
  
  if (!(buf = malloc((size_t)n)))
    return -1;
  
  sprintf(buf,
	  "In response to: %s\n"
	  "Cooperative: yes\n"
	  "Depth: %s\n"
	  "Red size: %zu\n"
	  "Green size: %zu\n"
	  "Blue size: %zu\n"
	  "Gamma support: %s\n"
	  "\n",
	  message_id, depth, output->red_size, output->green_size,
	  output->blue_size, supported);
  
  return send_message(conn, buf, (size_t)n);
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
 * @return                 Zero on success (even if ignored), -1 on error
 */
static int get_gamma(size_t conn, char* message_id, char* crtc, char* coalesce,
		     char* high_priority, char* low_priority)
{
  struct output* output;
  int64_t high, low;
  int coal;
  char* buf;
  ssize_t m;
  size_t start, end, len, n, i;
  char depth[3];
  char tables[sizeof("Tables: \n") + 3 * sizeof(size_t)];
  const char *error = NULL;
  
  if ((message_id    == NULL) ||
      (crtc          == NULL) ||
      (coalesce      == NULL) ||
      (high_priority == NULL) ||
      (low_priority  == NULL))
    return fprintf(stderr, "%s: ignoring incomplete Command: get-gamma message\n", argv0), 0;
  
  high = (int64_t)atoll(high_priority);
  low  = (int64_t)atoll(low_priority);
  
  if (!strcmp(coalesce, "yes"))
    coal = 1;
  else if (!strcmp(coalesce, "no"))
    coal = 0;
  else
    return fprintf(stderr, "%s: ignoring Command: get-gamma message with bad Coalesce value: %s\n",
		   argv0, coalesce), 0;
  
  output = output_find_by_name(crtc, outputs, outputs_n);
  if (output == NULL)
    error = "No such CRTC";
  else if (output->supported == LIBGAMMA_NO)
    error = "CRTC does not support gamma ramps";
  
  if (error != NULL)
    {
      snprintf(NULL, 0,
	       "Command: error\n"
	       "In response to: %s\n"
	       "Error: custom\n"
	       "Length: %zu\n"
	       "\n"
	       "%s\n%zn",
	       message_id, strlen(error) + 1, error, &m);
      
      if (!(buf = malloc((size_t)m)))
	return -1;
      
      sprintf(buf,
	      "Command: error\n"
	      "In response to: %s\n"
	      "Error: custom\n"
	      "Length: %zu\n"
	      "\n"
	      "%s\n",
	      message_id, strlen(error) + 1, error);
      
      return send_message(conn, buf, (size_t)m);
    }
  
  for (start = 0; start < output->table_size; start++)
    if (output->table_filters[start].priority <= high)
      break;
  
  for (end = output->table_size; end > 0; end--)
    if (output->table_filters[end - 1].priority >= low)
      break;
  
  switch (output->depth)
    {
    case -2:
      sprintf(depth, "%s", "d");
      break;
    case -1:
      sprintf(depth, "%s", "f");
      break;
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
  
  snprintf(NULL, 0,
	   "In response to: %s\n"
	   "Depth: %s\n"
	   "Red size: %zu\n"
	   "Green size: %zu\n"
	   "Blue size: %zu\n"
	   "%s"
	   "Length: %zu\n"
	   "\n%zn",
	   message_id, depth, output->red_size, output->green_size,
	   output->blue_size, tables, n, &m);
  
  if (!(buf = malloc(n + (size_t)m)))
    return -1;
  
  sprintf(buf,
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
  
  n = (size_t)m;
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
	    apply(&ramps, output->table_filters[i].ramps, output->depth, &ramps);
	  memcpy(buf + n, ramps.u8.red, output->ramps_size);
	  libgamma_gamma_ramps8_destroy(&(ramps.u8));
	}
      n += output->ramps_size;
    }
  else
    for (i = start; i < end; i++)
      {
	*(int64_t*)(buf + n) = output->table_filters[i].priority;
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
 * @param   conn      The index of the connection
 * @param   crtc      The value of the ‘CRTC’ header
 * @param   priority  The value of the ‘Priority’ header
 * @param   class     The value of the ‘Class’ header
 * @param   lifespan  The value of the ‘Lifespan’ header
 * @return            Zero on success (even if ignored), -1 on error
 */
static int set_gamma(size_t conn, char* crtc, char* priority, char* class, char* lifespan)
{
  struct message* msg = client_messages + conn;
  struct output* output = NULL;
  struct filter filter;
  char* p;
  char* q;
  int saved_errno;
  ssize_t r;
  
  if ((crtc     == NULL) ||
      (priority == NULL) ||
      (class    == NULL) ||
      (lifespan == NULL))
    return fprintf(stderr, "%s: ignoring incomplete Command: set-gamma message\n", argv0), 0;
  
  filter.client   = connections[conn];
  filter.priority = (int64_t)atoll(priority);
  filter.ramps    = NULL;
  
  output = output_find_by_name(crtc, outputs, outputs_n);
  if (output == NULL)
    return fprintf(stderr, "%s: ignoring Command: set-gamma message with non-existing CRTC: %s\n",
		   argv0, crtc), 0;
  
  p = strstr(class, "::");
  if ((p == NULL) || (p == class))
    return fprintf(stderr, "%s: ignoring Command: set-gamma message with malformatted class: %s\n",
		   argv0, class), 0;
  q = strstr(p + 2, "::");
  if ((q == NULL) || (q == p))
    return fprintf(stderr, "%s: ignoring Command: set-gamma message with malformatted class: %s\n",
		   argv0, class), 0;
  
  if (!strcmp(lifespan, "until-removal"))
    filter.lifespan = LIFESPAN_UNTIL_REMOVAL;
  else if (!strcmp(lifespan, "until-death"))
    filter.lifespan = LIFESPAN_UNTIL_DEATH;
  else if (!strcmp(lifespan, "remove"))
    filter.lifespan = LIFESPAN_REMOVE;
  else
    return fprintf(stderr, "%s: ignoring Command: set-gamma message with bad lifetime: %s\n",
		   argv0, lifespan), 0;
  
  if (filter.lifespan == LIFESPAN_REMOVE)
    {
      if (msg->payload_size)
	fprintf(stderr, "%s: ignoring superfluous playload on Command: set-gamma message with "
			"Lifespan: remove\n", argv0);
    }
  else if (msg->payload_size != output->ramps_size)
    return fprintf(stderr, "%s: ignoring Command: set-gamma message bad payload: size: %zu instead of %zu\n",
		   argv0, msg->payload_size, output->ramps_size), 0;
  
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
  
  return 0;
  
 fail:
  saved_errno = errno;
  free(filter.class);
  free(filter.ramps);
  errno = saved_errno;
  return -1;
}


/**
 * Handle event on a connection to a client
 * 
 * @param   conn  The index of the connection
 * @return        1: The connection as closed
 *                0: Successful
 *                -1: Failure
 */
static int handle_connection(size_t conn)
{
  struct message* msg = client_messages + conn;
  int r, fd = connections[conn];
  char* command       = NULL;
  char* crtc          = NULL;
  char* coalesce      = NULL;
  char* high_priority = NULL;
  char* low_priority  = NULL;
  char* priority      = NULL;
  char* class         = NULL;
  char* lifespan      = NULL;
  char* message_id    = NULL; /* Never report as superfluous */
  size_t i;
  
 again:
  switch (message_read(msg, fd))
    {
    default:
      break;
    case -1:
      switch (errno)
	{
	case EINTR:
	case EAGAIN:
#if EAGAIN != EWOULDBLOCK
	case EWOULDBLOCK:
#endif
	  return 0;
	default:
	  return -1;
	case ECONNRESET:;
	  /* Fall throught to `case -2` in outer switch */
	}
    case -2:
      shutdown(fd, SHUT_RDWR);
      close(fd);
      connections[conn] = -1;
      if (conn < connections_ptr)
	connections_ptr = conn;
      if (conn == connections_used)
	connections_used -= 1;
      message_destroy(msg);
      connection_closed(fd);
      return 1;
    }
  
  for (i = 0; i < msg->header_count; i++)
    {
      char* header = msg->headers[i];
      if      (strstr(header, "Command: ")       == header)  command       = strstr(header, ": ") + 2;
      else if (strstr(header, "CRTC: ")          == header)  crtc          = strstr(header, ": ") + 2;
      else if (strstr(header, "Coalesce: ")      == header)  coalesce      = strstr(header, ": ") + 2;
      else if (strstr(header, "High priority: ") == header)  high_priority = strstr(header, ": ") + 2;
      else if (strstr(header, "Low priority: ")  == header)  low_priority  = strstr(header, ": ") + 2;
      else if (strstr(header, "Priority: ")      == header)  priority      = strstr(header, ": ") + 2;
      else if (strstr(header, "Class: ")         == header)  class         = strstr(header, ": ") + 2;
      else if (strstr(header, "Lifespan: ")      == header)  lifespan      = strstr(header, ": ") + 2;
      else if (strstr(header, "Message ID: ")    == header)  message_id    = strstr(header, ": ") + 2;
      else
	fprintf(stderr, "%s: ignoring unrecognised header: %s\n", argv0, header);
    }
  
  r = 0;
  if (command == NULL)
    fprintf(stderr, "%s: ignoring message without command header\n", argv0);
  else if (!strcmp(command, "enumerate-crtcs"))
    {
      if (crtc || coalesce || high_priority || low_priority || priority || class || lifespan)
	fprintf(stderr, "%s: ignoring superfluous headers in Command: enumerate-crtcs message\n", argv0);
      r = enumerate_crtcs(conn, message_id);
    }
  else if (!strcmp(command, "get-gamma-info"))
    {
      if (coalesce || high_priority || low_priority || priority || class || lifespan)
	fprintf(stderr, "%s: ignoring superfluous headers in Command: get-gamma-info message\n", argv0);
      r = get_gamma_info(conn, message_id, crtc);
    }
  else if (!strcmp(command, "get-gamma"))
    {
      if (priority || class || lifespan)
	fprintf(stderr, "%s: ignoring superfluous headers in Command: get-gamma message\n", argv0);
      r = get_gamma(conn, message_id, crtc, coalesce, high_priority, low_priority);
    }
  else if (!strcmp(command, "set-gamma"))
    {
      if (coalesce || high_priority || low_priority)
	fprintf(stderr, "%s: ignoring superfluous headers in Command: set-gamma message\n", argv0);
      r = set_gamma(conn, crtc, priority, class, lifespan);
    }
  else
    fprintf(stderr, "%s: ignoring unrecognised command: Command: %s\n", argv0, command);
  if (r < 0)
    return -1;
  
  goto again;
}


/**
 * The program's main loop
 * 
 * @return  Zero on success, -1 on error
 */
int main_loop(void)
{
  fd_set fds_orig, fds_read, fds_ex;
  int i, r, update, fdn = update_fdset(&fds_orig);
  size_t j;
  
  while (!reexec && !terminate)
    {
      memcpy(&fds_read, &fds_orig, sizeof(fd_set));
      memcpy(&fds_ex,   &fds_orig, sizeof(fd_set));
      if (select(fdn, &fds_read, NULL, &fds_ex, NULL) < 0)
	{
	  if (errno == EINTR)
	    continue;
	  return -1;
	}
      
      update = 0;
      for (i = 0; i < fdn; i++)
	if (FD_ISSET(i, &fds_read) || FD_ISSET(i, &fds_ex))
	  {
	    if (i == socketfd)
	      r = handle_server();
	    else
	      {
		for (j = 0;; j++)
		  if (connections[j] == i)
		    break;
		r = handle_connection(j);
	      }
	    switch (r)
	      {
	      case 0:
		break;
	      case 1:
		update = 1;
		break;
	      default:
		return -1;
	      }
	  }
      if (update)
	update_fdset(&fds_orig);
    }
  
  return 0;
}

