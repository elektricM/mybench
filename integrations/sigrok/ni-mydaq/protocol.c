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
#include <math.h>
#include <string.h>
#include "protocol.h"

/* libmydaq messages go to the sigrok log; they may come from its threads. */
static void log_bridge(void *user, mydaq_log_level level, const char *message)
{
	(void)user;

	switch (level) {
	case MYDAQ_LOG_ERROR:
		sr_err("libmydaq: %s", message);
		break;
	case MYDAQ_LOG_WARNING:
		sr_warn("libmydaq: %s", message);
		break;
	case MYDAQ_LOG_INFO:
		sr_info("libmydaq: %s", message);
		break;
	case MYDAQ_LOG_DEBUG:
		sr_dbg("libmydaq: %s", message);
		break;
	default:
		sr_spew("libmydaq: %s", message);
		break;
	}
}

SR_PRIV void ni_mydaq_attach_log(mydaq_context *mctx)
{
	mydaq_log_level level;

	switch (sr_log_loglevel_get()) {
	case SR_LOG_NONE:
		level = MYDAQ_LOG_NONE;
		break;
	case SR_LOG_ERR:
		level = MYDAQ_LOG_ERROR;
		break;
	case SR_LOG_WARN:
		level = MYDAQ_LOG_WARNING;
		break;
	case SR_LOG_INFO:
		level = MYDAQ_LOG_INFO;
		break;
	case SR_LOG_DBG:
		level = MYDAQ_LOG_DEBUG;
		break;
	default:
		level = MYDAQ_LOG_TRACE;
		break;
	}
	mydaq_context_set_log(mctx, level, log_bridge, NULL);
}

/* libmydaq's description, with a hint where the user can act on it. */
SR_PRIV const char *ni_mydaq_strerror(int rc)
{
	switch (rc) {
	case MYDAQ_ERR_BUSY:
		return "the myDAQ is in use by another program (MYBENCH, "
			"the ngscopeclient bridge, another PulseView or "
			"sigrok-cli, a virtual machine)";
	case MYDAQ_ERR_ACCESS:
		return "permission denied by the operating system (on "
			"Linux, install a udev rule for USB 3923:755b)";
	case MYDAQ_ERR_NO_DEVICE:
		return "the myDAQ was disconnected";
	case MYDAQ_ERR_NOT_FOUND:
		return "no matching myDAQ is attached";
	case MYDAQ_ERR_OVERFLOW:
		return "samples were lost because the host did not read "
			"them in time (try a lower sample rate)";
	default:
		return mydaq_strerror(rc);
	}
}

SR_PRIV int ni_mydaq_err_to_sr(int rc)
{
	switch (rc) {
	case MYDAQ_OK:
		return SR_OK;
	case MYDAQ_ERR_INVALID_ARG:
		return SR_ERR_ARG;
	case MYDAQ_ERR_NO_MEM:
		return SR_ERR_MALLOC;
	case MYDAQ_ERR_TIMEOUT:
		return SR_ERR_TIMEOUT;
	case MYDAQ_ERR_NOT_SUPPORTED:
		return SR_ERR_NA;
	case MYDAQ_ERR_NO_DEVICE:
		return SR_ERR_DEV_CLOSED;
	case MYDAQ_ERR_USB:
		return SR_ERR_IO;
	default:
		return SR_ERR;
	}
}

SR_PRIV void ni_mydaq_acquisition_teardown(struct dev_context *devc)
{
	size_t i;

	if (devc->task) {
		mydaq_ai_task_destroy(devc->task);	/* stops the hardware */
		devc->task = NULL;
	}
	for (i = 0; i < MYDAQ_AI_MAX_CHANNELS; i++) {
		g_slist_free(devc->ch_list[i]);
		devc->ch_list[i] = NULL;
	}
	g_free(devc->volts);
	devc->volts = NULL;
	g_free(devc->fbuf);
	devc->fbuf = NULL;
	devc->num_enabled = 0;
}

