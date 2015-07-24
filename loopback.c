
/*
 * Greybus Simulator
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <libsoc_gpio.h>
#include <linux/fs.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "gbsim.h"

/* Misc */
#define GB_LOOPBACK_MAX				4
#define GB_OPERATION_DATA_SIZE_MAX		\
	(0x800 - sizeof(struct gb_loopback_transfer_request))

enum {
	LOOPBACK_FSM_IDLE = 0,
	LOOPBACK_FSM_PING_HOST,
	LOOPBACK_FSM_TRANSFER_HOST,
	LOOPBACK_FSM_SINK_HOST,
};

struct gb_loopback {
	uint16_t	cport_id;
	uint16_t	hd_cport_id;
	uint8_t		id;
	bool		init;
	pthread_mutex_t	loopback_data;
	uint8_t		module_id;
	uint32_t	ms;
	size_t		size;
	int		state;
};

static struct gb_loopback gblb;
static bool terminate_thread;
static int thread_started;
static int port_count;
static pthread_t loopback_pthread;
static pthread_barrier_t loopback_barrier;

static int gb_loopback_ping_host(struct gb_loopback *gblbp)
{
	/* TBD */
	return 0;
}

static int gb_loopback_transfer_host(struct gb_loopback *gblbp, size_t size)
{
	/* TBD */
	return 0;
}

static int gb_loopback_sink_host(struct gb_loopback *gblbp, size_t size)
{
	/* TBD */
	return 0;
}

/* Analog based on the firmware loop */
static void *loopback_thread(void *param)
{
	int state;

	pthread_barrier_wait(&loopback_barrier);

	while (!terminate_thread) {
		if (!gblb.init)
			state = LOOPBACK_FSM_IDLE;
		else
			state = gblb.state;

		switch (state) {
		case LOOPBACK_FSM_PING_HOST:
			gb_loopback_ping_host(&gblb);
			break;
		case LOOPBACK_FSM_TRANSFER_HOST:
			gb_loopback_transfer_host(&gblb, gblb.size);
			break;
		case LOOPBACK_FSM_SINK_HOST:
			gb_loopback_sink_host(&gblb, gblb.size);
			break;
		case LOOPBACK_FSM_IDLE:
		default:
			sleep(1);
			continue;
		}
	}

	gbsim_info("Loopback thread exit\n");
	pthread_exit(NULL);
	return NULL;
}

static void loopback_init_port(uint8_t module_id, uint16_t cport_id,
			  uint16_t hd_cport_id, uint8_t id)
{
	gblb.module_id = module_id;
	gblb.cport_id = cport_id;
	gblb.hd_cport_id = hd_cport_id;
	gblb.id = id;
	gblb.init = true;
	gbsim_debug("Loopback Module %hu Cport %hhu HDCport %hhu index %d\n",
		    module_id, cport_id, hd_cport_id, port_count);
}


int loopback_handler(uint16_t cport_id, uint16_t hd_cport_id, void *rbuf,
		 size_t rsize, void *tbuf, size_t tsize)
{
	char data[GB_OPERATION_DATA_SIZE_MAX];
	struct op_header *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	size_t payload_size = 0;
	uint16_t message_size;
	uint8_t module_id;
	uint8_t result = PROTOCOL_STATUS_SUCCESS;
	struct gb_loopback_transfer_request *request;
	struct gb_loopback_transfer_response *response;
	int ret;

	module_id = cport_to_module_id(cport_id);

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct op_header *)&op_req->header;

	/* Associate the module_id and cport_id with the device fd */
	loopback_init_port(module_id, cport_id, hd_cport_id, oph->id);

	switch (oph->type) {
	case GB_LOOPBACK_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(struct gb_protocol_version_response);
		op_rsp->pv_rsp.major = GB_LOOPBACK_VERSION_MAJOR;
		op_rsp->pv_rsp.minor = GB_LOOPBACK_VERSION_MINOR;
		gbsim_debug("Module %hhu -> AP CPort %hu LOOPBACK version\n",
			    module_id, cport_id);
		break;
	case GB_LOOPBACK_TYPE_PING:
		gbsim_debug("Module %hhu -> AP CPort %hu LOOPBACK ping\n",
			    module_id, cport_id);
		break;
	case GB_LOOPBACK_TYPE_TRANSFER:
		request = &op_req->loopback_xfer_req;
		response = (struct gb_loopback_transfer_response *)&data[0];
		gbsim_debug("Module %hhu -> AP CPort %hu LOOPBACK xfer rx %hu\n",
			    module_id, cport_id, request->len);
		if (request->len > GB_OPERATION_DATA_SIZE_MAX) {
			gbsim_error("Module %hhu -> AP Cport %hu rx %hu bytes\n",
				    module_id, cport_id, request->len);
			result = PROTOCOL_STATUS_INVALID;
		} else {
			memcpy(&response->data, request->data, request->len);
			payload_size = sizeof(*response) + request->len;
		}
		break;
	case GB_LOOPBACK_TYPE_SINK:
		request = &op_req->loopback_xfer_req;
		gbsim_debug("Module %hhu -> AP CPort %hu LOOPBACK sink rx %hu\n",
			    module_id, cport_id, request->len);
		break;
	default:
		gbsim_error("LOOPBACK operation type %02x not supported\n",
			    oph->type);
		return -EINVAL;
	}

	/* Fill in the response header */
	message_size = sizeof(struct op_header) + payload_size;
	op_rsp->header.size = htole16(message_size);
	op_rsp->header.id = oph->id;
	op_rsp->header.type = OP_RESPONSE | oph->type;
	op_rsp->header.result = result;

	/* Store the cport id in the header pad bytes */
	op_rsp->header.pad[0] = hd_cport_id & 0xff;
	op_rsp->header.pad[1] = (hd_cport_id >> 8) & 0xff;

	if (verbose)
		gbsim_dump(op_rsp, message_size);
	ret = write(to_ap, op_rsp, message_size);
	if (ret < 0)
		return ret;
	return 0;

}

void loopback_cleanup(void)
{
	if (thread_started) {
		/* signal termination */
		terminate_thread = true;

		/* sync */
		pthread_join(loopback_pthread, NULL);
		pthread_barrier_destroy(&loopback_barrier);
	}
}

void loopback_init(void)
{
	int ret;

	/* Init thread */
	pthread_barrier_init(&loopback_barrier, 0, 2);
	ret = pthread_create(&loopback_pthread, NULL, loopback_thread, NULL);
	if (ret < 0) {
		perror("can't create loopback thread");
		loopback_cleanup();
		return;
	}

	/* Sync thread startup */
	thread_started = 1;
	pthread_barrier_wait(&loopback_barrier);
}
