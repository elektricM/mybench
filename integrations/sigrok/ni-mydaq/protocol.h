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

#ifndef LIBSIGROK_HARDWARE_NI_MYDAQ_PROTOCOL_H
#define LIBSIGROK_HARDWARE_NI_MYDAQ_PROTOCOL_H

/*
 * NI myDAQ, through libmydaq (the MYBENCH project's USB driver library).
 * libmydaq does all the device communication on its own libusb context and
 * event thread; this driver only maps sigrok's configuration onto a
 * hardware-timed, continuous analog-input task and drains its buffer from
 * a session timer.
 */

#include <stdint.h>
#include <glib.h>
#include <libmydaq/mydaq.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ni-mydaq"

/* Channel indices equal libmydaq's mydaq_ai_channel values. */
#define NI_MYDAQ_CH_AI0		0
#define NI_MYDAQ_CH_AI1		1
#define NI_MYDAQ_CH_AUDIO_L	2
#define NI_MYDAQ_CH_AUDIO_R	3
#define NI_MYDAQ_NUM_CHANNELS	4
#define NI_MYDAQ_NUM_AI		2	/* AI0/AI1: the channels with a selectable range */

/* Session timer period: drain whatever the acquisition buffered meanwhile. */
#define NI_MYDAQ_POLL_MS	20
/* Largest read per call, in samples per channel. */
#define NI_MYDAQ_CHUNK		16384
/* Upper bound of reads per timer tick, so the main loop stays responsive. */
#define NI_MYDAQ_MAX_READS	64

/* Index into the volts/div table (api.c); equals mydaq_ai_range. */
enum ni_mydaq_range {
	NI_MYDAQ_RANGE_10V = MYDAQ_AI_RANGE_10V,
	NI_MYDAQ_RANGE_2V = MYDAQ_AI_RANGE_2V,
};

struct dev_context {
	/* Open device (dev_open .. dev_close). */
	mydaq_context *mctx;
	mydaq_device *dev;

	/* Settings. */
	uint64_t samplerate;
	uint64_t limit_samples;
	uint64_t limit_msec;
	gboolean continuous;
	enum ni_mydaq_range range[NI_MYDAQ_NUM_AI];
	struct sr_channel_group *cg[NI_MYDAQ_NUM_AI];

	/* Running acquisition. */
	mydaq_ai_task *task;
	size_t num_enabled;
	struct sr_channel *enabled[MYDAQ_AI_MAX_CHANNELS];
	int digits[MYDAQ_AI_MAX_CHANNELS];
	uint64_t sample_limit;		/* per channel, 0 = until stopped */
	uint64_t samples_sent;
	int64_t last_data_us;
	int64_t stall_timeout_us;
	GSList *ch_list[MYDAQ_AI_MAX_CHANNELS];	/* one-channel lists for the packets */
	double *volts;
	float *fbuf;
};

SR_PRIV void ni_mydaq_attach_log(mydaq_context *mctx);
SR_PRIV const char *ni_mydaq_strerror(int rc);
SR_PRIV int ni_mydaq_err_to_sr(int rc);
SR_PRIV int ni_mydaq_acquisition_setup(const struct sr_dev_inst *sdi);
SR_PRIV void ni_mydaq_acquisition_teardown(struct dev_context *devc);
SR_PRIV int ni_mydaq_receive_data(int fd, int revents, void *cb_data);

#endif
