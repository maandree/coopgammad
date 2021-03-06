/* See LICENSE file for copyright and license details. */
#include "communication.h"
#include "state.h"
#include "servers-coopgamma.h"

#include <sys/socket.h>
#include <errno.h>
#include <string.h>


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
int
send_message(size_t conn, char *restrict buf, size_t n)
{
	struct ring *restrict ring = outbound + conn;
	int fd = connections[conn];
	int saved_errno;
	size_t ptr = 0;
	ssize_t sent;
	size_t chunksize = n;
	size_t sendsize;
	size_t old_n;
	char *old_buf;
	size_t old_ptr;

	while ((old_buf = ring_peek(ring, &old_n))) {
		for (old_ptr = 0; old_ptr < n;) {
			sendsize = old_n - old_ptr < chunksize ? old_n - old_ptr : chunksize;
			sent = send(fd, old_buf + old_ptr, sendsize, MSG_NOSIGNAL);
			if (sent < 0) {
				if (errno == EPIPE)
					errno = ECONNRESET;
				if (errno != EMSGSIZE)
					goto fail;
				chunksize >>= 1;
				if (!chunksize)
					goto fail;
				continue;
			}
			old_ptr += (size_t)sent;
			ring_pop(ring, (size_t)sent);
		}
	}

	while (ptr < n) {
		sendsize = n - ptr < chunksize ? n - ptr : chunksize;
		sent = send(fd, buf + ptr, sendsize, MSG_NOSIGNAL);
		if (sent < 0) {
			if (errno == EPIPE)
				errno = ECONNRESET;
			if (errno != EMSGSIZE)
				goto fail;
			chunksize >>= 1;
			if (!chunksize)
				goto fail;
			continue;
		}
		ptr += (size_t)sent;
	}

	free(buf);
	return 0;

fail:
	switch (errno) {
	case EINTR:
#if defined(EAGAIN)
	case EAGAIN:
#endif
#if defined(EWOULDBLOCK) && (!defined(EAGAIN) || EAGAIN != EWOULDBLOCK)
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
 * Send a custom error without an error number
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The ID of the message to which this message is a response
 * @param   desc        The error description to send
 * @return              1: Client disconnected
 *                      0: Success (possibily delayed)
 *                      -1: An error occurred
 */
int
(send_error)(size_t conn, const char *restrict message_id, const char *restrict desc)
{
	char *restrict buf;
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
 * @param   conn        The index of the connection
 * @param   message_id  The ID of the message to which this message is a response
 * @param   number      The value of `errno`, 0 to indicate success
 * @return              1: Client disconnected
 *                      0: Success (possibily delayed)
 *                      -1: An error occurred
 */
int
(send_errno)(size_t conn, const char *restrict message_id, int number)
{
	char *restrict buf;
	size_t n;

	MAKE_MESSAGE(&buf, &n, 0,
	             "Command: error\n"
	             "In response to: %s\n"
	             "Error: %i\n"
	             "\n",
	             message_id, number);

	return send_message(conn, buf, n);
}
