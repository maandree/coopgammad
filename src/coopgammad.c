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
#include "arg.h"
#include "util.h"
#include "state.h"
#include "servers/master.h"
#include "servers/kernel.h"
#include "servers/crtc.h"
#include "servers/gamma.h"
#include "servers/coopgamma.h"

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>



/**
 * Number put in front of the marshalled data
 * so the program an detect incompatible updates
 */
#define MARSHAL_VERSION  0



#ifndef GCC_ONLY
# if defined(__GNUC__) && !defined(__clang__)
#  define GCC_ONLY(...)  __VA_ARGS__
# else
#  define GCC_ONLY(...)  /* nothing */
# endif
#endif



/**
 * Lists all function recognised adjustment methods,
 * will call macro X with the code for the each
 * adjustment method as the first argument and
 * corresponding name as the second argument
 */
#define LIST_ADJUSTMENT_METHODS				\
  X(LIBGAMMA_METHOD_DUMMY,                "dummy")	\
  X(LIBGAMMA_METHOD_X_RANDR,              "randr")	\
  X(LIBGAMMA_METHOD_X_VIDMODE,            "vidmode")	\
  X(LIBGAMMA_METHOD_LINUX_DRM,            "drm")	\
  X(LIBGAMMA_METHOD_W32_GDI,              "gdi")	\
  X(LIBGAMMA_METHOD_QUARTZ_CORE_GRAPHICS, "quartz")



/**
 * Used by initialisation functions as their return type. If a
 * value not listed here is returned by such function, it is the
 * exit value the process shall exit with as soon as possible.
 */
enum init_status
{
  /**
   * Initialisation was successful
   */
  INIT_SUCCESS = -1,
  
  /**
   * Initialisation failed
   */
  INIT_FAILURE = -2,
  
  /**
   * Server is already running
   */
  INIT_RUNNING = -3,
};



/**
 * The pathname of the PID file
 */
extern char* restrict pidpath;
char* restrict pidpath = NULL;

/**
 * The pathname of the socket
 */
extern char* restrict socketpath;
char* restrict socketpath = NULL;



/**
 * Called when the process receives
 * a signal telling it to re-execute
 * 
 * @param  signo  The received signal
 */
static void sig_reexec(int signo)
{
  reexec = 1;
  (void) signo;
}


/**
 * Called when the process receives
 * a signal telling it to terminate
 * 
 * @param  signo  The received signal
 */
static void sig_terminate(int signo)
{
  terminate = 1;
  (void) signo;
}


/**
 * Called when the process receives
 * a signal telling it to disconnect
 * from or reconnect to the site
 * 
 * @param  signo  The received signal
 */
static void sig_connection(int signo)
{
  connection = signo - SIGRTMIN + 1;
}



/**
 * Parse adjustment method name (or stringised number)
 * 
 * @param   arg  The adjustment method name (or stringised number)
 * @return       The adjustment method, -1 (negative) on error
 */
GCC_ONLY(__attribute__((nonnull)))
static int get_method(const char* restrict arg)
{
#if LIBGAMMA_METHOD_MAX > 5
# warning libgamma has added more adjustment methods
#endif
  
  const char* restrict p;
  
#define X(C, N)  if (!strcmp(arg, N))  return C;
  LIST_ADJUSTMENT_METHODS;
#undef X
  
  if (!*arg || (/* avoid overflow: */ strlen(arg) > 4))
    goto bad;
  for (p = arg; *p; p++)
    if (('0' > *p) || (*p > '9'))
      goto bad;
  
  return atoi(arg);
  
 bad:
  fprintf(stderr, "%s: unrecognised adjustment method name: %s\n", argv0, arg);
  errno = 0;
  return -1;
}


/**
 * Fork the process to the background
 * 
 * @param   keep_stderr  Keep stderr open?
 * @return               An `enum init_status` value or an exit value
 */
static enum init_status daemonise(int keep_stderr)
{
  pid_t pid;
  int fd = -1, saved_errno;
  int notify_rw[2] = { -1, -1 };
  char a_byte = 0;
  ssize_t got;
  
  if (pipe(notify_rw) < 0)
    goto fail;
  if (notify_rw[0] <= STDERR_FILENO)
    if ((notify_rw[0] = dup2atleast(notify_rw[0], STDERR_FILENO + 1)) < 0)
      goto fail;
  if (notify_rw[1] <= STDERR_FILENO)
    if ((notify_rw[1] = dup2atleast(notify_rw[1], STDERR_FILENO + 1)) < 0)
      goto fail;
  
