/* See LICENSE file for copyright and license details. */
#include "servers-crtc.h"
#include "servers-gamma.h"
#include "servers-coopgamma.h"
#include "state.h"
#include "communication.h"
#include "util.h"

#include <errno.h>
#include <string.h>


/**
 * Handle a ‘Command: enumerate-crtcs’ message
 * 
 * @param   conn        The index of the connection
 * @param   message_id  The value of the ‘Message ID’ header
 * @return              Zero on success (even if ignored), -1 on error,
 *                      1 if connection closed
 */
int
handle_enumerate_crtcs(size_t conn, const char *restrict message_id)
{
	size_t i, n = 0, len;
	char *restrict buf;

	for (i = 0; i < outputs_n; i++)
		n += strlen(outputs[i].name) + 1;

	MAKE_MESSAGE(&buf, &n, n,
	             "Command: crtc-enumeration\n"
	             "In response to: %s\n"
	             "Length: %zu\n"
	             "\n",
	             message_id, n);

	for (i = 0; i < outputs_n; i++) {
		len = strlen(outputs[i].name);
		memcpy(&buf[n], outputs[i].name, len);
		buf[n + len] = '\n';
		n += len + 1;
	}

	return send_message(conn, buf, n);
}


/**
 * Get the name of a CRTC
 * 
 * @param   info  Information about the CRTC
 * @param   crtc  libgamma's state for the CRTC
 * @return        The name of the CRTC, `NULL` on error
 */
char *
get_crtc_name(const libgamma_crtc_information_t *restrict info, const libgamma_crtc_state_t *restrict crtc)
{
	char *name;
	if (!info->edid_error && info->edid) {
		return libgamma_behex_edid(info->edid, info->edid_length);
	} else if (!info->connector_name_error && info->connector_name) {
		name = malloc(3 * sizeof(size_t) + strlen(info->connector_name) + 2);
		if (name)
			sprintf(name, "%zu.%s", crtc->partition->partition, info->connector_name);
		return name;
	} else {
		name = malloc(2 * 3 * sizeof(size_t) + 2);
		if (name)
			sprintf(name, "%zu.%zu", crtc->partition->partition, crtc->crtc);
		return name;
	}
}


/**
 * Initialise the site
 * 
 * @return  Zero on success, -1 on error
 */
int
initialise_site(void)
{
	char *restrict sitename_dup = NULL;
	int gerror;

	if (sitename && !(sitename_dup = memdup(sitename, strlen(sitename) + 1)))
		goto fail;
	if ((gerror = libgamma_site_initialise(&site, method, sitename_dup)))
		goto fail_libgamma;

	return 0;

fail_libgamma:
	sitename_dup = NULL;
	libgamma_perror(argv0, gerror);
	errno = 0;
fail:
	free(sitename_dup);
	return -1;
}


/**
 * Get partitions and CRTC:s
 * 
 * @return  Zero on success, -1 on error
 */
int
initialise_crtcs(void)
{
	size_t i, j, n, n0;
	int gerror;

	/* Get partitions */
	outputs_n = 0;
	if (site.partitions_available) {
		partitions = calloc(site.partitions_available, sizeof(*partitions));
		if (!partitions)
			goto fail;
	}
	for (i = 0; i < site.partitions_available; i++) {
		if ((gerror = libgamma_partition_initialise(&partitions[i], &site, i)))
			goto fail_libgamma;
		outputs_n += partitions[i].crtcs_available;
	}

	/* Get CRTC:s */
	if (outputs_n) {
		crtcs = calloc(outputs_n, sizeof(*crtcs));
		if (!crtcs)
			goto fail;
	}
	for (i = 0, j = n = 0; i < site.partitions_available; i++)
		for (n0 = n, n += partitions[i].crtcs_available; j < n; j++)
			if ((gerror = libgamma_crtc_initialise(&crtcs[j], &partitions[i], j - n0)))
				goto fail_libgamma;

	return 0;

fail_libgamma:
	libgamma_perror(argv0, gerror);
	errno = 0;
fail:
	return -1;
}


