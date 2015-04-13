/*
 * Greybus Simulator
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <stdlib.h>
#include <stdio.h>
#include <linux/types.h>
#include <unistd.h>
#include <string.h>

#include "gbsim.h"

#define ES1_MSG_SIZE	(4 * 1024)

/* Receive buffer for all data arriving from the AP */
static char cport_rbuf[ES1_MSG_SIZE];

static struct gbsim_cport *cport_find(uint16_t cport_id)
{
	struct gbsim_cport *cport;

	TAILQ_FOREACH(cport, &info.cports, cnode)
		if (cport->id == cport_id)
			return cport;

	return NULL;
}

static char *get_protocol(uint16_t cport_id)
{
	struct gbsim_cport *cport;

	cport = cport_find(cport_id);
	if (!cport)
		return "N/A";

	switch (cport->protocol) {
	case GREYBUS_PROTOCOL_GPIO:
		return "GPIO";
	case GREYBUS_PROTOCOL_I2C:
		return "I2C";
	case GREYBUS_PROTOCOL_PWM:
		return "PWM";
	case GREYBUS_PROTOCOL_I2S_MGMT:
		return "I2S_MGMT";
	case GREYBUS_PROTOCOL_I2S_RECEIVER:
		return "I2S_RECEIVER";
	case GREYBUS_PROTOCOL_I2S_TRANSMITTER:
		return "I2S_TRANSMITTER";
	default:
		return "(Unknown protocol>";
	}
}

static void cport_recv_handler(struct gbsim_cport *cport,
				void *rbuf, size_t size)
{
	switch (cport->protocol) {
	case GREYBUS_PROTOCOL_GPIO:
		gpio_handler(cport->id, rbuf, size);
		break;
	case GREYBUS_PROTOCOL_I2C:
		i2c_handler(cport->id, rbuf, size);
		break;
	case GREYBUS_PROTOCOL_PWM:
		pwm_handler(cport->id, rbuf, size);
		break;
	case GREYBUS_PROTOCOL_I2S_MGMT:
		i2s_mgmt_handler(cport->id, rbuf, size);
		break;
	case GREYBUS_PROTOCOL_I2S_RECEIVER:
	case GREYBUS_PROTOCOL_I2S_TRANSMITTER:
		i2s_data_handler(cport->id, rbuf, size);
		break;
	default:
		gbsim_error("handler not found for cport %u\n", cport->id);
	}
}

static void recv_handler(void *rbuf, size_t size)
{
	struct op_header *hdr = rbuf;
	uint16_t cport_id;
	struct gbsim_cport *cport;

	if (size < sizeof(*hdr)) {
		gbsim_error("short message received\n");
		return;
	}

	/*
	 * Retreive and clear the cport id stored in the header pad bytes.
	 */
	cport_id = hdr->pad[1] << 8 | hdr->pad[0];
	hdr->pad[0] = 0;
	hdr->pad[1] = 0;

	cport = cport_find(cport_id);
	if (!cport) {
		gbsim_error("message received for unknown cport id %u\n",
			cport_id);
		return;
	}

	/* FIXME: can identify module from our cport connection */
	gbsim_debug("AP -> Module %hhu CPort %hu %s request\n  ",
		    cport_to_module_id(cport_id), cport_id,
		    get_protocol(cport_id));

	if (verbose)
		gbsim_dump(rbuf, size);

	cport_recv_handler(cport, rbuf, size);
}

void recv_thread_cleanup(void *arg)
{
	cleanup_endpoint(svc_int, "svc_int");
	cleanup_endpoint(to_ap, "to_ap");
	cleanup_endpoint(from_ap, "from_ap");
}

/*
 * Repeatedly perform blocking reads to receive messages arriving
 * from the AP.
 */
void *recv_thread(void *param)
{
	while (1) {
		ssize_t size;

		size = read(from_ap, cport_rbuf, ES1_MSG_SIZE);
		if (size < 0) {
			gbsim_error("error %zd receiving from AP\n", size);
			return NULL;
		}

		recv_handler(cport_rbuf, size);
		memset(cport_rbuf, 0, sizeof(cport_rbuf));
	}
}