  if ((pid = fork()) < 0)
    goto fail;
  if (pid > 0)
    {
      /* Original process (parent): */
      waitpid(pid, NULL, 0);
      close(notify_rw[1]), notify_rw[1] = -1;
      got = read(notify_rw[0], &a_byte, 1);
      if (got < 0)
	goto fail;
      close(notify_rw[0]);
      errno = 0;
      return got == 0 ? INIT_FAILURE : (enum init_status)0;
    }
  
  /* Intermediary process (child): */
  close(notify_rw[0]), notify_rw[0] = -1;
  if (setsid() < 0)
    goto fail;
  if ((pid = fork()) < 0)
    goto fail;
  if (pid > 0)
    {
      /* Intermediary process (parent): */
      return (enum init_status)0;
    }
  
  /* Daemon process (child): */
  
  /* Replace std* with /dev/null */
  fd = open("/dev/null", O_RDWR);
  if (fd < 0)
    goto fail;
#define xdup2(s, d)  do if (s != d) { close(d); if (dup2(s, d) < 0) goto fail; } while (0)
  xdup2(fd, STDIN_FILENO);
  xdup2(fd, STDOUT_FILENO);
  if (keep_stderr)
    xdup2(fd, STDERR_FILENO);
  if (fd > STDERR_FILENO)
    close(fd);
  fd = -1;
  
  /* Update PID file */
  fd = open(pidpath, O_WRONLY);
  if (fd < 0)
    goto fail;
  if (dprintf(fd, "%llu\n", (unsigned long long)getpid()) < 0)
    goto fail;
  close(fd), fd = -1;
  
  /* Notify */
  if (write(notify_rw[1], &a_byte, 1) <= 0)
    goto fail;
  close(notify_rw[1]);
  
  return INIT_SUCCESS;
 fail:
  saved_errno = errno;
  if (fd >= 0)            close(fd);
  if (notify_rw[0] >= 0)  close(notify_rw[0]);
  if (notify_rw[1] >= 0)  close(notify_rw[1]);
  errno = saved_errno;
  return INIT_FAILURE;
}


/**
 * Initialise the process
 * 
 * @param   full         Perform a full initialisation, shall be done
 *                       iff the state is not going to be unmarshalled
 * @param   preserve     Preserve current gamma ramps at priority 0
 * @param   foreground   Keep process in the foreground
 * @param   keep_stderr  Keep stderr open
 * @param   query        Was -q used, see `main` for description
 * @return               An `enum init_status` value or an exit value
 */
static enum init_status initialise(int full, int preserve, int foreground, int keep_stderr, int query)
{
  struct rlimit rlimit;
  size_t i, n;
  sigset_t mask;
  char* restrict sitename_dup = NULL;
  int s, gerror;
  enum init_status r;
  
  /* Zero out some memory so it can be destoried safely. */
  memset(&site, 0, sizeof(site));
  
  if (full && !query)
    {
      /* Close all file descriptors above stderr */
      if (getrlimit(RLIMIT_NOFILE, &rlimit) || (rlimit.rlim_cur == RLIM_INFINITY))
	n = 4 << 10;
      else
	n = (size_t)(rlimit.rlim_cur);
      for (i = STDERR_FILENO + 1; i < n; i++)
	close((int)i);
      
      /* Set umask, reset signal handlers, and reset signal mask */
      umask(0);
      for (s = 1; s < _NSIG; s++)
	signal(s, SIG_DFL);
      if (sigfillset(&mask))
	perror(argv0);
      else
	sigprocmask(SIG_UNBLOCK, &mask, NULL);
    }
  
  /* Get method */
  if ((method < 0) && (libgamma_list_methods(&method, 1, 0) < 1))
    return fprintf(stderr, "%s: no adjustment method available\n", argv0), -1;
  
  /* Go no further if we are just interested in the adjustment method and site */
  if (query)
    return INIT_SUCCESS;
  
  /* Get site */
  if ((sitename != NULL) && !(sitename_dup = memdup(sitename, strlen(sitename) + 1)))
    goto fail;
  if ((gerror = libgamma_site_initialise(&site, method, sitename_dup)))
    goto fail_libgamma;
  
  if (full)
    {
      /* Get PID file and socket pathname */
      if (!(pidpath   = get_pidfile_pathname()) ||
	  !(socketpath = get_socket_pathname()))
	goto fail;
      
      /* Create PID file */
      if ((r = create_pidfile(pidpath)) < 0)
	{
	  free(pidpath), pidpath = NULL;
	  if (r == -2)
	    return INIT_RUNNING;
	  goto fail;
	}
    }
  
  /* Get partitions and CRTC:s */
  if (initialise_crtcs() < 0)
    goto fail;
  