/* Samples per channel to send, from the limits; 0 = until stopped. */
static uint64_t sample_limit(const struct dev_context *devc)
{
	uint64_t limit, from_time;

	if (devc->continuous)
		return 0;
	limit = devc->limit_samples;
	if (devc->limit_msec) {
		/* Counted on the sample clock, not on the host's clock. */
		from_time = (devc->limit_msec * devc->samplerate + 999) / 1000;
		if (from_time == 0)
			from_time = 1;
		if (!limit || from_time < limit)
			limit = from_time;
	}

	return limit;
}

SR_PRIV int ni_mydaq_acquisition_setup(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	mydaq_ai_config cfg;
	gboolean want_ai, want_audio;
	double burst_s, rate;
	GSList *l;
	size_t i, n;
	int rc;

	devc = sdi->priv;
	if (!devc->dev)
		return SR_ERR_DEV_CLOSED;
	if (devc->task)
		ni_mydaq_acquisition_teardown(devc);

	/* sdi->channels is ordered by index: AI0, AI1, AudioInL, AudioInR. */
	want_ai = want_audio = FALSE;
	n = 0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch->enabled)
			continue;
		if (ch->index >= NI_MYDAQ_CH_AUDIO_L)
			want_audio = TRUE;
		else
			want_ai = TRUE;
		if (n < MYDAQ_AI_MAX_CHANNELS)
			devc->enabled[n] = ch;
		n++;
	}
	if (n == 0) {
		sr_err("No channel enabled.");
		return SR_ERR_ARG;
	}
	if (want_ai && want_audio) {
		sr_err("AudioInL/AudioInR cannot be acquired together with "
			"AI0/AI1 (they share the converters): enable either "
			"AI channels or audio channels.");
		return SR_ERR_ARG;
	}
	if (n > MYDAQ_AI_MAX_CHANNELS)
		return SR_ERR_BUG;

	memset(&cfg, 0, sizeof(cfg));
	for (i = 0; i < n; i++) {
		ch = devc->enabled[i];
		cfg.channels[i].channel = (mydaq_ai_channel)ch->index;
		if (ch->index < NI_MYDAQ_NUM_AI)
			cfg.channels[i].range = (mydaq_ai_range)devc->range[ch->index];
		else
			cfg.channels[i].range = MYDAQ_AI_RANGE_2V; /* audio: fixed */
		/* 16-bit converter: 0.3 mV steps on ±10 V, 61 uV on ±2 V. */
		devc->digits[i] = cfg.channels[i].range == MYDAQ_AI_RANGE_10V ? 4 : 5;
		devc->ch_list[i] = g_slist_append(NULL, ch);
	}
	devc->num_enabled = n;
	cfg.num_channels = n;
	cfg.sample_rate = (double)devc->samplerate;
	cfg.mode = MYDAQ_CONTINUOUS;
	/* 0: libmydaq's defaults, ~100 ms bursts and >= 2 s of host buffer. */
	cfg.samples_per_channel = 0;

	devc->volts = g_try_malloc(sizeof(double) * NI_MYDAQ_CHUNK * n);
	devc->fbuf = g_try_malloc(sizeof(float) * NI_MYDAQ_CHUNK);
	if (!devc->volts || !devc->fbuf) {
		ni_mydaq_acquisition_teardown(devc);
		return SR_ERR_MALLOC;
	}

	rc = mydaq_ai_task_create(devc->dev, &cfg, &devc->task);
	if (rc != MYDAQ_OK) {
		sr_err("Cannot set up the acquisition: %s.", ni_mydaq_strerror(rc));
		ni_mydaq_acquisition_teardown(devc);
		return ni_mydaq_err_to_sr(rc);
	}
	rate = mydaq_ai_task_rate(devc->task);
	if (fabs(rate - (double)devc->samplerate) > 1e-6 * rate)
		sr_warn("Sample clock runs at %.6f S/s, not %.0f S/s.", rate,
			(double)devc->samplerate);

	devc->sample_limit = sample_limit(devc);
	devc->samples_sent = 0;
	/*
	 * The device sends data in bursts of ~100 ms (at least 16 samples
	 * per channel, i.e. 16 s at 1 S/s): no data for several bursts
	 * means the stream stalled.
	 */
	burst_s = MAX(16.0, rate * 0.1) / rate;
	devc->stall_timeout_us = (int64_t)((5.0 + 3.0 * burst_s) * 1e6);

	rc = mydaq_ai_task_start(devc->task);
	if (rc != MYDAQ_OK) {
		sr_err("Cannot start the acquisition: %s.", ni_mydaq_strerror(rc));
		ni_mydaq_acquisition_teardown(devc);
		return ni_mydaq_err_to_sr(rc);
	}
	devc->last_data_us = g_get_monotonic_time();
	sr_dbg("Acquiring %zu channel(s) at %.0f S/s, limit %" PRIu64
		" samples per channel.", n, rate, devc->sample_limit);

	return SR_OK;
}

