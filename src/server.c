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
 * The clients' connections' inbound-message buffers
 */
struct message* inbound = NULL;

/**
 * The clients' connections' outbound-message buffers
 */
struct ring* outbound = NULL;


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
 * Construct a message
 * 
 * @param  bufp:char**            Output parameter for the buffer, must not have side-effects
 * @param  np:size_t*             Output parameter for the size of the buffer sans `extra`
 * @param  extra:size_t           The extra number for bytes to allocate to the buffer
 * @param  format:string-literal  Message format string
 * @param  ...                    Message formatting arguments
 */
#define MAKE_MESSAGE(bufp, np, extra, format, ...)		\
  do								\
    {								\
      ssize_t m__;						\
      snprintf(NULL, 0, format "%zn", __VA_ARGS__, &m__);	\
      *(bufp) = malloc((size_t)(extra) + (size_t)m__);		\
      if (*(bufp) == NULL)					\
	return -1;						\
      sprintf(*(bufp), format, __VA_ARGS__);			\
      *(np) = (size_t)m__;					\
    }								\
  while (0)
  


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
	message_destroy(inbound + i);
	ring_destroy(outbound + i);
      }
  free(inbound);
  free(outbound);
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
      {
	off += message_marshal(inbound + i, bs ? bs + off : NULL);
	off += ring_marshal(outbound + i, bs ? bs + off : NULL);
      }
  
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
  inbound = NULL;
  
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
      
      inbound = malloc(connections_alloc * sizeof(*inbound));
      if (inbound == NULL)
	return 0;
    }
  
  for (i = 0; i < connections_used; i++)
    if (connections[i] >= 0)
      {
	off += n = message_unmarshal(inbound + i, bs + off);
	if (n == 0)
	  return 0;
	off += n = ring_unmarshal(outbound + i, bs + off);
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
      
      new = realloc(outbound, (connections_alloc + 10) * sizeof(*outbound));
      if (new == NULL)
	goto fail;
      outbound = new;
      ring_initialise(outbound + connections_ptr);
      
      new = realloc(inbound, (connections_alloc + 10) * sizeof(*inbound));
      if (new == NULL)
	goto fail;
      inbound = new;
      connections_alloc += 10;
      if (message_initialise(inbound + connections_ptr))
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
 * Send a message
 * 
 * @param   conn  The index of the connection
 * @param   buf   The data to send
 * @param   n     The size of `buf`
 * @return        Zero on success, -1 on error, 1 if disconncted
 *                EINTR, EAGAIN, EWOULDBLOCK, and ECONNRESET count
 *                as success (ECONNRESET cause 1 to be returned),
 *                and are handled appropriately.
 */
static int send_message(size_t conn, char* buf, size_t n)
{
  struct ring* ring = outbound + conn;
  int fd = connections[conn];
  int saved_errno;
  size_t ptr = 0;
  ssize_t sent;
  size_t chunksize = n;
  size_t sendsize;
  size_t old_n;
  char* old_buf;
    
  while ((old_buf = ring_peek(ring, &old_n)))
    {
      size_t old_ptr = 0;
      while (old_ptr < n)
	{
	  sendsize = old_n - old_ptr < chunksize ? old_n - old_ptr : chunksize;
	  sent = send(fd, old_buf + old_ptr, sendsize, 0);
	  if (sent < 0)
	    {
	      if (errno != EMSGSIZE)
		goto fail;
	      chunksize >>= 1;
	      if (chunksize == 0)
		goto fail;
	      continue;
	    }
	  old_ptr += (size_t)sent;
	  ring_pop(ring, (size_t)sent);
	}
    }
  
  while (ptr < n)
    {
      sendsize = n - ptr < chunksize ? n - ptr : chunksize;
      sent = send(fd, buf + ptr, sendsize, 0);
      if (sent < 0)
	{
	  if (errno != EMSGSIZE)
	    goto fail;
	  chunksize >>= 1;
	  if (chunksize == 0)
	    goto fail;
	  continue;
	}
      ptr += (size_t)sent;
    }
  
  free(buf);
  return 0;
  
 fail:
  switch (errno)
    {
    case EINTR:
    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
#endif
      if (ring_push(ring, buf + ptr, n - ptr) < 0)
	goto proper_fail;
      free(buf);
      return 0;
    case ECONNRESET:
      free(buf);
      if (connection_closed(fd) < 0)
	return -1;
      return 1;
    default:
      break;
    }
 proper_fail:
  saved_errno = errno;
  free(buf);
  errno = saved_errno;
  return -1;
}


/**
 * Continue sending the queued messages
 * 
 * @param   conn  The index of the connection
 * @return        Zero on success, -1 on error, 1 if disconncted
 *                EINTR, EAGAIN, EWOULDBLOCK, and ECONNRESET count
 *                as success (ECONNRESET cause 1 to be returned),
 *                and are handled appropriately.
 */
static inline int continue_send(size_t conn)
{
  return send_message(conn, NULL, 0);
}


/**
 * Send a custom error without an error number
 * 
 * @param   ...  The error description to send
 * @return       1: Client disconnected
 *               0: Success (possibily delayed)
 *               -1: An error occurred
 */
#define send_error(...)  ((send_error)(conn, message_id, __VA_ARGS__))


/**
 * Send a custom error without an error number
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The ID of the message to which this message is a response
 * @param   desc        The error description to send
 * @return              1: Client disconnected
 *                      0: Success (possibily delayed)
 *                      -1: An error occurred
 */
static int (send_error)(size_t conn, const char* message_id, const char* desc)
{
  char* buf;
  size_t n;
  
  MAKE_MESSAGE(&buf, &n, 0,
	       "Command: error\n"
	       "In response to: %s\n"
	       "Error: custom\n"
	       "Length: %zu\n"
	       "\n"
	       "%s\n",
	       message_id, strlen(desc) + 1, desc);
  
  return send_message(conn, buf, n);
}


/**
 * Send a standard error
 * 
 * @param   ...  The value of `errno`, 0 to indicate success
 * @return       1: Client disconnected
 *               0: Success (possibily delayed)
 *               -1: An error occurred
 */
#define send_errno(...)  ((send_errno)(conn, message_id, __VA_ARGS__))


/**
 * Send a standard error
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The ID of the message to which this message is a response
 * @param   number      The value of `errno`, 0 to indicate success
 * @return              1: Client disconnected
 *                      0: Success (possibily delayed)
 *                      -1: An error occurred
 */
static int (send_errno)(size_t conn, const char* message_id, int number)
{
  char* buf;
  size_t n;
  
  MAKE_MESSAGE(&buf, &n, 0,
	       "Command: error\n"
	       "In response to: %s\n"
	       "Error: %i\n"
	       "\n",
	       message_id, number);
  
  return send_message(conn, buf, n);
}
  

/**
 * Handle a ‘Command: enumerate-crtcs’ message
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @return              Zero on success (even if ignored), -1 on error,
 *                      1 if connection closed
 */
static int enumerate_crtcs(size_t conn, char* message_id)
{
  size_t i, n = 0, len;
  char* buf;
  
  for (i = 0; i < outputs_n; i++)
    n += strlen(outputs[i].name) + 1;
  
  MAKE_MESSAGE(&buf, &n, 0,
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
 * Handle a ‘Command: set-gamma’ message
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @param   crtc        The value of the ‘CRTC’ header
 * @return              Zero on success (even if ignored), -1 on error,
 *                      1 if connection closed
 */
static int get_gamma_info(size_t conn, char* message_id, char* crtc)
{
  struct output* output;
  char* buf;
  char depth[3];
  const char* supported;
  size_t n;
  
  if (crtc  == NULL)  return send_error("protocol error: 'CRTC' header omitted");
  
  output = output_find_by_name(crtc, outputs, outputs_n);
  if (output == NULL)
    return send_error("selected CRTC does not exist");
  
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
  
  MAKE_MESSAGE(&buf, &n, 0,
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
  
  return send_message(conn, buf, n);
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
static int get_gamma(size_t conn, char* message_id, char* crtc, char* coalesce,
		     char* high_priority, char* low_priority)
{
  struct output* output;
  int64_t high, low;
  int coal;
  char* buf;
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
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @param   crtc        The value of the ‘CRTC’ header
 * @param   priority    The value of the ‘Priority’ header
 * @param   class       The value of the ‘Class’ header
 * @param   lifespan    The value of the ‘Lifespan’ header
 * @return              Zero on success (even if ignored), -1 on error,
 *                      1 if connection closed
 */
static int set_gamma(size_t conn, char* message_id, char* crtc, char* priority, char* class, char* lifespan)
{
  struct message* msg = inbound + conn;
  struct output* output = NULL;
  struct filter filter;
  char* p;
  char* q;
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
 * Handle event on a connection to a client
 * 
 * @param   conn  The index of the connection
 * @return        1: The connection as closed
 *                0: Successful
 *                -1: Failure
 */
static int handle_connection(size_t conn)
{
  struct message* msg = inbound + conn;
  int r, fd = connections[conn];
  char* command       = NULL;
  char* crtc          = NULL;
  char* coalesce      = NULL;
  char* high_priority = NULL;
  char* low_priority  = NULL;
  char* priority      = NULL;
  char* class         = NULL;
  char* lifespan      = NULL;
  char* message_id    = NULL;
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
      if (connection_closed(fd) < 0)
	return -1;
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
    fprintf(stderr, "%s: ignoring message without Command header\n", argv0);
  else if (message_id == NULL)
    fprintf(stderr, "%s: ignoring message without Message ID header\n", argv0);
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
      r = set_gamma(conn, message_id, crtc, priority, class, lifespan);
    }
  else
    fprintf(stderr, "%s: ignoring unrecognised command: Command: %s\n", argv0, command);
  if (r)
    return r;
  
  goto again;
}


/**
 * The program's main loop
 * 
 * @return  Zero on success, -1 on error
 */
int main_loop(void)
{
  fd_set fds_orig, fds_rd, fds_wr, fds_ex;
  int i, r, update, fdn = update_fdset(&fds_orig);
  size_t j;
  
  while (!reexec && !terminate)
    {
      memcpy(&fds_rd, &fds_orig, sizeof(fd_set));
      memcpy(&fds_ex, &fds_orig, sizeof(fd_set));
      
      FD_ZERO(&fds_wr);
      for (j = 0; j < connections_used; j++)
	if ((connections[j] >= 0) && ring_have_more(outbound + j))
	  FD_SET(connections[j], &fds_wr);
      
      if (select(fdn, &fds_rd, &fds_wr, &fds_ex, NULL) < 0)
	{
	  if (errno == EINTR)
	    continue;
	  return -1;
	}
      
      update = 0;
      for (i = 0; i < fdn; i++)
	{
	  int do_read  = FD_ISSET(i, &fds_rd) || FD_ISSET(i, &fds_ex);
	  int do_write = FD_ISSET(i, &fds_wr);
	  if (do_read || do_write)
	    {
	      if (i == socketfd)
		r = handle_server();
	      else
		{
		  for (j = 0;; j++)
		    if (connections[j] == i)
		      break;
		  r = do_read ? handle_connection(j) : 0;
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
	      if (do_write)
		switch (continue_send(j))
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
	}
      if (update)
	update_fdset(&fds_orig);
    }
  
  return 0;
}

