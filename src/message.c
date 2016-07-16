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
#include "message.h"
#include "util.h"

#include <sys/socket.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>



/**
 * Initialise a message slot so that it can
 * be used by to read messages
 * 
 * @param   this  Memory slot in which to store the new message
 * @return        Non-zero on error, `errno` will be set accordingly
 */
int message_initialise(struct message* this)
{
  this->headers = NULL;
  this->header_count = 0;
  this->payload = NULL;
  this->payload_size = 0;
  this->payload_ptr = 0;
  this->buffer_size = 128;
  this->buffer_ptr = 0;
  this->stage = 0;
  this->buffer = malloc(this->buffer_size);
  if (this->buffer == NULL)
    return -1;
  return 0;
}


/**
 * Release all resources in a message, should
 * be done even if initialisation fails
 * 
 * @param  this  The message
 */
void message_destroy(struct message* this)
{
  size_t i;
  if (this->headers != NULL)
    for (i = 0; i < this->header_count; i++)
      free(this->headers[i]);
  
  free(this->headers);
  free(this->payload);
  free(this->buffer);
}



#if defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wcast-align"
#endif


/**
 * Marshal a message for state serialisation
 * 
 * @param  this  The message
 * @param  buf   Output buffer for the marshalled data,
 *               `NULL` just measure how large the buffers
 *               needs to be
 * @return       The number of marshalled byte
 */
size_t message_marshal(const struct message* this, void* buf)
{
  size_t i, n, off = 0;
  char* bs = buf;
  
  if (bs != NULL)
    *(size_t*)(bs + off) = this->header_count;
  off += sizeof(size_t);
  
  if (bs != NULL)
    *(size_t*)(bs + off) = this->payload_size;
  off += sizeof(size_t);
  
  if (bs != NULL)
    *(size_t*)(bs + off) = this->payload_ptr;
  off += sizeof(size_t);
  
  if (bs != NULL)
    *(size_t*)(bs + off) = this->buffer_ptr;
  off += sizeof(size_t);
  
  if (bs != NULL)
    *(int*)(bs + off) = this->stage;
  off += sizeof(int);
  
  for (i = 0; i < this->header_count; i++)
    {
      n = strlen(this->headers[i]) + 1;
      if (bs != NULL)
	memcpy(bs + off, this->headers[i], n);
      off += n;
    }
  
  if (bs != NULL)
    memcpy(bs + off, this->payload, this->payload_ptr);
  off += this->payload_ptr;
  
  if (bs != NULL)
    memcpy(bs + off, this->buffer, this->buffer_ptr);
  off += this->buffer_ptr;
  
  return off;
}


/**
 * Unmarshal a message for state deserialisation
 * 
 * @param   this  Memory slot in which to store the new message
 * @param   buf   In buffer with the marshalled data
 * @return        The number of unmarshalled bytes, 0 on error
 */
size_t message_unmarshal(struct message* this, const void* buf)
{
  size_t i, n, off = 0, header_count;
  const char* bs = buf;
  
  this->header_count = 0;
  
  header_count = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  this->payload_size = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  this->payload_ptr = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  this->buffer_size = this->buffer_ptr = *(const size_t*)(bs + off);
  off += sizeof(size_t);
  
  this->stage = *(const int*)(bs + off);
  off += sizeof(int);
  
  /* Make sure that the pointers are NULL so that they are
     not freed without being allocated when the message is
     destroyed if this function fails. */
  this->headers = NULL;
  this->payload = NULL;
  this->buffer  = NULL;
  
  /* To 2-power-multiple of 128 bytes. */
  this->buffer_size >>= 7;
  if (this->buffer_size == 0)
    this->buffer_size = 1;
  else
    {
      this->buffer_size -= 1;
      this->buffer_size |= this->buffer_size >> 1;
      this->buffer_size |= this->buffer_size >> 2;
      this->buffer_size |= this->buffer_size >> 4;
      this->buffer_size |= this->buffer_size >> 8;
      this->buffer_size |= this->buffer_size >> 16;
#if SIZE_MAX == UINT64_MAX
      this->buffer_size |= this->buffer_size >> 32;
#endif
      this->buffer_size += 1;
    }
  this->buffer_size <<= 7;
  
  /* Allocate header list, payload and read buffer. */
  
  if (header_count > 0)
    if (!(this->headers = malloc(header_count * sizeof(char*))))
      goto fail;
  
  if (this->payload_size > 0)
    if (!(this->payload = malloc(this->payload_size)))
      goto fail;
  
  if (!(this->buffer = malloc(this->buffer_size)))
    goto fail;
  
  /* Fill the header list, payload and read buffer. */
  
  for (i = 0; i < header_count; i++)
    {
      n = strlen(bs + off) + 1;
      this->headers[i] = memdup(bs + off, n);
      if (this->headers[i] == NULL)
	goto fail;
      off += n;
      this->header_count++;
    }
  
  memcpy(this->payload, bs + off, this->payload_ptr);
  off += this->payload_ptr;
  
  memcpy(this->buffer, bs + off, this->buffer_ptr);
  off += this->buffer_ptr;
  
  return off;
  
 fail:
  return 0;
}


