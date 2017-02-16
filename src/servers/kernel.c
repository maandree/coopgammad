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
#include "kernel.h"
#include "../state.h"
#include "../util.h"

#include <libgamma.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



/**
 * Get the pathname of the runtime file
 * 
 * @param   suffix  The suffix for the file
 * @return          The pathname of the file, `NULL` on error
 */
GCC_ONLY(__attribute__((malloc, nonnull)))
static char* get_pathname(const char* restrict suffix)
{
  const char* restrict rundir = getenv("XDG_RUNTIME_DIR");
  const char* restrict username = "";
  char* name = NULL;
  char* p;
  char* restrict rc;
  struct passwd* restrict pw;
  size_t n;
  int saved_errno;
  
  if (sitename)
    {
      name = memdup(sitename, strlen(sitename) + 1);
      if (name == NULL)
	goto fail;
    }
  else if ((name = libgamma_method_default_site(method)))
    {
      name = memdup(name, strlen(name) + 1);
      if (name == NULL)
	goto fail;
    }
  
  if (name != NULL)
    switch (method)
      {
      case LIBGAMMA_METHOD_X_RANDR:
      case LIBGAMMA_METHOD_X_VIDMODE:
	if ((p = strrchr(name, ':')))
	  if ((p = strchr(p, '.')))
	    *p = '\0';
	break;
      default:
	break;
      }
  
  if (!rundir || !*rundir)
    rundir = "/tmp";
  
  if ((pw = getpwuid(getuid())))
    username = pw->pw_name ? pw->pw_name : "";
  
  n = sizeof("/.coopgammad/~/.") + 3 * sizeof(int);
  n += strlen(rundir) + strlen(username) + ((name != NULL) ? strlen(name) : 0) + strlen(suffix);
  if (!(rc = malloc(n)))
    goto fail;
  sprintf(rc, "%s/.coopgammad/~%s/%i%s%s%s",
	  rundir, username, method, name ? "." : "", name ? name : "", suffix);
  free(name);
  return rc;
  
 fail:
  saved_errno = errno;
  free(name);
  errno = saved_errno;
  return NULL;
}


/**
 * Get the pathname of the socket
 * 
 * @return  The pathname of the socket, `NULL` on error
 */
char* get_socket_pathname(void)
{
  return get_pathname(".socket");
}


/**
 * Get the pathname of the PID file
 * 
 * @return  The pathname of the PID file, `NULL` on error
 */
char* get_pidfile_pathname(void)
{
  return get_pathname(".pid");
}


/**
 * Get the pathname of the state file
 * 
 * @return  The pathname of the state file, `NULL` on error
 */
char* get_state_pathname(void)
{
  return get_pathname(".state");
}


/**
 * Check whether a PID file is outdated
 * 
 * @param   pidpath  The PID file
 * @param   token    An environment variable (including both key and value)
 *                   that must exist in the process if it is a coopgammad process
 * @return           -1: An error occurred
 *                    0: The service is already running
 *                    1: The PID file is outdated
 */
GCC_ONLY(__attribute__((nonnull)))
static int is_pidfile_reusable(const char* restrict pidpath, const char* restrict token)
{
  /* PORTERS: /proc/$PID/environ is Linux specific */
  
  char temp[sizeof("/proc//environ") + 3 * sizeof(long long int)];
  int fd = -1, saved_errno, tries = 0;
  char* content = NULL;
  char* p;
  pid_t pid = 0;
  size_t n;
#if defined(HAVE_LINUX_PROCFS)
  char* end;
#else
  (void) token;
#endif
  
  /* Get PID */
 retry:
  fd = open(pidpath, O_RDONLY);
  if (fd < 0)
    return -1;
  content = nread(fd, &n);
  if (content == NULL)
    goto fail;
  close(fd), fd = -1;
  
  if (n == 0)
    {
      if (++tries > 1)
	goto bad;
      msleep(100); /* 1 tenth of a second */
      goto retry;
    }
  
  if (('0' > content[0]) || (content[0] > '9'))
    goto bad;
  if ((content[0] == '0') && ('0' <= content[1]) && (content[1] <= '9'))
    goto bad;
  for (p = content; *p; p++)
    if (('0' <= *p) && (*p <= '9'))
      pid = pid * 10 + (*p & 15);
    else
      break;
  if (*p++ != '\n')
    goto bad;
  if (*p)
    goto bad;
  if ((size_t)(p - content) != n)
    goto bad;
  sprintf(temp, "%llu\n", (unsigned long long)pid);
  if (strcmp(content, temp))
    goto bad;
  free(content);
  
  /* Validate PID */
#if defined(HAVE_LINUX_PROCFS)
  sprintf(temp, "/proc/%llu/environ", (unsigned long long)pid);
  fd = open(temp, O_RDONLY);
  if (fd < 0)
    return ((errno == ENOENT) || (errno == EACCES)) ? 1 : -1;
  content = nread(fd, &n);
  if (content == NULL)
    goto fail;
  close(fd), fd = -1;
  
  for (end = (p = content) + n; p != end; p = strchr(p, '\0') + 1)
    if (!strcmp(p, token))
      return free(content), 0;
  free(content);
#else
  if ((kill(pid, 0) == 0) || (errno == EINVAL))
    return 0;
#endif
  
  return 1;
 bad:
  fprintf(stderr, "%s: pid file contains invalid content: %s\n", argv0, pidpath);
  errno = 0;
  return -1;
 fail:
  saved_errno = errno;
  free(content);
  if (fd >= 0)
    close(fd);
  errno = saved_errno;
  return -1;
}


