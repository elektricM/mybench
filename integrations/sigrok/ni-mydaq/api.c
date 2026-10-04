/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Martin <martin@dolez.fr>
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

#include <config.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "protocol.h"

#define MAX_DEVICES		8
#define DEFAULT_SAMPLERATE	SR_KHZ(100)
#define TIMEBASE_HZ		100000000	/* sample clocks are 100 MHz / n */

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

/*
 * PulseView lists only logic analyzers and oscilloscopes, and libsigrok
 * has no DAQ class: a hardware-timed analog stream is closest to a scope.
 */
static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_CONTINUOUS | SR_CONF_GET | SR_CONF_SET,
};

/*
 * Input range per AI channel. PulseView shows channel-group options only
 * for keys it knows (VDIV yes, RANGE no), so the range is offered as
 * volts/div over 10 divisions: 2 V/div = ±10 V, 400 mV/div = ±2 V.
 */
static const uint32_t devopts_cg[] = {
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint64_t vdivs[][2] = {
	[NI_MYDAQ_RANGE_10V] = { 2, 1 },
	[NI_MYDAQ_RANGE_2V] = { 400, 1000 },
};

/* 1-2-5 steps up to the 200 kS/s per channel maximum; all divide 100 MHz. */
static const uint64_t samplerates[] = {
	SR_HZ(1), SR_HZ(2), SR_HZ(5),
	SR_HZ(10), SR_HZ(20), SR_HZ(50),
	SR_HZ(100), SR_HZ(200), SR_HZ(500),
	SR_KHZ(1), SR_KHZ(2), SR_KHZ(5),
	SR_KHZ(10), SR_KHZ(20), SR_KHZ(50),
	SR_KHZ(100), SR_KHZ(200),
};

static const char *channel_names[NI_MYDAQ_NUM_CHANNELS] = {
	[NI_MYDAQ_CH_AI0] = "AI0",
	[NI_MYDAQ_CH_AI1] = "AI1",
	[NI_MYDAQ_CH_AUDIO_L] = "AudioInL",
	[NI_MYDAQ_CH_AUDIO_R] = "AudioInR",
};

/*
 * A rate the sample clock makes exactly (100 MHz / n, up to 200 kS/s) is
 * kept; anything else becomes the nearest entry of the 1-2-5 list.
 */
static uint64_t coerce_samplerate(uint64_t requested)
{
	double best_dist, dist;
	uint64_t best;
	size_t i;

	if (requested >= 1 && requested <= samplerates[ARRAY_SIZE(samplerates) - 1]
			&& TIMEBASE_HZ % requested == 0)
		return requested;
	if (requested == 0)
		return samplerates[0];

	best = samplerates[0];
	best_dist = INFINITY;
	for (i = 0; i < ARRAY_SIZE(samplerates); i++) {
		dist = fabs(log((double)samplerates[i] / (double)requested));
		if (dist < best_dist) {
			best_dist = dist;
			best = samplerates[i];
		}
	}

	return best;
}

static int vdiv_idx(GVariant *data)
{
	uint64_t p, q;
	unsigned int i;

	g_variant_get(data, "(tt)", &p, &q);
	if (q == 0)
		return -1;
	/* By value: "0.4V" and "400mV" are the same setting. */
	for (i = 0; i < ARRAY_SIZE(vdivs); i++)
		if (p * vdivs[i][1] == vdivs[i][0] * q)
			return i;

	return -1;
}

static gboolean is_hex_string(const char *s)
{
	if (!*s)
		return FALSE;
	for (; *s; s++)
		if (!isxdigit((unsigned char)*s))
			return FALSE;

	return TRUE;
}

/*
 * conn= selects a myDAQ by USB location ("1-2.3", as libmydaq reports it),
 * by bus.address ("1.5", as other libsigrok USB drivers do) or by serial
 * number ("036CD1C6").
 */
static gboolean conn_matches(const char *conn, const mydaq_device_desc *d,
		gboolean by_serial)
{
	unsigned int bus, address;
	char tail;

	if (!conn)
		return TRUE;
	if (by_serial)
		return d->serial[0] && g_ascii_strcasecmp(conn, d->serial) == 0;
	if (strcmp(conn, d->location) == 0)
		return TRUE;
	if (sscanf(conn, "%u.%u%c", &bus, &address, &tail) == 2)
		return bus == d->bus && address == d->address;

	return FALSE;
}

static struct sr_dev_inst *new_device(const mydaq_device_desc *d)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	int i;

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("NI");
	sdi->model = g_strdup("myDAQ");
	/* Reading the serial needs the device: known now only for conn=<serial>. */
	if (d->serial[0])
		sdi->serial_num = g_strdup(d->serial);
	sdi->connection_id = g_strdup(d->location);

	devc = g_malloc0(sizeof(*devc));
	devc->samplerate = DEFAULT_SAMPLERATE;
	for (i = 0; i < NI_MYDAQ_NUM_CHANNELS; i++) {
		/* The audio inputs exclude AI0/AI1: off by default. */
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG,
			i < NI_MYDAQ_NUM_AI, channel_names[i]);
		if (i < NI_MYDAQ_NUM_AI) {
			cg = sr_channel_group_new(sdi, channel_names[i], NULL);
			cg->channels = g_slist_append(NULL, ch);
			devc->cg[i] = cg;
			devc->range[i] = NI_MYDAQ_RANGE_10V;
		}
	}
	sdi->priv = devc;

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	mydaq_device_desc list[MAX_DEVICES];
	mydaq_context *mctx;
	struct sr_config *src;
	const char *conn;
	gboolean by_serial;
	GSList *devices, *l;
	size_t count, i;
	int rc;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_CONN)
			conn = g_variant_get_string(src->data, NULL);
	}

	rc = mydaq_context_new(&mctx);
	if (rc != MYDAQ_OK) {
		sr_err("Cannot initialise libmydaq: %s.", mydaq_strerror(rc));
		return NULL;
	}
	ni_mydaq_attach_log(mctx);

	/*
	 * Enumeration never opens a device, so a scan does not disturb a
	 * myDAQ that another program (or a VM) is using. Only conn=<serial>
	 * opens each myDAQ briefly to read its serial number.
	 */
	by_serial = FALSE;
	rc = mydaq_enumerate(mctx, list, MAX_DEVICES, &count, 0);
	if (rc == MYDAQ_OK && conn && count > 0 && is_hex_string(conn)) {
		for (i = 0; i < count && i < MAX_DEVICES; i++)
			if (conn_matches(conn, &list[i], FALSE))
				break;
		if (i == count || i == MAX_DEVICES) {
			by_serial = TRUE;
			rc = mydaq_enumerate(mctx, list, MAX_DEVICES, &count,
				MYDAQ_ENUM_READ_SERIAL);
		}
	}
	if (rc != MYDAQ_OK) {
		sr_err("Cannot list USB devices: %s.", mydaq_strerror(rc));
		mydaq_context_free(mctx);
		return NULL;
	}

	devices = NULL;
	for (i = 0; i < count && i < MAX_DEVICES; i++) {
		if (!conn_matches(conn, &list[i], by_serial))
			continue;
		sr_dbg("Found a myDAQ at %s.", list[i].location);
		devices = g_slist_append(devices, new_device(&list[i]));
	}
	mydaq_context_free(mctx);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	mydaq_info info;
	int rc;

	devc = sdi->priv;
	rc = mydaq_context_new(&devc->mctx);
	if (rc != MYDAQ_OK) {
		sr_err("Cannot initialise libmydaq: %s.", mydaq_strerror(rc));
		return ni_mydaq_err_to_sr(rc);
	}
	ni_mydaq_attach_log(devc->mctx);

	/* Runs the device initialisation and reads the calibration (~2 s). */
	if (sdi->connection_id) {
		rc = mydaq_open_location(devc->mctx, sdi->connection_id, 0,
			&devc->dev);
		if (rc == MYDAQ_ERR_NOT_FOUND && sdi->serial_num)
			rc = mydaq_open(devc->mctx, sdi->serial_num, 0, &devc->dev);
	} else {
		rc = mydaq_open(devc->mctx, sdi->serial_num, 0, &devc->dev);
	}
	if (rc != MYDAQ_OK) {
		sr_err("Cannot open the myDAQ%s%s: %s.",
			sdi->connection_id ? " at " : "",
			sdi->connection_id ? sdi->connection_id : "",
			ni_mydaq_strerror(rc));
		devc->dev = NULL;
		mydaq_context_free(devc->mctx);
		devc->mctx = NULL;
		return ni_mydaq_err_to_sr(rc);
	}

	/* Right after an init the device can refuse commands for a moment. */
	rc = mydaq_wait_ready(devc->dev);
	if (rc != MYDAQ_OK)
		sr_warn("The myDAQ is not ready yet: %s.", ni_mydaq_strerror(rc));

	if (mydaq_get_info(devc->dev, &info) == MYDAQ_OK) {
		if (!sdi->serial_num && info.serial[0])
			sdi->serial_num = g_strdup(info.serial);
		sr_info("Opened myDAQ %s at %s (initialisation %.1f s).",
			info.serial, info.location, info.init_seconds);
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	ni_mydaq_acquisition_teardown(devc);
	mydaq_close(devc->dev);
	devc->dev = NULL;
	mydaq_context_free(devc->mctx);
	devc->mctx = NULL;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int idx;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	if (cg) {
		if (key != SR_CONF_VDIV)
			return SR_ERR_NA;
		if ((idx = std_cg_idx(cg, devc->cg, NI_MYDAQ_NUM_AI)) < 0)
			return SR_ERR_ARG;
		*data = g_variant_new("(tt)", vdivs[devc->range[idx]][0],
			vdivs[devc->range[idx]][1]);
		return SR_OK;
	}

	switch (key) {
	case SR_CONF_CONN:
		if (!sdi->connection_id)
			return SR_ERR_NA;
		*data = g_variant_new_string(sdi->connection_id);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devc->limit_msec);
		break;
	case SR_CONF_CONTINUOUS:
		*data = g_variant_new_boolean(devc->continuous);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/* Settings apply to the next acquisition. */
static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t requested;
	char *wanted, *got;
	int idx, range;

	devc = sdi->priv;

	if (cg) {
		if (key != SR_CONF_VDIV)
			return SR_ERR_NA;
		if ((idx = std_cg_idx(cg, devc->cg, NI_MYDAQ_NUM_AI)) < 0)
			return SR_ERR_ARG;
		if ((range = vdiv_idx(data)) < 0)
			return SR_ERR_ARG;
		devc->range[idx] = (enum ni_mydaq_range)range;
		return SR_OK;
	}

	switch (key) {
	case SR_CONF_SAMPLERATE:
		requested = g_variant_get_uint64(data);
		devc->samplerate = coerce_samplerate(requested);
		if (devc->samplerate != requested) {
			wanted = sr_samplerate_string(requested);
			got = sr_samplerate_string(devc->samplerate);
			sr_info("Sample rate %s is not available, using %s.",
				wanted, got);
			g_free(wanted);
			g_free(got);
		}
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		break;
	case SR_CONF_CONTINUOUS:
		devc->continuous = g_variant_get_boolean(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	if (cg) {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
			break;
		case SR_CONF_VDIV:
			*data = std_gvar_tuple_array(ARRAY_AND_SIZE(vdivs));
			break;
		default:
			return SR_ERR_NA;
		}
		return SR_OK;
	}

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts,
			devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	int ret;

	ret = ni_mydaq_acquisition_setup(sdi);
	if (ret != SR_OK)
		return ret;

	ret = sr_session_source_add(sdi->session, -1, 0, NI_MYDAQ_POLL_MS,
		ni_mydaq_receive_data, (struct sr_dev_inst *)sdi);
	if (ret != SR_OK) {
		ni_mydaq_acquisition_teardown(sdi->priv);
		return ret;
	}

	return std_session_send_df_header(sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	if (!devc->task)
		return SR_OK;	/* already stopped (limit reached, error) */

	sr_session_source_remove(sdi->session, -1);
	ni_mydaq_acquisition_teardown(devc);

	return std_session_send_df_end(sdi);
}

static struct sr_dev_driver ni_mydaq_driver_info = {
	.name = "ni-mydaq",
	.longname = "NI myDAQ",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(ni_mydaq_driver_info);
