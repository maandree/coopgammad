/* See LICENSE file for copyright and license details. */
#include "state.h"
#include "util.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/**
 * The name of the process
 */
char *restrict argv0; /* do not marshal */

/**
 * The real pathname of the process's binary,
 * `NULL` if `argv0` is satisfactory
 */
char *restrict argv0_real = NULL;

/**
 * Array of all outputs
 */
struct output *restrict outputs = NULL;

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
 * the site? 1 if disconnect, 2 if
 * reconnect, 0 otherwise.
 */
volatile sig_atomic_t connection = 0;

/**
 * List of all client's file descriptors
 * 
 * Unused slots, with index less than `connections_used`,
 * should have the value -1 (negative)
 */
int *restrict connections = NULL;

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
struct message *restrict inbound = NULL;

/**
 * The clients' connections' outbound-message buffers
 */
struct ring *restrict outbound = NULL;

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
char *restrict sitename = NULL;

/**
 * The libgamma site state
 */
libgamma_site_state_t site; /* do not marshal */

/**
 * The libgamma partition states
 */
libgamma_partition_state_t *restrict partitions = NULL; /* do not marshal */

/**
 * The libgamma CRTC states
 */
libgamma_crtc_state_t *restrict crtcs = NULL; /* do not marshal */

/**
 * Preserve gamma ramps at priority 0?
 */
int preserve = 0;


/**
 * As part of a state dump, dump one or two gamma ramp-trios
 * 
 * @param  left        The left ramps
 * @param  right       The right ramps
 * @param  depth       The gamma ramp type/depth
 * @param  have_right  Print right ramps?
 * @param  indent      Print indent
 */
static void
ramps_dump(union gamma_ramps *left, union gamma_ramps *right, signed depth, int have_right, const char *indent)
{
#define STRINGISE(SIDE, CH, N, BUF)\
	do {\
		if (!SIDE || !SIDE->u8.CH) {\
			strcpy(BUF, "null");\
		} else if (i < N) {\
			switch (depth) {\
			case -2: snprintf(BUF, sizeof(BUF), "%lf",        SIDE->d.CH[i]);   break;\
			case -1: snprintf(BUF, sizeof(BUF), "%f", (double)(SIDE->f.CH[i])); break;\
			case  8: snprintf(BUF, sizeof(BUF), "%02" PRIx8,  SIDE->u8.CH[i]);  break;\
			case 16: snprintf(BUF, sizeof(BUF), "%04" PRIx16, SIDE->u16.CH[i]); break;\
			case 32: snprintf(BUF, sizeof(BUF), "%08" PRIx32, SIDE->u32.CH[i]); break;\
			case 64: snprintf(BUF, sizeof(BUF), "%16" PRIx64, SIDE->u64.CH[i]); break;\
			default:\
				strcpy(BUF, "corrupt state");\
				break;\
			}\
		}\
	} while (0)

	char lr[320], lg[320], lb[320], rr[320], rg[320], rb[320];
	size_t rn = left ? left->u8.red_size   : right ? right->u8.red_size   : 0;
	size_t gn = left ? left->u8.green_size : right ? right->u8.green_size : 0;
	size_t bn = left ? left->u8.blue_size  : right ? right->u8.blue_size  : 0;
	size_t i, n = rn > gn ? rn : gn;
	n = n > bn ? n : bn;

	for (i = 0; i < n; i++) {
		*lr = *lg = *lb = *rr = *rg = *rb = '\0';

		STRINGISE(left, red,   rn, lr);
		STRINGISE(left, green, gn, lg);
		STRINGISE(left, blue,  bn, lb);

		if (have_right) {
			STRINGISE(right, red,   rn, rr);
			STRINGISE(right, green, gn, rg);
			STRINGISE(right, blue,  bn, rb);
		}

		if (have_right)
			fprintf(stderr, "%s%zu: %s, %s, %s :: %s, %s, %s\n", indent, i, lr, lg, lb, rr, rg, rb);
		else
			fprintf(stderr, "%s%zu: %s, %s, %s\n", indent, i, lr, lg, lb);
	}
}


