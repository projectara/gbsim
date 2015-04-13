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

static char *get_protocol(unsigned int id)
{
	struct gbsim_cport *cport;

	TAILQ_FOREACH(cport, &info.cports, cnode) {
		if (cport->id == id) {
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
			}
		}
	}
	return "N/A";
}

static void exec_subdev_handler(unsigned int id, void *rbuf, size_t size)
{
	struct gbsim_cport *cport;

	TAILQ_FOREACH(cport, &info.cports, cnode) {
		if (cport->id == id)
			switch (cport->protocol) {
			case GREYBUS_PROTOCOL_GPIO:
				gpio_handler(id, rbuf, size);
				break;
			case GREYBUS_PROTOCOL_I2C:
				i2c_handler(id, rbuf, size);
				break;
			case GREYBUS_PROTOCOL_PWM:
				pwm_handler(id, rbuf, size);
				break;
			case GREYBUS_PROTOCOL_I2S_MGMT:
				i2s_mgmt_handler(id, rbuf, size);
				break;
			case GREYBUS_PROTOCOL_I2S_RECEIVER:
			case GREYBUS_PROTOCOL_I2S_TRANSMITTER:
				i2s_data_handler(id, rbuf, size);
				break;
			default:
				gbsim_error("subdev handler not found for cport %d\n",
					     id);
			}
	}
}

static void cport_handler(void *rbuf, size_t size)
{
	struct op_header *hdr = rbuf;
	unsigned int id;

	if (size < sizeof(*hdr)) {
		gbsim_error("short message received\n");
		return;
	}

	/*
	 * Retreive and clear the cport id stored in the header pad bytes.
	 */
	id = hdr->pad[1] << 8 | hdr->pad[0];
	hdr->pad[0] = 0;
	hdr->pad[1] = 0;

	/* FIXME: can identify module from our cport connection */
	gbsim_debug("AP -> Module %d CPort %d %s request\n  ",
		    cport_to_module_id(id),
		    id,
		    get_protocol(id));

	if (verbose)
		gbsim_dump(rbuf, size);

	exec_subdev_handler(id, rbuf, size);
}

void cport_thread_cleanup(void *arg)
{
	cleanup_endpoint(svc_int, "svc_int");
	cleanup_endpoint(to_ap, "to_ap");
	cleanup_endpoint(from_ap, "from_ap");
}

/*
 * Repeatedly perform blocking reads to receive messages arriving
 * from the AP.
 */
void *cport_thread(void *param)
{
	while (1) {
		ssize_t size;

		size = read(from_ap, cport_rbuf, ES1_MSG_SIZE);
		if (size < 0) {
			gbsim_error("error %zd receiving from AP\n", size);
			return NULL;
		}

		cport_handler(cport_rbuf, size);
		memset(cport_rbuf, 0, sizeof(cport_rbuf));
	}
}
