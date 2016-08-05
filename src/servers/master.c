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
#include "master.h"
#include "crtc.h"
#include "gamma.h"
#include "coopgamma.h"
#include "../util.h"
#include "../communication.h"
#include "../state.h"

#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



/**
 * All poll(3p) events that are not for writing
 */
#define NON_WR_POLL_EVENTS  (POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI | POLLERR | POLLHUP | POLLNVAL)


/**
 * Extract headers from an inbound message and pass
 * them on to appropriate message handling function
 * 
 * @param   conn  The index of the connection
 * @param   msg   The inbound message
 * @return        1: The connection as closed
 *                0: Successful
 *                -1: Failure
 */
static int dispatch_message(size_t conn, struct message* restrict msg)
{
  size_t i;
  int r = 0;
  const char* header;
  const char* value;
  const char* command       = NULL;
  const char* crtc          = NULL;
  const char* coalesce      = NULL;
  const char* high_priority = NULL;
  const char* low_priority  = NULL;
  const char* priority      = NULL;
  const char* class         = NULL;
  const char* lifespan      = NULL;
  const char* message_id    = NULL;
  
  for (i = 0; i < msg->header_count; i++)
    {
      value = strstr((header = msg->headers[i]), ": ") + 2;
      if      (strstr(header, "Command: ")       == header)  command       = value;
      else if (strstr(header, "CRTC: ")          == header)  crtc          = value;
      else if (strstr(header, "Coalesce: ")      == header)  coalesce      = value;
      else if (strstr(header, "High priority: ") == header)  high_priority = value;
      else if (strstr(header, "Low priority: ")  == header)  low_priority  = value;
      else if (strstr(header, "Priority: ")      == header)  priority      = value;
      else if (strstr(header, "Class: ")         == header)  class         = value;
      else if (strstr(header, "Lifespan: ")      == header)  lifespan      = value;
      else if (strstr(header, "Message ID: ")    == header)  message_id    = value;
      else if (strstr(header, "Length: ")        == header)  ;/* Handled transparently */
      else
	fprintf(stderr, "%s: ignoring unrecognised header: %s\n", argv0, header);
    }
  
  if (command == NULL)
    fprintf(stderr, "%s: ignoring message without Command header\n", argv0);
  else if (message_id == NULL)
    fprintf(stderr, "%s: ignoring message without Message ID header\n", argv0);
  else if (!strcmp(command, "enumerate-crtcs"))
    {
      if (crtc || coalesce || high_priority || low_priority || priority || class || lifespan)
	fprintf(stderr, "%s: ignoring superfluous headers in Command: enumerate-crtcs message\n", argv0);
      r = handle_enumerate_crtcs(conn, message_id);
    }
  else if (!strcmp(command, "get-gamma-info"))
    {
      if (coalesce || high_priority || low_priority || priority || class || lifespan)
	fprintf(stderr, "%s: ignoring superfluous headers in Command: get-gamma-info message\n", argv0);
      r = handle_get_gamma_info(conn, message_id, crtc);
    }
  else if (!strcmp(command, "get-gamma"))
    {
      if (priority || class || lifespan)
	fprintf(stderr, "%s: ignoring superfluous headers in Command: get-gamma message\n", argv0);
      r = handle_get_gamma(conn, message_id, crtc, coalesce, high_priority, low_priority);
    }
  else if (!strcmp(command, "set-gamma"))
    {
      if (coalesce || high_priority || low_priority)
	fprintf(stderr, "%s: ignoring superfluous headers in Command: set-gamma message\n", argv0);
      r = handle_set_gamma(conn, message_id, crtc, priority, class, lifespan);
    }
  else
    fprintf(stderr, "%s: ignoring unrecognised command: Command: %s\n", argv0, command);
  
  return r;
}


/**
 * Sets the file descriptor set that includes
 * the server socket and all connections
 * 
 * The file descriptor will be ordered as in
 * the array `connections`, `socketfd` will
 * be last.
 * 
 * @param   fds        Reference parameter for the array of file descriptors
 * @param   fdn        Output parameter for the number of file descriptors
 * @parma   fds_alloc  Reference parameter for the allocation size of `fds`, in elements
 * @return             Zero on success, -1 on error
 */