/**
 * Dump the state to stderr
 */
void
state_dump(void)
{
	size_t i, j;
	struct output *restrict out;
	const char *str;
	struct filter *restrict filter;
	union gamma_ramps left;
	size_t depth;
		
	fprintf(stderr, "argv0: %s\n", argv0 ? argv0 : "(null)");
	fprintf(stderr, "Realpath of argv0: %s\n", argv0_real ? argv0_real : "(null)");
	fprintf(stderr, "Calibrations preserved: %s\n", preserve ? "yes" : "no");
	fprintf(stderr, "Connected: %s\n", connected ? "yes" : "no");
	fprintf(stderr, "Socket FD: %i\n", socketfd);
	fprintf(stderr, "Re-execution pending: %s\n", reexec ? "yes" : "no");
	fprintf(stderr, "Termination pending: %s\n", terminate ? "yes" : "no");
	if (0 <= connection && connection <= 2)
		fprintf(stderr, "Pending connection change: %s\n",
		        connection == 0 ? "none" : connection == 1 ? "disconnect" : "reconnect");
	else
		fprintf(stderr, "Pending connection change: %i (CORRUPT STATE)\n", connection);
	fprintf(stderr, "Adjustment method: %i\n", method);
	fprintf(stderr, "Site name: %s\n", sitename ? sitename : "(automatic)");
	fprintf(stderr, "Clients:\n");
	fprintf(stderr, "  Next empty slot: %zu\n", connections_ptr);
	fprintf(stderr, "  Initialised slots: %zu\n", connections_used);
	fprintf(stderr, "  Allocated slots: %zu\n", connections_alloc);
	if (!connections) {
		fprintf(stderr, "  File descriptor array is null\n");
	} else {
		for (i = 0; i < connections_used; i++) {
			if (connections[i] < 0) {
				fprintf(stderr, "  Slot %zu: empty\n", i);
				continue;
			}
			fprintf(stderr, "  Slot %zu:\n", i);
			fprintf(stderr, "    File descriptor: %i\n", connections[i]);
			if (!inbound) {
				fprintf(stderr, "    Inbound message array is null\n");
			} else {
				fprintf(stderr, "    Inbound message:\n");
				fprintf(stderr, "      Header array: %s\n", inbound[i].headers ? "non-null" : "null");
				fprintf(stderr, "      Headers: %zu\n", inbound[i].header_count);
				fprintf(stderr, "      Payload buffer: %s\n", inbound[i].payload ? "non-null" : "null");
				fprintf(stderr, "      Payload size: %zu\n", inbound[i].payload_size);
				fprintf(stderr, "      Payload write pointer: %zu\n", inbound[i].payload_ptr);
				fprintf(stderr, "      Message buffer: %s\n", inbound[i].buffer ? "non-null" : "null");
				fprintf(stderr, "      Message buffer size: %zu\n", inbound[i].buffer_size);
				fprintf(stderr, "      Message buffer write pointer: %zu\n", inbound[i].buffer_ptr);
				fprintf(stderr, "      Read stage: %i\n", inbound[i].stage);
			}
			if (!outbound) {
				fprintf(stderr, "    Outbound message array is null\n");
			} else {
				fprintf(stderr, "      Ring buffer: %s\n", outbound[i].buffer ? "non-null" : "null");
				fprintf(stderr, "      Head: %zu\n", outbound[i].end);
				fprintf(stderr, "      Tail: %zu\n", outbound[i].start);
				fprintf(stderr, "      Size: %zu\n", outbound[i].size);
			}
		}
	}
	fprintf(stderr, "Partition array: %s\n", partitions ? "non-null" : "null");
	fprintf(stderr, "CRTC array: %s\n", crtcs ? "non-null" : "null");
	fprintf(stderr, "Output:\n");
	fprintf(stderr, "  Output count: %zu\n", outputs_n);
	if (!outputs) {
		fprintf(stderr, "  Output array is null\n");
	} else {
		for (i = 0; i < outputs_n; i++) {
			out = outputs + i;
			fprintf(stderr, "  Output %zu:\n", i);
			fprintf(stderr, "    Depth: %i (%s)\n", out->depth,
			        out->depth == -1 ? "float" :
			        out->depth == -2 ? "double" :
			        out->depth ==  8 ? "uint8_t" :
			        out->depth == 16 ? "uint16_t" :
			        out->depth == 32 ? "uint32_t" :
			        out->depth == 64 ? "uint64_t" : "CORRUPT STATE");
			fprintf(stderr, "    Gamma supported: %s (%u)\n",
			        out->supported == LIBGAMMA_YES   ? "yes" :
			        out->supported == LIBGAMMA_NO    ? "no" :
			        out->supported == LIBGAMMA_MAYBE ? "maybe" :
			        "CORRUPT STATE", out->supported);
			fprintf(stderr, "    Name is EDID: %s\n", out->name_is_edid ? "yes" : "no");
			switch (out->colourspace) {
			case COLOURSPACE_UNKNOWN:         str = "unknown";                                     break;
			case COLOURSPACE_SRGB:            str = "sRGB with explicit gamut";                    break;
			case COLOURSPACE_SRGB_SANS_GAMUT: str = "sRGB with implicit gamut (actually illegal)"; break;
			case COLOURSPACE_RGB:             str = "RGB other than sRGB, with unknown gamut";     break;
			case COLOURSPACE_RGB_SANS_GAMUT:  str = "RGB other than sRGB, with listed gamut";      break;
			case COLOURSPACE_NON_RGB:         str = "Non-RGB multicolour";                         break;
			case COLOURSPACE_GREY:            str = "Monochrome or singlecolour scale";            break;
			default:                          str = "CORRUPT STATE";                               break;
			}
			fprintf(stderr, "    Colourspace: %s (%u)\n", str, out->colourspace);
			if (out->colourspace == COLOURSPACE_SRGB || out->colourspace == COLOURSPACE_RGB) {
				fprintf(stderr, "      Red (x, y): (%u / 1024, %u / 1024)\n", out->red_x, out->red_y);
				fprintf(stderr, "      Green (x, y): (%u / 1024, %u / 1024)\n", out->green_x, out->green_y);
				fprintf(stderr, "      Blue (x, y): (%u / 1024, %u / 1024)\n", out->blue_x, out->blue_y);
				fprintf(stderr, "      White (x, y): (%u / 1024, %u / 1024)\n", out->white_x, out->white_y);
				if (out->colourspace == COLOURSPACE_SRGB) {
					fprintf(stderr, "      Expected red (x, y): (655 / 1024, 338 / 1024)\n");
					fprintf(stderr, "      Expected green (x, y): (307 / 1024, 614 / 1024)\n");
					fprintf(stderr, "      Expected blue (x, y): (154 / 1024, 61 / 1024)\n");
					fprintf(stderr, "      Expected white (x, y): (320 / 1024, 337 / 1024)\n");
				}
			}
			if (out->supported) {
				fprintf(stderr, "    Gamma ramp size:\n");
				fprintf(stderr, "      Red: %zu stops\n", out->red_size);
				fprintf(stderr, "      Green: %zu stops\n", out->green_size);
				fprintf(stderr, "      Blue: %zu stops\n", out->blue_size);
				fprintf(stderr, "      Total: %zu bytes\n", out->ramps_size);
				fprintf(stderr, "    Name: %s\n", out->name ? out->name : "(null)");
				fprintf(stderr, "    CRTC state: %s\n", out->crtc ? "non-null" : "null");
				fprintf(stderr, "    Saved gamma ramps (stop: red, green, blue):\n");
				ramps_dump(&out->saved_ramps, NULL, out->depth, 0, "      ");
				fprintf(stderr, "    Filter table:\n");
				fprintf(stderr, "      Filter count: %zu\n", out->table_size);
				fprintf(stderr, "      Slots allocated: %zu\n", out->table_alloc);
				if (out->table_size > 0) {
					if (!out->table_filters)
						fprintf(stderr, "      Filter table is null\n");
					if (!out->table_sums)
						fprintf(stderr, "      Result table is null\n");
				}
				for (j = 0; j < out->table_size; j++) {
					filter = out->table_filters ? out->table_filters + j : NULL;
					fprintf(stderr, "      Filter %zu:\n", j);
					if (filter) {
						if (filter->lifespan == LIFESPAN_UNTIL_DEATH)
							fprintf(stderr, "        Client FD: %i\n", filter->client);
						switch (filter->lifespan) {
						case LIFESPAN_REMOVE:        str = "remove (ILLEGAL STATE)"; break;
						case LIFESPAN_UNTIL_REMOVAL: str = "until-removal";          break;
						case LIFESPAN_UNTIL_DEATH:   str = "until-death";            break;
						default:                     str = "CORRUPT STATE";          break;
						}
						fprintf(stderr, "        Lifespan: %s (%u)\n", str, filter->lifespan);
						fprintf(stderr, "        Priority: %"PRIi64"\n", filter->priority);
						fprintf(stderr, "        Class: %s\n", filter->class ? filter->class : "(null)");
						str = "yes";
						if (!filter->class)
							str = "no, is NULL";
						else if (strchr(filter->class, '\n'))
							str = "no, contains LF";
						else if (!strstr(filter->class, "::"))
							str = "no, does not contain \"::\"";
						else if (!strstr(strstr(filter->class, "::") + 2, "::"))
							str = "no, contains only one \"::\"";
						else if (verify_utf8(filter->class) < 0)
							str = "no, not UTF-8";
						fprintf(stderr, "        Class legal: %s\n", str);
						if (!filter->ramps && filter->lifespan != LIFESPAN_REMOVE)
							fprintf(stderr, "        Ramps are NULL\n");
					}
					if (filter ? filter->lifespan != LIFESPAN_REMOVE : !!out->table_sums) {
						switch (out->depth) {
						case -2:
							depth = sizeof(double);
							break;
						case -1:
							depth = sizeof(float);
							break;
						case 8: case 16: case 32: case 64:
							depth = (size_t)(out->depth) / 8;
							break;
						default:
							goto corrupt_depth;
						}
						if (filter && filter->ramps) {
							left.u8.red_size   = out->red_size;
							left.u8.green_size = out->green_size;
							left.u8.blue_size  = out->blue_size;
							left.u8.red   = filter->ramps;
							left.u8.green = left.u8.red + out->red_size * depth;
							left.u8.blue  = left.u8.green + out->green_size * depth;
						}
						fprintf(stderr, "        Ramps (stop: filter red, green, blue :: "
						                        "composite red, geen, blue):\n");
						ramps_dump((filter && filter->ramps) ? &left : NULL,
						           out->table_sums ? out->table_sums + j : NULL,
						           out->depth, 1, "          ");
					corrupt_depth:;
					}
				}
			}
		}
	}
}