  /* Get CRTC information */
  if (outputs_n && !(outputs = calloc(outputs_n, sizeof(*outputs))))
    goto fail;
  if (initialise_gamma_info() < 0)
    goto fail;
  
  /* Sort outputs */
  qsort(outputs, outputs_n, sizeof(*outputs), output_cmp_by_name);
  
  /* Load current gamma ramps */
  store_gamma();
  
  /* Preserve current gamma ramps at priority=0 if -p */
  if (preserve && (preserve_gamma() < 0))
    goto fail;
  
  if (full)
    {
      /* Create socket and start listening */
      if (create_socket(socketpath) < 0)
	goto fail;
      
      /* Get the real pathname of the process's binary, in case
       * it is relative, so we can re-execute without problem. */
      if ((*argv0 != '/') && strchr(argv0, '/') && !(argv0_real = realpath(argv0, NULL)))
	goto fail;
      
      /* Change directory to / to avoid blocking umounting */
      if (chdir("/") < 0)
	perror(argv0);
    }
  
  /* Set up signal handlers */
  if ((signal(SIGUSR1,      sig_reexec)     == SIG_ERR) ||
      (signal(SIGTERM,      sig_terminate)  == SIG_ERR) ||
      (signal(SIGRTMIN + 0, sig_connection) == SIG_ERR) ||
      (signal(SIGRTMIN + 1, sig_connection) == SIG_ERR))
    goto fail;
  
  /* Place in the background unless -f */
  if (full && (foreground == 0))
    return daemonise(keep_stderr);
  else if (full)
    {
      /* Signal the spawner that the service is ready */
      close(STDOUT_FILENO);
      /* Avoid potential catastrophes that would occur if a library
       * that is being used was so mindless as to write to stdout. */
      if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
	perror(argv0);
    }
  
  return INIT_SUCCESS;
 fail_libgamma:
  libgamma_perror(argv0, gerror);
  errno = 0;
 fail:
  return INIT_FAILURE;
}


/**
 * Deinitialise the process
 * 
 * @param  full  Perform a full deinitialisation, shall be
 *               done iff the process is going to re-execute
 */
static void destroy(int full)
{
  if (full)
    disconnect_all();
  
  if (full)
    close_socket(socketpath);
  
  if (full && (outputs != NULL))
    restore_gamma();
  
  state_destroy();
  free(socketpath);
  if (full && (pidpath != NULL))
    unlink(pidpath);
  free(pidpath);
}



#if defined(__clang__)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wcast-align"
#endif


/**
 * Marshal the state of the process
 * 
 * @param   buf  Output buffer for the marshalled data,
 *               `NULL` to only measure how large the
 *               buffer needs to be
 * @return       The number of marshalled bytes
 */
static size_t marshal(void* restrict buf)
{
  size_t off = 0, n;
  char* restrict bs = buf;
  
  if (bs != NULL)
    *(int*)(bs + off) = MARSHAL_VERSION;
  off += sizeof(int);
  
  n = strlen(pidpath) + 1;
  if (bs != NULL)
    memcpy(bs + off, pidpath, n);
  off += n;
  
  n = strlen(socketpath) + 1;
  if (bs != NULL)
    memcpy(bs + off, socketpath, n);
  off += n;
  
  off += state_marshal(bs ? bs + off : NULL);
  
  return off;
}


/**
 * Unmarshal the state of the process
 * 
 * @param   buf  Buffer with the marshalled data
 * @return       The number of marshalled bytes, 0 on error
 */
GCC_ONLY(__attribute__((nonnull)))
static size_t unmarshal(const void* restrict buf)
{
  size_t off = 0, n;
  const char* restrict bs = buf;
  
  if (*(const int*)(bs + off) != MARSHAL_VERSION)
    {
      fprintf(stderr, "%s: re-executing to incompatible version, sorry about that\n", argv0);
      errno = 0;
      return 0;
    }
  off += sizeof(int);
  
  n = strlen(bs + off) + 1;
  if (!(pidpath = memdup(bs + off, n)))
    return 0;
  off += n;
  
  n = strlen(bs + off) + 1;
  if (!(socketpath = memdup(bs + off, n)))
    return 0;
  off += n;
  
  off += n = state_unmarshal(bs + off);
  if (n == 0)
    return 0;
  
  return off;
}


#if defined(__clang__)
# pragma GCC diagnostic pop
#endif



/**
 * Unmarshal the state of the process and merge with new state
 * 
 * @param   statefile  The state file
 * @return             Zero on success, -1 on error
 */
