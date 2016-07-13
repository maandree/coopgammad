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
#include <libgamma.h>

#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "arg.h"
#include "output.h"
#include "util.h"
#include "server.h"



/**
 * Number put in front of the marshalled data
 * so the program an detect incompatible updates
 */
#define MARSHAL_VERSION  0



/**
 * The name of the process
 */
char* argv0;

/**
 * The real pathname of the process's binary,
 * `NULL` if `argv0` is satisfactory
 */
char* argv0_real = NULL;

/**
 * Array of all outputs
 */
struct output* outputs = NULL;

/**
 * The nubmer of elements in `outputs`
 */
size_t outputs_n = 0;

/**
 * The server socket's file descriptor
 */
int socketfd = -1;

/**
 * The pathname of the PID file
 */
char* pidpath = NULL;

/**
 * The pathname of the socket
 */
char* socketpath = NULL;

/**
 * Error code returned by libgamma
 */
int gerror;

/**
 * The adjustment method, -1 for automatic
 */
int method = -1;

/**
 * The site's name, may be `NULL`
 */
char* sitename = NULL;

/**
 * The libgamma site state
 */
libgamma_site_state_t site;

/**
 * The libgamma partition states
 */
libgamma_partition_state_t* partitions = NULL;

/**
 * The libgamma CRTC states
 */
libgamma_crtc_state_t* crtcs = NULL;

/**
 * Has the process receive a signal
 * telling it to re-execute?
 */
volatile sig_atomic_t reexec = 0;

/**
 * Has the process receive a signal
 * telling it to terminate?
 */
volatile sig_atomic_t terminate = 0;



/**
 * Called when the process receives
 * a signal telling it to re-execute
 * 
 * @param  signo The received signal
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
 * @param  signo The received signal
 */
static void sig_terminate(int signo)
{
  terminate = 1;
  (void) signo;
}


/**
 * Get the pathname of the runtime file
 * 
 * @param   suffix  The suffix for the file
 * @return          The pathname of the file, `NULL` on error
 */