/* Sends the block just read (grouped by channel), one packet per channel. */
static int send_block(const struct sr_dev_inst *sdi, size_t n)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	const double *src;
	size_t c, i;
	int ret;

	devc = sdi->priv;
	for (c = 0; c < devc->num_enabled; c++) {
		src = devc->volts + c * n;
		for (i = 0; i < n; i++)
			devc->fbuf[i] = (float)src[i];

		sr_analog_init(&analog, &encoding, &meaning, &spec,
			devc->digits[c]);
		meaning.channels = devc->ch_list[c];
		meaning.mq = SR_MQ_VOLTAGE;
		meaning.unit = SR_UNIT_VOLT;
		/* AI0/AI1 are DC coupled, the audio inputs AC coupled. */
		meaning.mqflags = devc->enabled[c]->index < NI_MYDAQ_NUM_AI ?
			SR_MQFLAG_DC : 0;
		analog.num_samples = (uint32_t)n;
		analog.data = devc->fbuf;

		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		ret = sr_session_send(sdi, &packet);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

static int abort_acquisition(struct sr_dev_inst *sdi, int rc)
{
	sr_err("Acquisition stopped: %s.", ni_mydaq_strerror(rc));
	sr_dev_acquisition_stop(sdi);

	return TRUE;
}

/* Session timer: forwards what the acquisition buffered, without blocking. */
SR_PRIV int ni_mydaq_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	size_t n, reads;
	int rc, ret;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi || !(devc = sdi->priv) || !devc->task)
		return TRUE;

	for (reads = 0; reads < NI_MYDAQ_MAX_READS; reads++) {
		n = mydaq_ai_task_available(devc->task);
		if (n == 0)
			break;
		if (n > NI_MYDAQ_CHUNK)
			n = NI_MYDAQ_CHUNK;
		if (devc->sample_limit &&
				n > devc->sample_limit - devc->samples_sent)
			n = (size_t)(devc->sample_limit - devc->samples_sent);

		/* Reports a lost stream (overflow, unplug) even with data. */
		rc = mydaq_ai_task_read(devc->task, devc->volts, n,
			MYDAQ_GROUP_BY_CHANNEL, 0);
		if (rc != MYDAQ_OK)
			return abort_acquisition(sdi, rc);
		ret = send_block(sdi, n);
		if (ret != SR_OK) {
			sr_err("Cannot send samples: %s.", sr_strerror(ret));
			sr_dev_acquisition_stop(sdi);
			return TRUE;
		}
		devc->samples_sent += n;
		devc->last_data_us = g_get_monotonic_time();
		if (devc->sample_limit &&
				devc->samples_sent >= devc->sample_limit) {
			sr_dev_acquisition_stop(sdi);
			return TRUE;
		}
	}

	if (!mydaq_is_connected(devc->dev))
		return abort_acquisition(sdi, MYDAQ_ERR_NO_DEVICE);
	if (!mydaq_ai_task_is_running(devc->task) &&
			mydaq_ai_task_available(devc->task) == 0) {
		/* The task ended on an error: a read returns it. */
		rc = mydaq_ai_task_read(devc->task, devc->volts, 1,
			MYDAQ_GROUP_BY_CHANNEL, 0);
		return abort_acquisition(sdi,
			rc != MYDAQ_OK ? rc : MYDAQ_ERR_STATE);
	}
	if (g_get_monotonic_time() - devc->last_data_us >
			devc->stall_timeout_us) {
		sr_err("Acquisition stopped: no data from the myDAQ for %.0f s.",
			(double)devc->stall_timeout_us / 1e6);
		sr_dev_acquisition_stop(sdi);
	}

	return TRUE;
}