#if defined(__clang__)
# pragma GCC diagnostic pop
#endif



/**
 * Extend the header list's allocation
 * 
 * @param   this    The message
 * @param   extent  The number of additional entries
 * @return          Zero on success, -1 on error
 */
static int extend_headers(struct message* this, size_t extent)
{
  char** new;
  if (!(new = realloc(this->headers, (this->header_count + extent) * sizeof(char*))))
    return -1;
  this->headers = new;
  return 0;
}


/**
 * Extend the read buffer by way of doubling
 * 
 * @param   this  The message
 * @return        Zero on success, -1 on error
 */
static int extend_buffer(struct message* this)
{
  char* new;
  if (!(new = realloc(this->buffer, (this->buffer_size << 1) * sizeof(char))))
    return -1;
  this->buffer = new;
  this->buffer_size <<= 1;
  return 0;
}


/**
 * Reset the header list and the payload
 * 
 * @param  this  The message
 */
static void reset_message(struct message* this)
{
  size_t i;
  if (this->headers != NULL)
    for (i = 0; i < this->header_count; i++)
      free(this->headers[i]);
  free(this->headers);
  this->headers = NULL;
  this->header_count = 0;
  
  free(this->payload);
  this->payload = NULL;
  this->payload_size = 0;
  this->payload_ptr = 0;
}


/**
 * Read the headers the message and determine, and store, its payload's length
 * 
 * @param   this  The message
 * @return        Zero on success, negative on error (malformated message: unrecoverable state)
 */
#if defined(__GNUC__)
__attribute__((pure))
#endif
static int get_payload_length(struct message* this)
{
  char* header;
  size_t i;
  
  for (i = 0; i < this->header_count; i++)
    if (strstr(this->headers[i], "Length: ") == this->headers[i])
      {
	/* Store the message length. */
	header = this->headers[i] + strlen("Length: ");
	this->payload_size = (size_t)atol(header);
	
	/* Do not except a length that is not correctly formated. */
	for (; *header; header++)
	  if ((*header < '0') || ('9' < *header))
	    return -2; /* Malformated value, enters unrecoverable state. */
	
	/* Stop searching for the ‘Length’ header, we have found and parsed it. */
	break;
      }
  
  return 0;
}


/**
 * Verify that a header is correctly formatted
 * 
 * @param   header  The header, must be NUL-terminated
 * @param   length  The length of the header
 * @return          Zero if valid, negative if invalid (malformated message: unrecoverable state)
 */
#if defined(__GNUC__)
__attribute__((pure))
#endif
static int validate_header(const char* header, size_t length)
{
  char* p = memchr(header, ':', length * sizeof(char));
  
  if (verify_utf8(header, 0) < 0)
    /* Either the string is not UTF-8, or your are under an UTF-8 attack,
       let's just call this unrecoverable because the client will not correct. */
    return -2;
  
  if ((p == NULL) || /* Buck you, rawmemchr should not segfault the program. */
      (p[1] != ' ')) /* Also an invalid format. ' ' is mandated after the ':'. */
    return -2;
  
  return 0;
}


/**
 * Remove the beginning of the read buffer
 * 
 * @param  this        The message
 * @param  length      The number of characters to remove  
 * @param  update_ptr  Whether to update the buffer pointer
 */
static void unbuffer_beginning(struct message* this, size_t length, int update_ptr)
{
  memmove(this->buffer, this->buffer + length, (this->buffer_ptr - length) * sizeof(char));
  if (update_ptr)
    this->buffer_ptr -= length;
}


/**
 * Remove the header–payload delimiter from the buffer,
 * get the payload's size and allocate the payload
 * 
 * @param   this  The message
 * @return        The return value follows the rules of `message_read`
 */
static int initialise_payload(struct message* this)
{
  /* Remove the \n (end of empty line) we found from the buffer. */
  unbuffer_beginning(this, 1, 1);
  
  /* Get the length of the payload. */
  if (get_payload_length(this) < 0)
    return -2; /* Malformated value, enters unrecoverable state. */
  
  /* Allocate the payload buffer. */
  if (this->payload_size > 0)
    if (!(this->payload = malloc(this->payload_size)))
      return -1;
  
  return 0;
}


/**
 * Create a header from the buffer and store it
 * 
 * @param   this    The message
 * @param   length  The length of the header, including LF-termination
 * @return          The return value follows the rules of `message_read`
 */
