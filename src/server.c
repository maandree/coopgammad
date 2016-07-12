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
 * The server connection's message buffer
 */
struct message server_message;

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
 * Initialise the state of the connections
 * 
 * @param  Zero on success, -1 on error
 */
int server_initialise(void)
{
  return message_initialise(&server_message);
}


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
  message_destroy(&server_message);
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
  
  off += message_marshal(&server_message, bs ? bs + off : NULL);
  
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
  memset(&server_message, 0, sizeof(server_message));
  
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
  
  off += n = message_unmarshal(&server_message, bs + off);
  if (n == 0)
    return 0;
  
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
 * The program's main loop
 * 
 * @return  Zero on success, -1 on error
 */
int main_loop(void)
{
  fd_set fds_orig, fds_read, fds_err;
  int fdn = update_fdset(&fds_orig);
  
  while (!reexec && !terminate)
    {
      memcpy(&fds_read, &fds_orig, sizeof(fd_set));
      memcpy(&fds_err, &fds_orig, sizeof(fd_set));
      if (select(fdn, &fds_read, NULL, &fds_err, NULL) < 0)
	{
	  if (errno == EINTR)
	    continue;
	  goto fail;
	}
      /* TODO */
    }
  
  return 0;
 fail:
  return -1;
}