static int update_fdset(struct pollfd** restrict fds, nfds_t* restrict fdn, nfds_t* restrict fds_alloc)
{
  size_t i;
  nfds_t j = 0;
  
  if (connections_used + 1 > *fds_alloc)
    {
      void* new = realloc(*fds, (connections_used + 1) * sizeof(**fds));
      if (new == NULL)
	return -1;
      *fds = new;
      *fds_alloc = connections_used + 1;
    }
  
  for (i = 0; i < connections_used; i++)
    if (connections[i] >= 0)
      {
	(*fds)[j].fd = connections[i];
	(*fds)[j].events = NON_WR_POLL_EVENTS;
	j++;
      }
  
  (*fds)[j].fd = socketfd;
  (*fds)[j].events = NON_WR_POLL_EVENTS;
  j++;
  
  *fdn = j;
  return 0;
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
  else
    {
      connections[connections_ptr] = fd;
      ring_initialise(outbound + connections_ptr);
      if (message_initialise(inbound + connections_ptr))
	goto fail;
    }
  
  connections_ptr++;
  while ((connections_ptr < connections_used) && (connections[connections_ptr] >= 0))
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
 * Handle event on a connection to a client
 * 
 * @param   conn  The index of the connection
 * @return        1: The connection as closed
 *                0: Successful
 *                -1: Failure
 */
static int handle_connection(size_t conn)
{
  struct message* restrict msg = inbound + conn;
  int r, fd = connections[conn];
  
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
      while ((connections_used > 0) && (connections[connections_used - 1] < 0))
	connections_used -= 1;
      message_destroy(msg);
      ring_destroy(outbound + conn);
      if (connection_closed(fd) < 0)
	return -1;
      return 1;
    }
  
  if ((r = dispatch_message(conn, msg)))
    return r;
  
  goto again;
}



/**
 * Disconnect all clients
 */
void disconnect_all(void)
{
  size_t i;
  for (i = 0; i < connections_used; i++)
    if (connections[i] >= 0)
      {
	shutdown(connections[i], SHUT_RDWR);
	close(connections[i]);
      }
}


/**
 * The program's main loop
 * 
 * @return  Zero on success, -1 on error
 */
int main_loop(void)
{
  struct pollfd* fds = NULL;
  nfds_t i, fdn = 0, fds_alloc = 0;
  int r, update, saved_errno;
  size_t j;
  
  if (update_fdset(&fds, &fdn, &fds_alloc) < 0)
    goto fail;
  
  while (!reexec && !terminate)
    {
      if (connection)
	{
	  if ((connection == 1 ? disconnect() : reconnect()) < 0)
	    {
	      connection = 0;
	      goto fail;
	    }
	  connection = 0;
	}
      
      for (j = 0, i = 0; j < connections_used; j++)
	if (connections[j] >= 0)
	  {
	    fds[i].revents = 0;
	    if (ring_have_more(outbound + j))
	      fds[(size_t)i++ + j].events |= POLLOUT;
	    else
	      fds[(size_t)i++ + j].events &= ~POLLOUT;
	  }
      fds[i].revents = 0;
      
      if (poll(fds, fdn, -1) < 0)
	{
	  if (errno == EAGAIN)
	    perror(argv0);
	  else if (errno != EINTR)
	    goto fail;
	}
      
      update = 0;
      for (i = 0; i < fdn; i++)
	{
	  int do_read  = fds[i].revents & NON_WR_POLL_EVENTS;
	  int do_write = fds[i].revents & POLLOUT;
	  int fd = fds[i].fd;
	  if (!do_read && !do_write)
	    continue;
	  
	  if (fd == socketfd)
	    r = handle_server();
	  else
	    {
	      for (j = 0; connections[j] != fd; j++);
	      r = do_read ? handle_connection(j) : 0;
	    }
	  
	  if ((r >= 0) && do_write)
	    r |= continue_send(j);
	  if (r < 0)
	    goto fail;
	  update |= (r > 0);
	}
      if (update)
	if (update_fdset(&fds, &fdn, &fds_alloc) < 0)
	  goto fail;
    }
  
  free(fds);
  return 0;
 fail:
  saved_errno = errno;
  free(fds);
  errno = saved_errno;
  return -1;
}