static int store_header(struct message* this, size_t length)
{
  char* header;
  
  /* Allocate the header. */
  if (!(header = malloc(length))) /* Last char is a LF, which is substituted with NUL. */
    return -1;
  /* Copy the header data into the allocated header, */
  memcpy(header, this->buffer, length * sizeof(char));
  /* and NUL-terminate it. */
  header[length - 1] = '\0';
  
  /* Remove the header data from the read buffer. */
  unbuffer_beginning(this, length, 1);
  
  /* Make sure the the header syntax is correct so that
     the program does not need to care about it. */
  if (validate_header(header, length))
    {
      free(header);
      return -2;
    }
  
  /* Store the header in the header list. */
  this->headers[this->header_count++] = header;
  
  return 0;
}


/**
 * Continue reading from the socket into the buffer
 * 
 * @param   this  The message
 * @param   fd    The file descriptor of the socket
 * @return        The return value follows the rules of `message_read`
 */
static int continue_read(struct message* this, int fd)
{
  size_t n;
  ssize_t got;
  int r;
  
  /* Figure out how much space we have left in the read buffer. */
  n = this->buffer_size - this->buffer_ptr;
  
  /* If we do not have too much left, */
  if (n < 128)
    {
      /* grow the buffer, */
      if ((r = extend_buffer(this)) < 0)
	return r;
      
      /* and recalculate how much space we have left. */
      n = this->buffer_size - this->buffer_ptr;
    }
  
  /* Then read from the socket. */
  errno = 0;
  got = recv(fd, this->buffer + this->buffer_ptr, n, 0);
  this->buffer_ptr += (size_t)(got < 0 ? 0 : got);
  if (errno)
    return -1;
  if (got == 0)
    {
      errno = ECONNRESET;
      return -1;
    }
  
  return 0;
}


/**
 * Read the next message from a file descriptor
 * 
 * @param   this  Memory slot in which to store the new message
 * @param   fd    The file descriptor
 * @return        0:  At least one message is available
 *                -1: Exceptional connection:
 *                  EINTR:        System call interrupted
 *                  EAGAIN:       No message is available
 *                  EWOULDBLOCK:  No message is available
 *                  ECONNRESET:   Connection closed
 *                  Other:        Failure
 *                -2: Corrupt message (unrecoverable)
 */
int message_read(struct message* this, int fd)
{
  size_t header_commit_buffer = 0;
  int r;
  
  /* If we are at stage 2, we are done and it is time to start over.
     This is important because the function could have been interrupted. */
  if (this->stage == 2)
    {
      reset_message(this);
      this->stage = 0;
    }
  
  /* Read from file descriptor until we have a full message. */
  for (;;)
    {
      char* p;
      size_t length;
      
      /* Stage 0: headers. */
      /* Read all headers that we have stored into the read buffer. */
      while ((this->stage == 0) &&
	     ((p = memchr(this->buffer, '\n', this->buffer_ptr * sizeof(char))) != NULL))
	if ((length = (size_t)(p - this->buffer)))
	  {
	    /* We have found a header. */
	    
	    /* On every eighth header found with this function call,
	       we prepare the header list for eight more headers so
	       that it does not need to be reallocated again and again. */
	    if (header_commit_buffer == 0)
	      if ((r = extend_headers(this, header_commit_buffer = 8)) < 0)
		return r;
	    
	    /* Create and store header. */
	    if ((r = store_header(this, length + 1)) < 0)
	      return r;
	    header_commit_buffer -= 1;
	  }
	else
	  {
	    /* We have found an empty line, i.e. the end of the headers. */
	    
	    /* Remove the header–payload delimiter from the buffer,
	       get the payload's size and allocate the payload. */
	    if ((r = initialise_payload(this)) < 0)
	      return r;
	    
	    /* Mark end of stage, next stage is getting the payload. */
	    this->stage = 1;
	  }
      
      
      /* Stage 1: payload. */
      if ((this->stage == 1) && (this->payload_size > 0))
	{
	  /* How much of the payload that has not yet been filled. */
	  size_t need = this->payload_size - this->payload_ptr;
	  /* How much we have of that what is needed. */
	  size_t move = this->buffer_ptr < need ? this->buffer_ptr : need;
	  
	  /* Copy what we have, and remove it from the the read buffer. */
	  memcpy(this->payload + this->payload_ptr, this->buffer, move * sizeof(char));
	  unbuffer_beginning(this, move, 1);
	  
	  /* Keep track of how much we have read. */
	  this->payload_ptr += move;
	}
      if ((this->stage == 1) && (this->payload_ptr == this->payload_size))
	{
	  /* If we have filled the payload (or there was no payload),
	     mark the end of this stage, i.e. that the message is
	     complete, and return with success. */
	  this->stage = 2;
	  return 0;
	}
      
      
      /* If stage 1 was not completed. */
      
      /* Continue reading from the socket into the buffer. */
      if ((r = continue_read(this, fd)) < 0)
	return r;
    }
}