static char* get_pathname(const char* suffix)
{
  const char* rundir = getenv("XDG_RUNTIME_DIR");
  const char* username = "";
  char* name = NULL;
  char* p;
  char* rc;
  struct passwd* pw;
  size_t n;
  int saved_errno;
  
  if (site.site)
    {
      name = memdup(site.site, strlen(site.site) + 1);
      if (name == NULL)
	goto fail;
    }
  else if ((name = libgamma_method_default_site(site.method)))
    {
      name = memdup(name, strlen(name) + 1);
      if (name == NULL)
	goto fail;
    }
  
  if (name != NULL)
    switch (site.method)
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
  n += strlen(rundir) + strlen(username) + strlen(name) + strlen(suffix);
  if (!(rc = malloc(n)))
    goto fail;
  sprintf(rc, "%s/.coopgammad/~%s/%i%s%s%s",
	  rundir, username, site.method, name ? "." : "", name ? name : "", suffix);
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
static inline char* get_socket_pathname(void)
{
  return get_pathname(".socket");
}


/**
 * Get the pathname of the PID file
 * 
 * @return  The pathname of the PID file, `NULL` on error
 */
static inline char* get_pidfile_pathname(void)
{
  return get_pathname(".pid");
}


/**
 * Get the pathname of the state file
 * 
 * @return  The pathname of the state file, `NULL` on error
 */
static inline char* get_state_pathname(void)
{
  return get_pathname(".state");
}


/**
 * Parse adjustment method name (or stringised number)
 * 
 * @param   arg  The adjustment method name (or stringised number)
 * @return       The adjustment method, -1 (negative) on error
 */
static int get_method(char* arg)
{
#if LIBGAMMA_METHOD_MAX > 5
# warning libgamma has added more adjustment methods
#endif
  
  char* p;
  
  if (!strcmp(arg, "dummy"))    return LIBGAMMA_METHOD_DUMMY;
  if (!strcmp(arg, "randr"))    return LIBGAMMA_METHOD_X_RANDR;
  if (!strcmp(arg, "vidmode"))  return LIBGAMMA_METHOD_X_VIDMODE;
  if (!strcmp(arg, "drm"))      return LIBGAMMA_METHOD_LINUX_DRM;
  if (!strcmp(arg, "gdi"))      return LIBGAMMA_METHOD_W32_GDI;
  if (!strcmp(arg, "quartz"))   return LIBGAMMA_METHOD_QUARTZ_CORE_GRAPHICS;
  
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
 * Get the name of a CRTC
 * 
 * @param   info  Information about the CRTC
 * @param   crtc  libgamma's state for the CRTC
 * @return        The name of the CRTC, `NULL` on error
 */
static char* get_crtc_name(libgamma_crtc_information_t* info, libgamma_crtc_state_t* crtc)
{
  if ((info->edid_error == 0) && (info->edid != NULL))
    return libgamma_behex_edid(info->edid, info->edid_length);
  else if ((info->connector_name_error == 0) && (info->connector_name != NULL))
    {
      char* name = malloc(3 * sizeof(size_t) + strlen(info->connector_name) + 2);
      if (name != NULL)
	sprintf(name, "%zu.%s", crtc->partition->partition, info->connector_name);
      return name;
    }
  else
    {
      char* name = malloc(2 * 3 * sizeof(size_t) + 2);
      if (name != NULL)
	sprintf(name, "%zu.%zu", crtc->partition->partition, crtc->crtc);
      return name;
    }
}


/**
 * Check whether a PID file is outdated
 * 
 * @param   pidfile  The PID file
 * @param   token    An environment variable (including both key and value)
 *                   that must exist in the process if it is a coopgammad process
 * @return           -1: An error occurred
 *                    0: The service is already running
 *                    1: The PID file is outdated
 */
static int is_pidfile_reusable(const char* pidfile, const char* token)
{
  /* PORTERS: /proc/$PID/environ is Linux specific */
  
  char temp[sizeof("/proc//environ") + 3 * sizeof(pid_t)];
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
  fd = open(pidfile, O_RDONLY);
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
  if ((size_t)(content - p) != n)
    goto bad;
  sprintf(temp, "%llu", (unsigned long long)pid);
  if (strcmp(content, temp))
    goto bad;
  
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
      return 0;
#else
  if ((kill(pid, 0) == 0) || (errno == EINVAL))
    return 0;
#endif
  
  return 1;
 bad:
  fprintf(stderr, "%s: pid file contain invalid content: %s\n", argv0, pidfile);
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
 * @param   pidfile  The pathname of the PID file
 * @return           Zero on success, -1 on error,
 *                   -2 if the service is already running
 */
static int create_pidfile(char* pidfile)
{
  int fd, r, saved_errno;
  char* p;
  char* token;
  
  /* Create token used to validate the service. */
  token = malloc(sizeof("COOPGAMMAD_PIDFILE_TOKEN=") + strlen(pidfile));
  if (token == NULL)
    return -1;
  sprintf(token, "COOPGAMMAD_PIDFILE_TOKEN=%s", pidfile);
  if (putenv(token))
    return -1;
  
  /* Create PID file's directory. */
  for (p = pidfile; *p == '/'; p++);
  while ((p = strchr(p, '/')))
    {
      *p = '\0';
      if (mkdir(pidfile, 0644) < 0)
	if (errno != EEXIST)
	  return -1;
      *p++ = '/';
    }
  
  /* Create PID file. */
 retry:
  fd = open(pidfile, O_CREAT | O_EXCL, 0644);
  if (fd < 0)
    {
      if (errno == EINTR)
	goto retry;
      if (errno != EEXIST)
	return -1;
      r = is_pidfile_reusable(pidfile, token);
      if (r > 0)
	{
	  unlink(pidfile);
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
  if (close(fd) < 0)
    if (errno != EINTR)
      return -1;
  return 0;
 fail:
  saved_errno = errno;
  close(fd);
  unlink(pidfile);
  errno = saved_errno;
  return -1;
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
 * @return               1: Success
 *                       2: Normal failure
 *                       3: Libgamma failure
 *                       4: The service is already running
 *                       Otherwise: The negative of the exit value the
 *                       process should have and shall exit immediately
 */
static int initialise(int full, int preserve, int foreground, int keep_stderr, int query)
{
  struct sockaddr_un address;
  struct rlimit rlimit;
  size_t i, j, n, n0;
  sigset_t mask;
  char* sitename_dup = NULL;
  int r;
  
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
      for (r = 1; r < _NSIG; r++)
	signal(r, SIG_DFL);
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
    return 1;
  
  /* Get site */
  if (sitename != NULL)
    if (!(sitename_dup = memdup(sitename, strlen(sitename) + 1)))
      goto fail;
  if ((gerror = libgamma_site_initialise(&site, method, sitename_dup)))
    goto fail_libgamma;
  
  if (full)
    {
      /* Get PID file and socket pathname */
      if (!(pidpath = get_pidfile_pathname()))
	goto fail;
      if (!(socketpath = get_socket_pathname()))
	goto fail;
      
      /* Create PID file */
      if ((r = create_pidfile(pidpath)) < 0)
	{
	  free(pidpath), pidpath = NULL;
	  if (r == -2)
	    goto already_running;
	  goto fail;
	}
    }
  
  /* Get partitions */
  if (site.partitions_available)
    if (!(partitions = calloc(site.partitions_available, sizeof(*partitions))))
      goto fail;
  for (i = 0; i < site.partitions_available; i++)
    {
      if ((gerror = libgamma_partition_initialise(partitions + i, &site, i)))
	goto fail_libgamma;
      outputs_n += partitions[i].crtcs_available;
    }
  
  /* Get CRTC:s */
  if (outputs_n)
    if (!(crtcs = calloc(outputs_n, sizeof(*crtcs))))
      goto fail;
  for (i = 0, j = n = 0; i < site.partitions_available; i++)
    for (n0 = n, n += partitions[i].crtcs_available; j < n; j++)
      if ((gerror = libgamma_crtc_initialise(crtcs + j, partitions + i, j - n0)))
	goto fail_libgamma;
  
  /* Get CRTC information */
  if (outputs_n)
    if (!(outputs = calloc(outputs_n, sizeof(*outputs))))
      goto fail;
  for (i = 0; i < outputs_n; i++)
    {
      libgamma_crtc_information_t info;
      int saved_errno;
      libgamma_get_crtc_information(&info, crtcs + i,
				    LIBGAMMA_CRTC_INFO_EDID |
				    LIBGAMMA_CRTC_INFO_MACRO_RAMP |
				    LIBGAMMA_CRTC_INFO_GAMMA_SUPPORT |
				    LIBGAMMA_CRTC_INFO_CONNECTOR_NAME);
      outputs[i].depth       = info.gamma_depth_error   ? 0 : info.gamma_depth;
      outputs[i].red_size    = info.gamma_size_error    ? 0 : info.red_gamma_size;
      outputs[i].green_size  = info.gamma_size_error    ? 0 : info.green_gamma_size;
      outputs[i].blue_size   = info.gamma_size_error    ? 0 : info.blue_gamma_size;
      outputs[i].supported   = info.gamma_support_error ? 0 : info.gamma_support;
      if (outputs[i].depth      == 0 || outputs[i].red_size  == 0 ||
	  outputs[i].green_size == 0 || outputs[i].blue_size == 0)
	outputs[i].supported = 0;
      outputs[i].name        = get_crtc_name(&info, crtcs + i);
      saved_errno = errno;
      outputs[i].crtc        = crtcs + i;
      libgamma_crtc_information_destroy(&info);
      outputs[i].ramps_size = outputs[i].red_size + outputs[i].green_size + outputs[i].blue_size;
      /* outputs[i].ramps_size will be multipled by the stop-size later */
      errno = saved_errno;
      if (outputs[i].name == NULL)
	goto fail;
    }
  free(crtcs), crtcs = NULL;
  
  /* Sort outputs */
  qsort(outputs, outputs_n, sizeof(*outputs), output_cmp_by_name);
  
  /* Load current gamma ramps */
#define LOAD_RAMPS(SUFFIX, MEMBER) \
  do \
    { \
      libgamma_gamma_ramps##SUFFIX##_initialise(&(outputs[i].saved_ramps.MEMBER)); \
      gerror = libgamma_crtc_get_gamma_ramps##SUFFIX(outputs[i].crtc, &(outputs[i].saved_ramps.MEMBER)); \
      if (gerror) \
	{ \
	  libgamma_perror(argv0, gerror); \
	  outputs[i].supported = LIBGAMMA_NO; \
	  libgamma_gamma_ramps##SUFFIX##_destroy(&(outputs[i].saved_ramps.MEMBER)); \
	  memset(&(outputs[i].saved_ramps.MEMBER), 0, sizeof(outputs[i].saved_ramps.MEMBER)); \
	} \
    } \
  while (0)
  for (i = 0; i < outputs_n; i++)
    if (outputs[i].supported != LIBGAMMA_NO)
      switch (outputs[i].depth)
	{
	case 8:
	  outputs[i].ramps_size *= sizeof(uint8_t);
	  LOAD_RAMPS(8, u8);
	  break;
	case 16:
	  outputs[i].ramps_size *= sizeof(uint16_t);
	  LOAD_RAMPS(16, u16);
	  break;
	case 32:
	  outputs[i].ramps_size *= sizeof(uint32_t);
	  LOAD_RAMPS(32, u32);
	  break;
	default:
	  outputs[i].depth = 64;
	  /* fall through */
	case 64:
	  outputs[i].ramps_size *= sizeof(uint64_t);
	  LOAD_RAMPS(64, u64);
	  break;
	case -1:
	  outputs[i].ramps_size *= sizeof(float);
	  LOAD_RAMPS(f, f);
	  break;
	case -2:
	  outputs[i].ramps_size *= sizeof(double);
	  LOAD_RAMPS(d, d);
	  break;
	}
  
  /* Preserve current gamma ramps at priority=0 if -p */
  if (preserve)
    for (i = 0; i < outputs_n; i++)
      {
	struct filter filter = {
	  .client   = -1,
	  .priority = 0,
	  .class    = NULL,
	  .lifespan = LIFESPAN_UNTIL_REMOVAL,
	  .ramps    = NULL
	};
	outputs[i].table_filters = calloc(4, sizeof(*(outputs[i].table_filters)));
	outputs[i].table_sums = calloc(4, sizeof(*(outputs[i].table_sums)));
	outputs[i].table_alloc = 4;
	outputs[i].table_size = 1;
	filter.class = memdup(PKGNAME "::" COMMAND "::preserved", sizeof(PKGNAME "::" COMMAND "::preserved"));
	if (filter.class == NULL)
	  goto fail;
	filter.ramps = memdup(outputs[i].saved_ramps.u8.red, outputs[i].ramps_size);
	if (filter.ramps == NULL)
	  goto fail;
	outputs[i].table_filters[0] = filter;
	COPY_RAMP_SIZES(&(outputs[i].table_sums[0].u8), outputs + i);
	if (!gamma_ramps_unmarshal(outputs[i].table_sums, outputs[i].saved_ramps.u8.red, outputs[i].ramps_size))
	  goto fail;
      }
  
  if (full)
    {
      /* Create socket and start listening */
      address.sun_family = AF_UNIX;
      if (strlen(socketpath) >= sizeof(address.sun_path))
	{
	  errno = ENAMETOOLONG;
	  goto fail;
	}
      strcpy(address.sun_path, socketpath);
      unlink(socketpath);
      if ((socketfd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
	goto fail;
      if (fchmod(socketfd, S_IRWXU) < 0)
	goto fail;
      if (bind(socketfd, (struct sockaddr*)(&address), sizeof(address)) < 0)
	goto fail;
      if (listen(socketfd, SOMAXCONN) < 0)
	goto fail;
      
      /* Get the real pathname of the process's binary, in case
       * it is relative, so we can re-execute without problem. */
      if ((*argv0 != '/') && strchr(argv0, '/'))
	if (!(argv0_real = realpath(argv0, NULL)))
	  goto fail;
      
      /* Change directory to / to avoid blocking umounting */
      if (chdir("/") < 0)
	perror(argv0);
    }
  
  /* Set up signal handlers */
  if (signal(SIGUSR1, sig_reexec) == SIG_ERR)
    goto fail;
  if (signal(SIGTERM, sig_terminate) == SIG_ERR)
    goto fail;
  
  /* Place in the background unless -f */
  if (full && (foreground == 0))
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
	  goto fail_background;
      if (notify_rw[1] <= STDERR_FILENO)
	if ((notify_rw[1] = dup2atleast(notify_rw[1], STDERR_FILENO + 1)) < 0)
	  goto fail_background;
      
      switch ((pid = fork()))
	{
	case -1:
	  goto fail_background;
	case 0:
	  /* Child */
	  close(notify_rw[0]), notify_rw[0] = -1;
	  if (setsid() < 0)
	    goto fail_background;
	  switch ((pid = fork()))
	    {
	    case -1:
	      goto fail_background;
	    case 0:
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
		goto fail_background;
	      if (dprintf(fd, "%llu\n", (unsigned long long)getpid()) < 0)
		goto fail_background;
	      close(fd), fd = -1;
	      
	      /* Notify */
	      if (write(notify_rw[1], &a_byte, 1) <= 0)
		goto fail_background;
	      close(notify_rw[1]);
	      break;
	    default:
	      /* Parent */
	      return 0;
	    }
	  break;
	default:
	  /* Parent */
	  close(notify_rw[1]), notify_rw[1] = -1;
	  got = read(notify_rw[0], &a_byte, 1);
	  if (got < 0)
	    goto fail_background;
	  close(notify_rw[0]);
	  return -(got == 0);
	}
      
      goto done_background;
    fail_background:
      saved_errno = errno;
      if (fd >= 0)            close(fd);
      if (notify_rw[0] >= 0)  close(notify_rw[0]);
      if (notify_rw[1] >= 0)  close(notify_rw[1]);
      errno = saved_errno;
    done_background:;
    }
  else if (full)
    {
      /* Signal the spawner that the service is ready */
      close(STDOUT_FILENO);
      /* Avoid potential catastrophes that would occur if a library that is being
       * used was so mindless as to write to stdout. */
      if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
	perror(argv0);
    }
  
  return 1;
 fail:
  return 2;
 fail_libgamma:
  return 3;
 already_running:
  return 4;
}


/**
 * Deinitialise the process
 * 
 * @param  full  Perform a full deinitialisation, shall be
 *               done iff the process is going to re-execute
 */
static void destroy(int full)
{
  size_t i;
  
  if (full && (socketfd >= 0))
    {
      shutdown(socketfd, SHUT_RDWR);
      close(socketfd);
      unlink(socketpath);
    }
#define RESTORE_RAMPS(SUFFIX, MEMBER) \
  do \
    if (outputs[i].saved_ramps.MEMBER.red != NULL) \
      { \
	gerror = libgamma_crtc_set_gamma_ramps##SUFFIX(outputs[i].crtc, outputs[i].saved_ramps.MEMBER); \
	if (gerror) \
	    libgamma_perror(argv0, gerror); \
      } \
  while (0)
  if (outputs != NULL)
    for (i = 0; i < outputs_n; i++)
      {
	if (full && (outputs[i].supported != LIBGAMMA_NO))
	  switch (outputs[i].depth)
	    {
	    case 8:
	      RESTORE_RAMPS(8, u8);
	      break;
	    case 16:
	      RESTORE_RAMPS(16, u16);
	      break;
	    case 32:
	      RESTORE_RAMPS(32, u32);
	      break;
	    case 64:
	      RESTORE_RAMPS(64, u64);
		break;
	    case -1:
	      RESTORE_RAMPS(f, f);
	      break;
	    case -2:
	      RESTORE_RAMPS(d, d);
	      break;
	    default:
	      break; /* impossible */
	    }
	if (crtcs == NULL)
	  libgamma_crtc_destroy(outputs[i].crtc + i);
	output_destroy(outputs + i);
      }
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
  free(socketpath);
  if (full && (pidpath != NULL))
    unlink(pidpath);
  free(pidpath);
  free(argv0_real);
  free(sitename);
}


/**
 * Marshal the state of the process
 * 
 * @param   buf  Output buffer for the marshalled data,
 *               `NULL` to only measure how large the
 *               buffer needs to be
 * @return       The number of marshalled bytes
 */
static size_t marshal(void* buf)
{
  size_t off = 0, i, n;
  char* bs = buf;
  
  if (bs != NULL)
    *(int*)(bs + off) = MARSHAL_VERSION;
  off += sizeof(int);
  
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
  
  n = strlen(pidpath) + 1;
  if (bs != NULL)
    memcpy(bs + off, pidpath, n);
  off += n;
  
  n = strlen(socketpath) + 1;
  if (bs != NULL)
    memcpy(bs + off, socketpath, n);
  off += n;
  
  off += server_marshal(bs ? bs + off : NULL);
  
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
  
  return off;
}


/**
 * Unmarshal the state of the process
 * 
 * @param   buf  Buffer with the marshalled data
 * @return       The number of marshalled bytes, 0 on error
 */
static size_t unmarshal(void* buf)
{
  size_t off = 0, i, n;
  char* bs = buf;
  
  if (*(int*)(bs + off) != MARSHAL_VERSION)
    {
      fprintf(stderr, "%s: re-executing to incompatible version, sorry about that\n", argv0);
      errno = 0;
      return 0;
    }
  off += sizeof(int);
  
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
  
  outputs_n = *(size_t*)(bs + off);
  off += sizeof(size_t);
  
  for (i = 0; i < outputs_n; i++)
    {
      off += n = output_unmarshal(outputs + i, bs + off);
      if (n == 0)
	return 0;
    }
  
  socketfd = *(int*)(bs + off);
  off += sizeof(int);
  
  n = strlen(bs + off) + 1;
  if (!(pidpath = memdup(bs + off, n)))
    return 0;
  off += n;
  
  n = strlen(bs + off) + 1;
  if (!(socketpath = memdup(bs + off, n)))
    return 0;
  off += n;
  
  off += n = server_unmarshal(bs + off);
  if (n == 0)
    return 0;
  
  method = *(int*)(bs + off);
  off += sizeof(int);
  
  if (*(int*)(bs + off))
    {
      off += sizeof(int);
      n = strlen(bs + off) + 1;
      if (!(sitename = memdup(bs + off, n)))
	return 0;
      off += n;
    }
  else
    off += sizeof(int);
  
  return off;
}


/**
 * Unmarshal the state of the process and merge with new state
 * 
 * @param   statefile  The state file
 * @return             Zero on success, -1 on error
 */
static int unmarshal_and_merge_state(const char* statefile)
{
  struct output* new_outputs = outputs;
  struct output* old_outputs = NULL;
  size_t new_outputs_n = outputs_n;
  size_t old_outputs_n = 0;
  size_t i, j, k;
  void* marshalled = NULL;
  int fd = -1, saved_errno;
  
  outputs = NULL;
  outputs_n = 0;
  
  fd = open(statefile, O_RDONLY);
  if (fd < 0)
    goto fail;
  if (!(marshalled = nread(fd, &k)))
    goto fail;
  close(fd), fd = -1;
  unlink(statefile), statefile = NULL;
  
  if (unmarshal(marshalled) == 0)
    goto fail;
  free(marshalled), marshalled = NULL;
  
  old_outputs   = outputs,   outputs   = new_outputs,   new_outputs   = NULL;
  old_outputs_n = outputs_n, outputs_n = new_outputs_n, new_outputs_n = 0;
  
  i = j = new_outputs_n = 0;
  while ((i < old_outputs_n) && (j < outputs_n))
    {
      int cmp = strcmp(old_outputs[i].name, outputs[j].name);
      if (cmp <= 0)
	new_outputs_n++;
      i += cmp >= 0;
      j += cmp <= 0;
    }
  new_outputs_n += outputs_n - j;
  
  new_outputs = calloc(new_outputs_n, sizeof(*new_outputs));
  if (new_outputs == NULL)
    goto fail;
  
  i = j = k = new_outputs_n = 0;
  while ((i < old_outputs_n) && (j < outputs_n))
    {
      int cmp = strcmp(old_outputs[i].name, outputs[j].name);
      int is_same = 0;
      if (cmp == 0)
	is_same = (old_outputs[i].depth      == outputs[j].depth      &&
		   old_outputs[i].red_size   == outputs[j].red_size   &&
		   old_outputs[i].green_size == outputs[j].green_size &&
		   old_outputs[i].blue_size  == outputs[j].blue_size);
      if (is_same)
	{
	  new_outputs[k] = old_outputs[i];
	  new_outputs[k].crtc = outputs[j].crtc;
	  output_destroy(outputs + j);
	  k++;
	}
      else if (cmp <= 0)
	new_outputs[k++] = outputs[j];
      i += cmp >= 0;
      j += cmp <= 0;
    }
  while (j < outputs_n)
    new_outputs[k++] = outputs[j++];
  
  outputs = new_outputs;
  outputs_n = new_outputs_n;
  
  for (i = 0; i < old_outputs_n; i++)
    output_destroy(old_outputs + i);
  
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
  const char* methodname = NULL;
  char* p;
  
  if (query == 1)
    {
      switch (method)
	{
	case LIBGAMMA_METHOD_DUMMY:                 methodname = "dummy";    break;
	case LIBGAMMA_METHOD_X_RANDR:               methodname = "randr";    break;
	case LIBGAMMA_METHOD_X_VIDMODE:             methodname = "vidmode";  break;
	case LIBGAMMA_METHOD_LINUX_DRM:             methodname = "drm";      break;
	case LIBGAMMA_METHOD_W32_GDI:               methodname = "gdi";      break;
	case LIBGAMMA_METHOD_QUARTZ_CORE_GRAPHICS:  methodname = "quartz";   break;
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
    {
      sitename = libgamma_method_default_site(method);
      if (sitename != NULL)
	{
	  sitename = memdup(sitename, strlen(sitename) + 1);
	  if (sitename == NULL)
	    return -1;
	}
    }
  
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
 * Print usage information and exit
 */
static void usage(void)
{
  printf("Usage: %s [-m method] [-s site] [-fkpq]\n", argv0);
  exit(1);
}


/**
 * Must not be started without stdin, stdout, or stderr (may be /dev/null)
 * 
 * argv[0] must refer to the real command name or pathname,
 * otherwise, re-execute will not work
 * 
 * The process closes stdout when the socket has been created
 * 
 * @signal  SIGUSR1  Re-execute to updated process image
 * @signal  SIGTERM  Terminate the process gracefully
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
  char* statebuffer = NULL;
  
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
    case 'p':
      preserve = 1;
      break;
    case 'f':
      foreground = 1;
      break;
    case 'k':
      keep_stderr = 1;
      break;
    case 'q':
      query = 1 + !!query;
      break;
    case '#': /* Internal, do not document */
      statefile = EARGF(usage());
      break;
    default:
      usage();
    }
  ARGEND;
  if (argc > 0)
    usage();
  
 restart: /* If C had comefrom: comefrom reexec_failure; */
  
  switch ((r = initialise(statefile == NULL, preserve, foreground, keep_stderr, query)))
    {
    case 1:
      break;
    case 2:
      goto fail;
    case 3:
      goto fail_libgamma;
    case 4:
      rc = 2;
      goto fail;
    default:
      return -r;
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
      unlink(statefile), statefile = NULL;
      reexec = 0; /* See `if (reexec && !terminate)` */
    }
  
  if (main_loop() < 0)
    goto fail;
  
  if (reexec && !terminate)
    {
      size_t buffer_size;
      int fd;
      
      /* `reexec = 0;` is done later in case of re-execute failure,
       * since it determines whether `statefile` shall be freed. */
      
      statefile = get_state_pathname();
      if (statefile == NULL)
	goto fail;
      
      buffer_size = marshal(NULL);
      statebuffer = malloc(buffer_size);
      if (statebuffer == NULL)
	goto fail;
      if (marshal(statebuffer) != buffer_size)
	abort();
      
      fd = open(statefile, O_CREAT, S_IRUSR | S_IWUSR);
      if (fd < 0)
	goto fail;
      
      if (nwrite(fd, statebuffer, buffer_size) != buffer_size)
	{
	  perror(argv0);
	  close(fd);
	  errno = 0;
	  goto fail;
	}
      free(statebuffer), statebuffer = NULL;
      
      if ((close(fd) < 0) && (errno != EINTR))
	goto fail;
      
      destroy(0);
      
      execlp(argv0_real ? argv0_real : argv0, argv0, "-#", statefile, preserve ? "-p" : NULL, NULL);
      perror(argv0);
      fprintf(stderr, "%s: restoring state without re-executing\n", argv0);
      free(argv0_real), argv0_real = NULL;
      goto restart;
    }
  
  rc = 0;
 done:
  free(statebuffer);
  if (statefile)
    unlink(statefile);
  if (reexec)
    free(statefile);
  destroy(1);
  return rc;
 fail:
  if (errno != 0)
    perror(argv0);
  goto done;
 fail_libgamma:
  libgamma_perror(argv0, gerror);
  goto done;
}

