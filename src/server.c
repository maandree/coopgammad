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
#include "util.h"

#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>



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
 * Destroy the state of the connections
 * 
 * @param  disconnect  Disconnect all connections?
 */
void server_destroy(int disconnect)
{
  size_t i;
  
  /* TODO */ (void) disconnect;
  
  for (i = 0; i < connections_used; i++)
    if (connections[i] >= 0)
      message_destroy(client_messages + i);
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
  int fd;
  
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
  
  if (connections_ptr == connections_alloc)
    {
      int* new;
      new = realloc(connections, (connections_alloc + 10) * sizeof(*connections));
      if (new == NULL)
	return -1;
      connections = new;
      connections_alloc += 10;
    }
  
  connections[connections_ptr++] = fd;
  while (connections_ptr < connections_used)
    if (connections[connections_ptr] >= 0)
      connections_ptr++;
  if (connections_used < connections_ptr)
    connections_used = connections_ptr;
  
  return 1;
}


/**
 * Handle event on a connection to a client
 * 
 * @param   conn  The index of the connection
 * @return        1: New connection accepted
 *                0: Successful
 *                -1: Failure
 */
static int handle_connection(size_t conn)
{
  /* TODO */
  return 0;
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

