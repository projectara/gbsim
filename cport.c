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
#include <errno.h>

#include "gbsim.h"

#define ES1_MSG_SIZE	(4 * 1024)

/* Receive buffer for all data arriving from the AP */
static char cport_rbuf[ES1_MSG_SIZE];
static char cport_tbuf[ES1_MSG_SIZE];

static struct gbsim_cport *cport_find(uint16_t cport_id)
{
	struct gbsim_cport *cport;

	TAILQ_FOREACH(cport, &info.cports, cnode)
		if (cport->hd_cport_id == cport_id)
			return cport;

	return NULL;
}

void allocate_cport(uint16_t cport_id, uint16_t hd_cport_id, int protocol_id)
{
	struct gbsim_cport *cport;

	cport = malloc(sizeof(*cport));
	cport->id = cport_id;

	cport->hd_cport_id = hd_cport_id;
	cport->protocol = protocol_id;
	TAILQ_INSERT_TAIL(&info.cports, cport, cnode);
}

static char *get_protocol(uint16_t cport_id)
{
	struct gbsim_cport *cport;

	cport = cport_find(cport_id);
	if (!cport)
		return "N/A";

	switch (cport->protocol) {
	case GREYBUS_PROTOCOL_CONTROL:
		return "CONTROL";
	case GREYBUS_PROTOCOL_GPIO:
		return "GPIO";
	case GREYBUS_PROTOCOL_I2C:
		return "I2C";
	case GREYBUS_PROTOCOL_UART:
		return "UART";
	case GREYBUS_PROTOCOL_LOOPBACK:
		return "LOOPBACK";
	case GREYBUS_PROTOCOL_PWM:
		return "PWM";
	case GREYBUS_PROTOCOL_SDIO:
		return "SDIO";
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

static int cport_recv_handler(struct gbsim_cport *cport,
				void *rbuf, size_t rsize,
				void *tbuf, size_t tsize)
{
	switch (cport->protocol) {
	case GREYBUS_PROTOCOL_CONTROL:
		return control_handler(cport->id, cport->hd_cport_id, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_GPIO:
		return gpio_handler(cport->id, cport->hd_cport_id, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_I2C:
		return i2c_handler(cport->id, cport->hd_cport_id, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_UART:
		return uart_handler(cport->id, cport->hd_cport_id, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_PWM:
		return pwm_handler(cport->id, cport->hd_cport_id, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_SDIO:
		return sdio_handler(cport->id, cport->hd_cport_id, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_I2S_MGMT:
		return i2s_mgmt_handler(cport->id, cport->hd_cport_id, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_I2S_RECEIVER:
	case GREYBUS_PROTOCOL_I2S_TRANSMITTER:
		return i2s_data_handler(cport->id, cport->hd_cport_id, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_LOOPBACK:
		return loopback_handler(cport->id, cport->hd_cport_id, rbuf, rsize, tbuf, tsize);
	default:
		gbsim_error("handler not found for cport %u\n", cport->id);
		return -EINVAL;
	}
}

static void recv_handler(void *rbuf, size_t rsize, void *tbuf, size_t tsize)
{
	struct op_header *hdr = rbuf;
	uint16_t hd_cport_id;
	struct gbsim_cport *cport;
	int ret;

	if (rsize < sizeof(*hdr)) {
		gbsim_error("short message received\n");
		return;
	}

	if (verbose)
		gbsim_dump(rbuf, rsize);

	/*
	 * Retreive and clear the cport id stored in the header pad bytes.
	 */
	hd_cport_id = hdr->pad[1] << 8 | hdr->pad[0];
	hdr->pad[0] = 0;
	hdr->pad[1] = 0;

	cport = cport_find(hd_cport_id);
	if (!cport) {
		gbsim_error("message received for unknown cport id %u\n",
			hd_cport_id);
		return;
	}

	/* FIXME: can identify module from our cport connection */
	gbsim_debug("AP -> Module %hhu CPort %hu %s request\n  ",
		    cport_to_module_id(hd_cport_id), cport->id,
		    get_protocol(hd_cport_id));

	ret = cport_recv_handler(cport, rbuf, rsize, tbuf, tsize);
	if (ret)
		gbsim_debug("cport_recv_handler() returned %d\n", ret);
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
		ssize_t rsize;

		rsize = read(from_ap, cport_rbuf, ES1_MSG_SIZE);
		if (rsize < 0) {
			gbsim_error("error %zd receiving from AP\n", rsize);
			return NULL;
		}

		recv_handler(cport_rbuf, rsize, cport_tbuf, sizeof(cport_tbuf));

		memset(cport_rbuf, 0, sizeof(cport_rbuf));
		memset(cport_tbuf, 0, sizeof(cport_tbuf));
	}
}