GCC_ONLY(__attribute__((nonnull)))
static int unmarshal_and_merge_state(const char* restrict statefile)
{
  struct output* restrict old_outputs = NULL;
  size_t i, n, r, old_outputs_n = 0;
  void* marshalled = NULL;
  int fd = -1, saved_errno;
  
  fd = open(statefile, O_RDONLY);
  if (fd < 0)
    goto fail;
  if (!(marshalled = nread(fd, &n)))
    goto fail;
  close(fd), fd = -1;
  unlink(statefile), statefile = NULL;
  
  old_outputs   = outputs;
  old_outputs_n = outputs_n;
  outputs   = NULL;
  outputs_n = 0;
  
  r = unmarshal(marshalled);
  if (r == 0)
    goto fail;
  if (r != n)
    {
      fprintf(stderr, "%s: unmarshalled state file was %s than the unmarshalled state\n",
	      argv0, n > r ? "larger" : "smaller");
      errno = 0;
      goto fail;
    }
  free(marshalled), marshalled = NULL;
  
  if (merge_state(old_outputs, old_outputs_n) < 0)
    goto fail;
  
  for (i = 0; i < old_outputs_n; i++)
    output_destroy(old_outputs + i);
  free(old_outputs);
  
  return 0;
 fail:
  saved_errno = errno;
  if (fd >= 0)
    close(fd);
  free(marshalled);
  for (i = 0; i < old_outputs_n; i++)
    output_destroy(old_outputs + i);
  free(old_outputs);
  errno = saved_errno;
  return -1;
}


/**
 * Print the response for the -q option
 * 
 * @param   query  See -q for `main`, must be atleast 1
 * @return         Zero on success, -1 on error
 */
static int print_method_and_site(int query)
{
  const char* restrict methodname = NULL;
  char* p;
  
  if (query == 1)
    {
      switch (method)
	{
#define X(C, N)  case C: methodname = N; break;
	LIST_ADJUSTMENT_METHODS;
#undef X
	default:
	  if (printf("%i\n", method) < 0)
	    return -1;
	  break;
	}
      if (!methodname)
	if (printf("%s\n", methodname) < 0)
	  return -1;
    }
  
  if (sitename == NULL)
    if ((sitename = libgamma_method_default_site(method)))
      if (!(sitename = memdup(sitename, strlen(sitename) + 1)))
	return -1;
  
  if (sitename != NULL)
    switch (method)
      {
      case LIBGAMMA_METHOD_X_RANDR:
      case LIBGAMMA_METHOD_X_VIDMODE:
	if ((p = strrchr(sitename, ':')))
	  if ((p = strchr(p, '.')))
	    *p = '\0';
	break;
      default:
	break;
      }
  
  if ((sitename != NULL) && (query == 1))
    if (printf("%s\n", sitename) < 0)
      return -1;
  
  if (query == 2)
    {
      site.method = method;
      site.site = sitename, sitename = NULL;
      socketpath = get_socket_pathname();
      if (socketpath == NULL)
	return -1;
      if (printf("%s\n", socketpath) < 0)
	return -1;
    }
  
  if (close(STDOUT_FILENO) < 0)
    if (errno != EINTR)
      return -1;
  
  return 0;    
}


/**
 * Reexecute the server
 * 
 * Returns only on failure
 * 
 * @param   preserve  Did -p appear on the comment line?
 * @return            Pathname of file where the state is stored,
 *                    `NULL` if the state is in tact
 */
static char* reexecute(int preserve)
{
  char* statefile = NULL;
  char* statebuffer = NULL;
  size_t buffer_size;
  int fd = -1, saved_errno;
  
  statefile = get_state_pathname();
  if (statefile == NULL)
    goto fail;
  
  buffer_size = marshal(NULL);
  statebuffer = malloc(buffer_size);
  if (statebuffer == NULL)
    goto fail;
  if (marshal(statebuffer) != buffer_size)
    {
      fprintf(stderr, "%s: internal error", argv0);
      errno = 0;
      goto fail;
    }
  
  fd = open(statefile, O_CREAT, S_IRUSR | S_IWUSR);
  if (fd < 0)
    goto fail;
  
  if (nwrite(fd, statebuffer, buffer_size) != buffer_size)
    goto fail;
  free(statebuffer), statebuffer = NULL;
  
  if ((close(fd) < 0) && (fd = -1, errno != EINTR))
    goto fail;
  fd = -1;
  
  destroy(0);
  
  execlp(argv0_real ? argv0_real : argv0, argv0, "- ", statefile, preserve ? "-p" : NULL, NULL);
  saved_errno = errno;
  free(argv0_real), argv0_real = NULL;
  errno = saved_errno;
  return statefile;
  
 fail:
  saved_errno = errno;
  free(statebuffer);
  if (fd >= 0)
    close(fd);
  if (statefile != NULL)
    unlink(statefile), free(statefile);
  errno = saved_errno;
  return NULL;
}