/**
 * Destroy the state
 */
void
state_destroy(void)
{
	size_t i;

	for (i = 0; i < connections_used; i++) {
		if (connections[i] >= 0) {
			message_destroy(inbound + i);
			ring_destroy(outbound + i);
		}
	}
	free(inbound);
	free(outbound);
	free(connections);

	if (outputs)
		for (i = 0; i < outputs_n; i++)
			output_destroy(outputs + i);
	free(outputs);

	if (crtcs)
		for (i = 0; i < outputs_n; i++)
			libgamma_crtc_destroy(crtcs + i);
	free(crtcs);

	if (partitions)
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
size_t
state_marshal(void *restrict buf)
{
	size_t off = 0, i, n;
	char *restrict bs = buf;

	if (!argv0_real) {
		if (bs)
			bs[off] = '\0';
		off += 1;
	} else {
		n = strlen(argv0_real) + 1;
		if (bs)
			memcpy(&bs[off], argv0_real, n);
		off += n;
	}

	if (bs)
		*(size_t *)&bs[off] = outputs_n;
	off += sizeof(size_t);

	for (i = 0; i < outputs_n; i++)
		off += output_marshal(outputs + i, bs ? &bs[off] : NULL);

	if (bs)
		*(int *)&bs[off] = socketfd;
	off += sizeof(int);

	if (bs)
		*(sig_atomic_t *)&bs[off] = connection;
	off += sizeof(sig_atomic_t);

	if (bs)
		*(int *)&bs[off] = connected;
	off += sizeof(int);

	if (bs)
		*(size_t *)&bs[off] = connections_ptr;
	off += sizeof(size_t);

	if (bs)
		*(size_t *)&bs[off] = connections_used;
	off += sizeof(size_t);

	if (bs)
		memcpy(&bs[off], connections, connections_used * sizeof(*connections));
	off += connections_used * sizeof(*connections);

	for (i = 0; i < connections_used; i++) {
		if (connections[i] >= 0) {
			off += message_marshal(&inbound[i], bs ? &bs[off] : NULL);
			off += ring_marshal(&outbound[i], bs ? &bs[off] : NULL);
		}
	}

	if (bs)
		*(int *)&bs[off] = method;
	off += sizeof(int);

	if (bs)
		*(int *)&bs[off] = sitename != NULL;
	off += sizeof(int);
	if (sitename) {
		n = strlen(sitename) + 1;
		if (bs)
			memcpy(&bs[off], sitename, n);
		off += n;
	}

	if (bs)
		*(int *)&bs[off] = preserve;
	off += sizeof(int);

	return off;
}


/**
 * Unmarshal the state
 * 
 * @param   buf  Buffer for the marshalled data
 * @return       The number of unmarshalled bytes, 0 on error
 */
size_t
state_unmarshal(const void *restrict buf)
{
	size_t off = 0, i, n;
	const char *restrict bs = buf;

	connections = NULL;
	inbound = NULL;

	if (bs[off]) {
		n = strlen(&bs[off]) + 1;
		if (!(argv0_real = memdup(&bs[off], n)))
			return 0;
		off += n;
	} else {
		off += 1;
	}

	outputs_n = *(const size_t *)&bs[off];
	off += sizeof(size_t);

	outputs = calloc(outputs_n, sizeof(*outputs));
	if (!outputs)
		return 0;

	for (i = 0; i < outputs_n; i++) {
		off += n = output_unmarshal(outputs + i, &bs[off]);
		if (!n)
			return 0;
	}

	socketfd = *(const int *)&bs[off];
	off += sizeof(int);

	connection = *(const sig_atomic_t *)&bs[off];
	off += sizeof(sig_atomic_t);

	connected = *(const int *)&bs[off];
	off += sizeof(int);

	connections_ptr = *(const size_t *)&bs[off];
	off += sizeof(size_t);

	connections_alloc = connections_used = *(const size_t *)&bs[off];
	off += sizeof(size_t);

	if (connections_used > 0) {
		connections = memdup(&bs[off], connections_used * sizeof(*connections));
		if (!connections)
			return 0;
		off += connections_used * sizeof(*connections);

		inbound = malloc(connections_used * sizeof(*inbound));
		if (!inbound)
			return 0;

		outbound = malloc(connections_used * sizeof(*outbound));
		if (!outbound)
			return 0;
	}

	for (i = 0; i < connections_used; i++) {
		if (connections[i] >= 0) {
			off += n = message_unmarshal(&inbound[i], &bs[off]);
			if (!n)
				return 0;
			off += n = ring_unmarshal(&outbound[i], &bs[off]);
			if (!n)
				return 0;
		}
	}

	method = *(const int *)&bs[off];
	off += sizeof(int);

	if (*(const int *)&bs[off]) {
		off += sizeof(int);
		n = strlen(&bs[off]) + 1;
		if (!(sitename = memdup(&bs[off], n)))
			return 0;
		off += n;
	} else {
		off += sizeof(int);
	}

	preserve = *(const int *)&bs[off];
	off += sizeof(int);

	return off;
}


#if defined(__clang__)
# pragma GCC diagnostic pop
#endif