/**
 * Create PID file
 * 
 * @param   pidpath  The pathname of the PID file
 * @return           Zero on success, -1 on error,
 *                   -2 if the service is already running
 */
int create_pidfile(char* pidpath)
{
  int fd = -1, r, saved_errno;
  char* p;
  char* restrict token = NULL;
  
  /* Create token used to validate the service. */
  token = malloc(sizeof("COOPGAMMAD_PIDFILE_TOKEN=") + strlen(pidpath));
  if (token == NULL)
    return -1;
  sprintf(token, "COOPGAMMAD_PIDFILE_TOKEN=%s", pidpath);
#if !defined(USE_VALGRIND)
  if (putenv(token))
    goto putenv_fail;
  /* `token` must not be free! */
#else
  {
    static char static_token[sizeof("COOPGAMMAD_PIDFILE_TOKEN=") + PATH_MAX];
    if (strlen(pidpath) > PATH_MAX)
      abort();
    strcpy(static_token, token);
    if (putenv(static_token))
      goto fail;
  }
#endif
  
  /* Create PID file's directory. */
  for (p = pidpath; *p == '/'; p++);
  while ((p = strchr(p, '/')))
    {
      *p = '\0';
      if (mkdir(pidpath, 0755) < 0)
	if (errno != EEXIST)
	  {
	    *p = '/';
	    goto fail;
	  }
      *p++ = '/';
    }
  
  /* Create PID file. */
 retry:
  fd = open(pidpath, O_CREAT | O_EXCL | O_WRONLY, 0644);
  if (fd < 0)
    {
      if (errno == EINTR)
	goto retry;
      if (errno != EEXIST)
	return -1;
      r = is_pidfile_reusable(pidpath, token);
      if (r > 0)
	{
	  unlink(pidpath);
	  goto retry;
	}
      else if (r < 0)
	goto fail;
      fprintf(stderr, "%s: service is already running\n", argv0);
      errno = 0;
      return -2;
    }
  
  /* Write PID to PID file. */
  if (dprintf(fd, "%llu\n", (unsigned long long)getpid()) < 0)
    goto fail;
  
  /* Done */
#if defined(USE_VALGRIND)
  free(token);
#endif
  if (close(fd) < 0)
    if (errno != EINTR)
      return -1;
  return 0;
#if !defined(USE_VALGRIND)
 putenv_fail:
  saved_errno = errno;
  free(token);
  errno = saved_errno;
#endif
 fail:
  saved_errno = errno;
#if defined(USE_VALGRIND)
  free(token);
#endif
  if (fd >= 0)
    {
      close(fd);
      unlink(pidpath);
    }
  errno = saved_errno;
  return -1;
}


/**
 * Create socket and start listening
 * 
 * @param   socketpath  The pathname of the socket
 * @return              Zero on success, -1 on error
 */
int create_socket(const char* socketpath)
{
  struct sockaddr_un address;
  
  address.sun_family = AF_UNIX;
  if (strlen(socketpath) >= sizeof(address.sun_path))
    {
      errno = ENAMETOOLONG;
      return -1;
    }
  strcpy(address.sun_path, socketpath);
  unlink(socketpath);
  if ((socketfd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
    return -1;
  if (fchmod(socketfd, S_IRWXU) < 0)
    return -1;
  if (bind(socketfd, (struct sockaddr*)(&address), (socklen_t)sizeof(address)) < 0)
    return -1;
  if (listen(socketfd, SOMAXCONN) < 0)
    return -1;
  
  return 0;
}


/**
 * Close and unlink the socket
 * 
 * @param  socketpath  The pathname of the socket
 */
void close_socket(const char* socketpath)
{
  if (socketfd >= 0)
    {
      shutdown(socketfd, SHUT_RDWR);
      close(socketfd);
      unlink(socketpath);
    }
}