/**
 * Print usage information and exit
 */
#if defined(__GNU__) || defined(__clang__)
__attribute__((noreturn))
#endif
static void usage(void)
{
  printf("Usage: %s [-m method] [-s site] [-fkpq]\n", argv0);
  exit(1);
}


#if defined(__clang__)
# pragma GCC diagnostic ignored "-Wdocumentation-unknown-command"
#endif


/**
 * Must not be started without stdin, stdout, or stderr (may be /dev/null)
 * 
 * argv[0] must refer to the real command name or pathname,
 * otherwise, re-execute will not work
 * 
 * The process closes stdout when the socket has been created
 * 
 * @signal  SIGUSR1     Re-execute to updated process image
 * @signal  SIGTERM     Terminate the process gracefully
 * @signal  SIGRTMIN+0  Disconnect from the site
 * @signal  SiGRTMIN+1  Reconnect to the site
 * 
 * @param   argc  The number of elements in `argv`
 * @param   argv  Command line arguments. Recognised options:
 *                  -s SITENAME
 *                    The site to which to connect
 *                  -m METHOD
 *                    Adjustment method name or adjustment method number
 *                  -p
 *                    Preserve current gamma ramps at priority 0
 *                  -f
 *                    Do not fork the process into the background
 *                  -k
 *                    Keep stderr open
 *                  -q
 *                    Print the select (possiblity default) adjustment
 *                    method on the first line in stdout, and the
 *                    selected (possibility defasult) site on the second
 *                    line in stdout, and exit. If the site name is `NULL`,
 *                    the second line is omitted. This is indented to
 *                    be used by clients to figure out to which instance
 *                    of the service it should connect. Use twice to
 *                    simply ge the socket pathname, an a terminating LF.
 *                    By combining -q and -m you can enumerate the name
 *                    of all recognised adjustment method, start from 0
 *                    and work up until a numerical adjustment method is
 *                    returned.
 * @return        0: Successful
 *                1: An error occurred
 *                2: Already running
 */
int main(int argc, char** argv)
{
  int rc = 1, preserve = 0, foreground = 0, keep_stderr = 0, query = 0, r;
  char* statefile = NULL;
  
  ARGBEGIN
    {
    case 's':
      sitename = EARGF(usage());
      /* To simplify re-exec: */
      sitename = memdup(sitename, strlen(sitename) + 1);
      if (sitename == NULL)
	goto fail;
      break;
    case 'm':
      method = get_method(EARGF(usage()));
      if (method < 0)
	goto fail;
      break;
    case 'p':  preserve    = 1;      break;
    case 'f':  foreground  = 1;      break;
    case 'k':  keep_stderr = 1;      break;
    case 'q':  query = 1 + !!query;  break;
    case ' ': /* Internal, do not document */
      statefile = EARGF(usage());
      break;
    default:
      usage();
    }
  ARGEND;
  if (argc > 0)
    usage();
  
 restart:
  
  switch ((r = initialise(statefile == NULL, preserve, foreground, keep_stderr, query)))
    {
    case INIT_SUCCESS:  break;
    case INIT_RUNNING:  rc = 2;  /* fall through */
    case INIT_FAILURE:  goto fail;
    default:            return r;
    }
  
  if (query)
    {
      if (print_method_and_site(query) < 0)
	goto fail;
      goto done;
    }
  
  if (statefile != NULL)
    {
      if (unmarshal_and_merge_state(statefile) < 0)
	goto fail;
      if (reexec)
	free(statefile);
      unlink(statefile), statefile = NULL;
      reexec = 0; /* See `if (reexec && !terminate)` below */
    }
  
 reenter_loop:
  if (main_loop() < 0)
    goto fail;
  
  if (reexec && !terminate)
    {
      if ((statefile = reexecute(preserve)))
	{
	  perror(argv0);
	  fprintf(stderr, "%s: restoring state without re-executing\n", argv0);
	  /* `reexec = 0;` is done later in case of re-execute failure,
	   * since it determines whether `statefile` shall be freed. */
	  goto restart;
	}
      perror(argv0);
      fprintf(stderr, "%s: continuing without re-executing\n", argv0);
      reexec = 0;
      goto reenter_loop;
    }
  
 done:
  rc = 0;
 deinit:
  if (statefile)
    unlink(statefile);
  if (reexec)
    free(statefile);
  destroy(1);
  return rc;
 fail:
  if (errno != 0)
    perror(argv0);
  goto deinit;
}

