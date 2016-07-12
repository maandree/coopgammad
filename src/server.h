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
#include "message.h"

#include <stddef.h>



/**
 * List of all client's file descriptors
 * 
 * Unused slots, with index less than `connections_used`,
 * should have the value -1 (negative)
 */
extern int* connections;

/**
 * The number of elements allocated for `connections`
 */
extern size_t connections_alloc;

/**
 * The index of the first unused slot in `connections`
 */
extern size_t connections_ptr;

/**
 * The index of the last used slot in `connections`, plus 1
 */
extern size_t connections_used;

/**
 * The server connection's message buffer
 */
extern struct message server_message;

/**
 * The clients' connections' message buffers
 */
extern struct message* client_messages;



/**
 * Initialise the state of the connections
 * 
 * @param  Zero on success, -1 on error
 */
int server_initialise(void);

/**
 * Destroy the state of the connections
 * 
 * @param  disconnect  Disconnect all connections?
 */
void server_destroy(int disconnect);

/**
 * Marshal the state of the connections
 * 
 * @param   buf  Output buffer for the marshalled data,
 *               `NULL` to only measure how many bytes
 *               this buffer needs
 * @return       The number of marshalled bytes
 */
size_t server_marshal(void* buf);

/**
 * Unmarshal the state of the connections
 * 
 * @param   buf  Buffer for the marshalled data
 * @return       The number of unmarshalled bytes, 0 on error
 */
size_t server_unmarshal(const void* buf);

/**
 * The program's main loop
 * 
 * @return  Zero on success, -1 on error
 */
int main_loop(void);

