/* See LICENSE file for copyright and license details. */
#ifndef SERVERS_MASTER_H
#define SERVERS_MASTER_H

/**
 * Disconnect all clients
 */
void disconnect_all(void);

/**
 * The program's main loop
 * 
 * @return  Zero on success, -1 on error
 */
int main_loop(void);

#endif