/**
 * Merge the new state with an old state
 * 
 * @param   old_outputs    The old `outputs`
 * @param   old_outputs_n  The old `outputs_n`
 * @return                 Zero on success, -1 on error
 */
int
merge_state(struct output *restrict old_outputs, size_t old_outputs_n)
{
	struct output *restrict new_outputs = NULL;
	size_t new_outputs_n;
	size_t i, j;
	int cmp, is_same;

	/* How many outputs does the system now have? */
	i = j = new_outputs_n = 0;
	while (i < old_outputs_n && j < outputs_n) {
		cmp = strcmp(old_outputs[i].name, outputs[j].name);
		if (cmp <= 0)
			new_outputs_n++;
		i += cmp >= 0;
		j += cmp <= 0;
	}
	new_outputs_n += outputs_n - j;

	/* Allocate output state array */
	if (new_outputs_n > 0) {
		new_outputs = calloc(new_outputs_n, sizeof(*new_outputs));
		if (!new_outputs)
			return -1;
	}

	/* Merge output states */
	i = j = new_outputs_n = 0;
	while (i < old_outputs_n && j < outputs_n) {
		is_same = 0;
		cmp = strcmp(old_outputs[i].name, outputs[j].name);
		if (!cmp) {
			is_same = (old_outputs[i].depth      == outputs[j].depth      &&
			           old_outputs[i].red_size   == outputs[j].red_size   &&
			           old_outputs[i].green_size == outputs[j].green_size &&
			           old_outputs[i].blue_size  == outputs[j].blue_size);
		}
		if (is_same) {
			new_outputs[new_outputs_n] = old_outputs[i];
			new_outputs[new_outputs_n].crtc = outputs[j].crtc;
			memset(&old_outputs[i], 0, sizeof(*old_outputs));
			outputs[j].crtc = NULL;
			output_destroy(&outputs[j]);
			new_outputs_n++;
		} else if (cmp <= 0) {
			new_outputs[new_outputs_n++] = outputs[j];
		}
		i += cmp >= 0;
		j += cmp <= 0;
	}
	while (j < outputs_n)
		new_outputs[new_outputs_n++] = outputs[j++];

	/* Commit merge */
	free(outputs);
	outputs   = new_outputs;
	outputs_n = new_outputs_n;

	return 0;
}


/**
 * Disconnect from the site
 * 
 * @return  Zero on success, -1 on error
 */
int
disconnect(void)
{
	size_t i;

	if (!connected)
		return 0;
	connected = 0;

	for (i = 0; i < outputs_n; i++) {
		outputs[i].crtc = NULL;
		libgamma_crtc_destroy(&crtcs[i]);
	}
	free(crtcs);
	crtcs = NULL;

	for (i = 0; i < site.partitions_available; i++)
		libgamma_partition_destroy(&partitions[i]);
	free(partitions);
	partitions = NULL;

	libgamma_site_destroy(&site);
	memset(&site, 0, sizeof(site));

	return 0;
}


/**
 * Reconnect to the site
 * 
 * @return  Zero on success, -1 on error
 */
int
reconnect(void)
{
	struct output *restrict old_outputs;
	size_t i, old_outputs_n;

	if (connected)
		return 0;
	connected = 1;

	/* Remember old state */
	old_outputs   = outputs,   outputs   = NULL;
	old_outputs_n = outputs_n, outputs_n = 0;

	/* Get site */
	if (initialise_site() < 0)
		goto fail;

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
	if (preserve && preserve_gamma() < 0)
		goto fail;

	/* Merge state */
	if (merge_state(old_outputs, old_outputs_n) < 0)
		goto fail;
	for (i = 0; i < old_outputs_n; i++)
		output_destroy(old_outputs + i);
	free(old_outputs);
	old_outputs = NULL;
	old_outputs_n = 0;

	/* Reapply gamma ramps */
	reapply_gamma();

	return 0;

fail:
	for (i = 0; i < old_outputs_n; i++)
		output_destroy(&old_outputs[i]);
	free(old_outputs);
	return -1;
}
