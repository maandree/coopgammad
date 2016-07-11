/**
 * gammad -- Cooperative gamma server
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

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "arg.h"
#include "output.h"
#include "util.h"



/**
 * The name of the process
 */
char* argv0;

/**
 * Array of all outputs
 */
struct output* outputs = NULL;

/**
 * The nubmer of elements in `outputs`
 */
size_t outputs_n = 0;



/**
 * Get the pathname of the runtime file
 * 
 * @param   site    The site
 * @param   suffix  The suffix for the file
 * @return          The pathname of the file, `NULL` on error
 */
static char* get_pathname(libgamma_site_state_t* site, const char* suffix)
{
  const char* rundir = getenv("XDG_RUNTIME_DIR");
  const char* username = "";
  char* name = NULL;
  char* p;
  char* rc;
  struct passwd* pw;
  size_t n;
  int saved_errno;
  
  if (site->site)
    {
      name = memdup(site->site, strlen(site->site) + 1);
      if (name == NULL)
	goto fail;
    }
  else if ((name = libgamma_method_default_site(site->method)))
    {
      name = memdup(name, strlen(name) + 1);
      if (name == NULL)
	goto fail;
    }
  
  if (name != NULL)
    switch (site->method)
      {
      case LIBGAMMA_METHOD_X_RANDR:
      case LIBGAMMA_METHOD_X_VIDMODE:
	if ((p = strrchr(name, ':')))
	  if ((p = strchr(p, '.')))
	    *p = '\0';
      default:
	break;
      }
  
  if (!rundir || !*rundir)
    rundir = "/tmp";
  
  if ((pw = getpwuid(getuid())))
    username = pw->pw_name ? pw->pw_name : "";
  
  n = sizeof("/.gammad/~/.") + 3 * sizeof(int);
  n += strlen(rundir) + strlen(username) + strlen(name) + strlen(suffix);
  if (!(rc = malloc(n)))
    goto fail;
  sprintf(rc, "%s/.gammad/~%s/%i%s%s%s",
	  rundir, username, site->method, name ? "." : "", name ? name : "", suffix);
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
 * @param   site  The site
 * @return        The pathname of the socket, `NULL` on error
 */
static inline char* get_socket_pathname(libgamma_site_state_t* site)
{
  return get_pathname(site, ".socket");
}


/**
 * Get the pathname of the PID file
 * 
 * @param   site  The site
 * @return        The pathname of the PID file, `NULL` on error
 */
static inline char* get_pidfile_pathname(libgamma_site_state_t* site)
{
  return get_pathname(site, ".pid");
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
 *                   that must exist in the process if it is a gammad process
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
  char* end;
  pid_t pid = 0;
  size_t n;
  
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
      usleep(100000); /* 1 tenth of a second */
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
  token = malloc(sizeof("GAMMAD_PIDFILE_TOKEN=") + strlen(pidfile));
  if (token == NULL)
    return -1;
  sprintf(token, "GAMMAD_PIDFILE_TOKEN=%s", pidfile);
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
 * Print usage information and exit
 */
static void usage(void)
{
  printf("Usage: %s [-m method] [-s site] [-p]\n", argv0);
  exit(1);
}


/**
 * Must not be started without stdout or stderr (may be /dev/null)
 * 
 * The process closes stdout when the socket has been created
 * 
 * @return  0: Successful
 *          1: An error occurred
 *          2: Already running
 */
int main(int argc, char** argv)
{
  int method = -1, gerror, rc = 1, preserve = 0, r;
  char* sitename = NULL;
  libgamma_site_state_t site;
  libgamma_partition_state_t* partitions = NULL;
  libgamma_crtc_state_t* crtcs = NULL;
  size_t i, j, n, n0;
  char* pidpath = NULL;
  char* socketpath = NULL;
  
  memset(&site, 0, sizeof(site));
  
  ARGBEGIN
    {
    case 's':
      sitename = EARGF(usage());
      break;
    case 'm':
      method = get_method(EARGF(usage()));
      if (method < 0)
	goto fail;
      break;
    case 'p':
      preserve = 1;
      break;
    default:
      usage();
    }
  ARGEND;
  if (argc > 0)
    usage();
  
  /* Get method */
  if ((method < 0) && (libgamma_list_methods(&method, 1, 0) < 1))
    return fprintf(stderr, "%s: no adjustment method available\n", argv0), 1;
  
  /* Get site */
  if ((gerror = libgamma_site_initialise(&site, method, sitename)))
    goto fail_libgamma;
  
  /* Get PID file and socket pathname */
  if (!(pidpath = get_pidfile_pathname(&site)))
    goto fail;
  if (!(socketpath = get_socket_pathname(&site)))
    goto fail;
  
  /* Create PID file */
  if ((r = create_pidfile(pidpath)) < 0)
    {
      free(pidpath), pidpath = NULL;
      rc = -r;
      goto fail;
    }
  
  /* TODO socket */
  
  /* Signal the spawner that the service is ready */
  close(STDOUT_FILENO);
  /* Avoid potential catastrophes that would occur if a library that is being
   * used was so mindless as to write to stdout. */
  if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
    perror(argv0);
  
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
	  .crtc     = NULL,
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
  
  /* Done */
  rc = 0;
 done:
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
	if (outputs[i].supported != LIBGAMMA_NO)
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
	libgamma_crtc_destroy(outputs[i].crtc + i);
	output_destroy(outputs + i);
      }
  free(crtcs);
  if (partitions != NULL)
    for (i = 0; i < site.partitions_available; i++)
      libgamma_partition_destroy(partitions + i);
  free(partitions);
  libgamma_site_destroy(&site);
  free(socketpath);
  if (pidpath)
    unlink(pidpath);
  free(pidpath);
  return rc;
  /* Fail */
 fail:
  if (errno != 0)
    perror(argv0);
  goto done;
 fail_libgamma:
  libgamma_perror(argv0, gerror);
  goto done;
}

