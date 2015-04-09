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

#include <greybus_manifest.h>

#include "gbsim.h"

#define ES1_MSG_SIZE	(4 * 1024)

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

static void exec_subdev_handler(unsigned int id, __u8 *rbuf, size_t size)
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

void cport_handler(__u8 *rbuf, size_t size)
{
	struct op_header *hdr = (void *)rbuf;
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

	free(rbuf);
}

void cport_thread_cleanup(void *arg)
{
	cleanup_endpoint(svc_int, "svc_int");
	cleanup_endpoint(cport_in, "cport_in");
	cleanup_endpoint(cport_out, "cport_out");
}

void *cport_thread(void *param)
{
	size_t size;
	__u8 *rbuf;

	do {
		rbuf = malloc(ES1_MSG_SIZE);
		if (!rbuf) {
			gbsim_error("failed to allocate receive buffer\n");
			return NULL;
		}
		/* blocking read for our max buf size */
		size = read(cport_out, rbuf, ES1_MSG_SIZE);
		if (size < 0) {
			gbsim_error("failed to read from CPort OUT endpoint\n");
			return NULL;
		}

		cport_handler(rbuf, size);
	} while (1);
}
