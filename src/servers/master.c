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

#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



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
 * @param   fds  The file descritor set
 * @return       The highest set file descritor plus 1
 */
GCC_ONLY(__attribute__((nonnull)))
static int update_fdset(fd_set* restrict fds)
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
      if (conn == connections_used)
	connections_used -= 1;
      message_destroy(msg);
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
  fd_set fds_orig, fds_rd, fds_wr, fds_ex;
  int i, r, update, fdn = update_fdset(&fds_orig);
  size_t j;
  
  while (!reexec && !terminate)
    {
      if (connection)
	{
	  if ((connection == 1 ? disconnect() : reconnect()) < 0)
	    return connection = 0, -1;
	  connection = 0;
	}
      
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
	  if (!do_read && !do_write)
	    continue;
	  
	  if (i == socketfd)
	    r = handle_server();
	  else
	    {
	      for (j = 0; connections[j] != i; j++);
	      r = do_read ? handle_connection(j) : 0;
	    }
	  
	  if ((r >= 0) && do_write)
	    r |= continue_send(j);
	  if (r < 0)
	    return -1;
	  update |= (r > 0);
	}
      if (update)
	update_fdset(&fds_orig);
    }
  
  return 0;
}

