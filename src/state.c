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
#include "state.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>



/**
 * The name of the process
 */
char* restrict argv0; /* do not marshal */

/**
 * The real pathname of the process's binary,
 * `NULL` if `argv0` is satisfactory
 */
char* restrict argv0_real = NULL;

/**
 * Array of all outputs
 */
struct output* restrict outputs = NULL;

/**
 * The nubmer of elements in `outputs`
 */
size_t outputs_n = 0;

/**
 * The server socket's file descriptor
 */
int socketfd = -1;

/**
 * Has the process receive a signal
 * telling it to re-execute?
 */
volatile sig_atomic_t reexec = 0; /* do not marshal */

/**
 * Has the process receive a signal
 * telling it to terminate?
 */
volatile sig_atomic_t terminate = 0; /* do not marshal */

/**
 * Has the process receive a to
 * disconnect from or reconnect to
 * the site? 1 if disconnct, 2 if
 * reconnect, 0 otherwise.
 */
volatile sig_atomic_t connection = 0;

/**
 * List of all client's file descriptors
 * 
 * Unused slots, with index less than `connections_used`,
 * should have the value -1 (negative)
 */
int* restrict connections = NULL;

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
struct message* restrict inbound = NULL;

/**
 * The clients' connections' outbound-message buffers
 */
struct ring* restrict outbound = NULL;

/**
 * Is the server connect to the display?
 * 
 * Set to true before the initial connection
 */
int connected = 1;

/**
 * The adjustment method, -1 for automatic
 */
int method = -1;

/**
 * The site's name, may be `NULL`
 */
char* restrict sitename = NULL;

/**
 * The libgamma site state
 */
libgamma_site_state_t site; /* do not marshal */

/**
 * The libgamma partition states
 */
libgamma_partition_state_t* restrict partitions = NULL; /* do not marshal */

/**
 * The libgamma CRTC states
 */
libgamma_crtc_state_t* restrict crtcs = NULL; /* do not marshal */

/**
 * Preserve gamma ramps at priority 0?
 */
int preserve = 0;



/**
 * Destroy the state
 */
void state_destroy(void)
{
  size_t i;
  
  for (i = 0; i < connections_used; i++)
    if (connections[i] >= 0)
      {
	message_destroy(inbound + i);
	ring_destroy(outbound + i);
      }
  free(inbound);
  free(outbound);
  free(connections);
  
  if (outputs != NULL)
    for (i = 0; i < outputs_n; i++)
      output_destroy(outputs + i);
  free(outputs);
  if (crtcs != NULL)
    for (i = 0; i < outputs_n; i++)
      libgamma_crtc_destroy(crtcs + i);
  free(crtcs);
  if (partitions != NULL)
    for (i = 0; i < site.partitions_available; i++)
      libgamma_partition_destroy(partitions + i);
  free(partitions);
  libgamma_site_destroy(&site);
  
  free(sitename);
}


#if defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wcast-align"
#endif


/**
 * Marshal the state
 * 
 * @param   buf  Output buffer for the marshalled data,
 *               `NULL` to only measure how many bytes
 *               this buffer needs
 * @return       The number of marshalled bytes
 */
size_t state_marshal(void* restrict buf)
{
  size_t off = 0, i, n;
  char* restrict bs = buf;
  
  if (argv0_real == NULL)
    {
      if (bs != NULL)
	*(bs + off) = '\0';
      off += 1;
    }
  else
    {
      n = strlen(argv0_real) + 1;
      if (bs != NULL)
	memcpy(bs + off, argv0_real, n);
      off += n;
    }
  
  if (bs != NULL)
    *(size_t*)(bs + off) = outputs_n;
  off += sizeof(size_t);
  
  for (i = 0; i < outputs_n; i++)
    off += output_marshal(outputs + i, bs ? bs + off : NULL);
  
  if (bs != NULL)
    *(int*)(bs + off) = socketfd;
  off += sizeof(int);
  
  if (bs != NULL)
    *(sig_atomic_t*)(bs + off) = connection;
  off += sizeof(sig_atomic_t);
  
  if (bs != NULL)
    *(int*)(bs + off) = connected;
  off += sizeof(int);
  
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
  
  if (bs != NULL)
    *(int*)(bs + off) = method;
  off += sizeof(int);
  
  if (bs != NULL)
    *(int*)(bs + off) = sitename != NULL;
  off += sizeof(int);
  if (sitename != NULL)
    {
      n = strlen(sitename) + 1;
      if (bs != NULL)
	memcpy(bs + off, sitename, n);
      off += n;
    }
  
  if (bs != NULL)
    *(int*)(bs + off) = preserve;
  off += sizeof(int);
  
  return off;
}


/**
 * Unmarshal the state
 * 
 * @param   buf  Buffer for the marshalled data
 * @return       The number of unmarshalled bytes, 0 on error
 */
size_t state_unmarshal(const void* restrict buf)
{
  size_t off = 0, i, n;
  const char* restrict bs = buf;
  
  connections = NULL;
  inbound = NULL;
  
  if (*(bs + off))
    {
      off += 1;
      n = strlen(bs + off) + 1;
      if (!(argv0_real = memdup(bs + off, n)))
	return 0;
      off += n;
    }
  else
    off += 1;
  
  outputs_n = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  for (i = 0; i < outputs_n; i++)
    {
      off += n = output_unmarshal(outputs + i, bs + off);
      if (n == 0)
	return 0;
    }
  
  socketfd = *(const int*)(bs + off);
  off += sizeof(int);
  
  connection = *(const sig_atomic_t*)(bs + off);
  off += sizeof(sig_atomic_t);
  
  connected = *(const int*)(bs + off);
  off += sizeof(int);
  
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
  
  method = *(const int*)(bs + off);
  off += sizeof(int);
  
  if (*(const int*)(bs + off))
    {
      off += sizeof(int);
      n = strlen(bs + off) + 1;
      if (!(sitename = memdup(bs + off, n)))
	return 0;
      off += n;
    }
  else
    off += sizeof(int);
  
  preserve = *(const int*)(bs + off);
  off += sizeof(int);
  
  return off;
}


#if defined(__clang__)
# pragma GCC diagnostic pop
#endif

