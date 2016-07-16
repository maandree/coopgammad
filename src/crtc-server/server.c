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
#include "../state.h"
#include "../communication.h"

#include <string.h>



/**
 * Handle a ‘Command: enumerate-crtcs’ message
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @return              Zero on success (even if ignored), -1 on error,
 *                      1 if connection closed
 */
int handle_enumerate_crtcs(size_t conn, const char* restrict message_id)
{
  size_t i, n = 0, len;
  char* restrict buf;
  
  for (i = 0; i < outputs_n; i++)
    n += strlen(outputs[i].name) + 1;
  
  MAKE_MESSAGE(&buf, &n, 0,
	       "Command: crtc-enumeration\n"
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

