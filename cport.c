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

#define ES1_MSG_SIZE	(2 * 1024)

/* Receive buffer for all data arriving from the AP */
static char cport_rbuf[ES1_MSG_SIZE];
static char cport_tbuf[ES1_MSG_SIZE];

/*
 * We (ab)use the operation-message header pad bytes to transfer the
 * cport id in order to minimise overhead.
 */
static void
gbsim_message_cport_pack(struct gb_operation_msg_hdr *header, uint16_t cport_id)
{
	header->pad[0] = cport_id;
}

/* Clear the pad bytes used for the CPort id */
static void gbsim_message_cport_clear(struct gb_operation_msg_hdr *header)
{
	header->pad[0] = 0;
}

/* Extract the CPort id packed into the header, and clear it */
static uint16_t gbsim_message_cport_unpack(struct gb_operation_msg_hdr *header)
{
	return (uint16_t)header->pad[0];
}

struct gbsim_cport *cport_find(uint16_t cport_id)
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

void free_cport(struct gbsim_cport *cport)
{
	TAILQ_REMOVE(&info.cports, cport, cnode);
	free(cport);
}

void free_cports(void)
{
	struct gbsim_cport *cport;

	/*
	 * Linux doesn't have a foreach_safe version of tailq and so the dirty
	 * trick of 'goto again'.
	 */
again:
	TAILQ_FOREACH(cport, &info.cports, cnode) {
		if (cport->hd_cport_id == GB_SVC_CPORT_ID)
			continue;

		free_cport(cport);
		goto again;
	}
	reset_hd_cport_id();
}

static void get_protocol_operation(uint16_t cport_id, char **protocol,
				   char **operation, uint8_t type)
{
	struct gbsim_cport *cport;

	cport = cport_find(cport_id);
	if (!cport) {
		*protocol = "N/A";
		*operation = "N/A";
		return;
	}

	switch (cport->protocol) {
	case GREYBUS_PROTOCOL_CONTROL:
		*protocol = "CONTROL";
		*operation = control_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_SVC:
		*protocol = "SVC";
		*operation = svc_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_GPIO:
		*protocol = "GPIO";
		*operation = gpio_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_I2C:
		*protocol = "I2C";
		*operation = i2c_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_UART:
		*protocol = "UART";
		*operation = uart_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_LOOPBACK:
		*protocol = "LOOPBACK";
		*operation = loopback_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_PWM:
		*protocol = "PWM";
		*operation = pwm_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_SDIO:
		*protocol = "SDIO";
		*operation = sdio_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_LIGHTS:
		*protocol = "LIGHTS";
		*operation = lights_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_I2S_MGMT:
		*protocol = "I2S_MGMT";
		*operation = i2s_mgmt_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_I2S_RECEIVER:
		*protocol = "I2S_RECEIVER";
		*operation = i2s_data_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_I2S_TRANSMITTER:
		*protocol = "I2S_TRANSMITTER";
		*operation = i2s_data_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_FIRMWARE:
		*protocol = "FIRMWARE";
		*operation = firmware_get_operation(type);
		break;
	default:
		*protocol = "(Unknown protocol)";
		*operation = "(Unknown operation)";
		break;
	}
}

static int send_msg_to_ap(uint16_t hd_cport_id,
			struct op_msg *message, uint16_t message_size,
			uint16_t operation_id, uint8_t type, uint8_t result)
{
	struct gb_operation_msg_hdr *header = &message->header;
	char *protocol, *operation;
	ssize_t nbytes;

	header->size = htole16(message_size);
	header->operation_id = operation_id;
	header->type = type;
	header->result = result;

	gbsim_message_cport_pack(header, hd_cport_id);

	get_protocol_operation(hd_cport_id, &protocol, &operation,
			       type & ~OP_RESPONSE);
	if (type & OP_RESPONSE)
		gbsim_debug("Module -> AP CPort %hu %s %s response\n",
			    hd_cport_id, protocol, operation);
	else
		gbsim_debug("Module -> AP CPort %hu %s %s request\n",
			    hd_cport_id, protocol, operation);

	/* Send the response to the AP */
	if (verbose)
		gbsim_dump(message, message_size);

	nbytes = write(to_ap, message, message_size);
	if (nbytes < 0)
		return nbytes;

	return 0;
}

int send_response(uint16_t hd_cport_id,
			struct op_msg *message, uint16_t message_size,
			uint16_t operation_id, uint8_t type, uint8_t result)
{
	return send_msg_to_ap(hd_cport_id, message, message_size,
				operation_id, type | OP_RESPONSE, result);
}

int send_request(uint16_t hd_cport_id,
			struct op_msg *message, uint16_t message_size,
			uint16_t operation_id, uint8_t type)
{
	return send_msg_to_ap(hd_cport_id, message, message_size,
				operation_id, type, 0);
}

static int cport_recv_handler(struct gbsim_cport *cport,
				void *rbuf, size_t rsize)
{
	void *tbuf = &cport_tbuf[0];
	size_t tsize = sizeof(cport_tbuf);

	memset(tbuf, 0, tsize);	/* Zero buffer before use */

	switch (cport->protocol) {
	case GREYBUS_PROTOCOL_CONTROL:
		return control_handler(cport, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_SVC:
		return svc_handler(cport, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_GPIO:
		return gpio_handler(cport, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_I2C:
		return i2c_handler(cport, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_UART:
		return uart_handler(cport, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_PWM:
		return pwm_handler(cport, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_SDIO:
		return sdio_handler(cport, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_LIGHTS:
		return lights_handler(cport, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_I2S_MGMT:
		return i2s_mgmt_handler(cport, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_I2S_RECEIVER:
	case GREYBUS_PROTOCOL_I2S_TRANSMITTER:
		return i2s_data_handler(cport, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_LOOPBACK:
		return loopback_handler(cport, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_FIRMWARE:
		return firmware_handler(cport, rbuf, rsize, tbuf, tsize);
	default:
		gbsim_error("handler not found for cport %u\n", cport->id);
		return -EINVAL;
	}
}

static void recv_handler(void *rbuf, size_t rsize)
{
	struct gb_operation_msg_hdr *hdr = rbuf;
	uint16_t hd_cport_id;
	struct gbsim_cport *cport;
	char *protocol, *operation, *type;
	int ret;

	if (rsize < sizeof(*hdr)) {
		gbsim_error("short message received\n");
		return;
	}

	/* Retreive the cport id stored in the header pad bytes */
	hd_cport_id = gbsim_message_cport_unpack(hdr);

	cport = cport_find(hd_cport_id);
	if (!cport) {
		gbsim_error("message received for unknown cport id %u\n",
			hd_cport_id);
		return;
	}

	type = hdr->type & OP_RESPONSE ? "response" : "request";
	get_protocol_operation(hd_cport_id, &protocol, &operation,
			       hdr->type & ~OP_RESPONSE);

	/* FIXME: can identify module from our cport connection */
	gbsim_debug("AP -> Module %hhu CPort %hu %s %s %s\n",
		    cport_to_module_id(hd_cport_id), cport->id,
		    protocol, operation, type);

	if (verbose)
		gbsim_dump(rbuf, rsize);

	gbsim_message_cport_clear(hdr);

	ret = cport_recv_handler(cport, rbuf, rsize);
	if (ret)
		gbsim_debug("cport_recv_handler() returned %d\n", ret);
}

void recv_thread_cleanup(void *arg)
{
	cleanup_endpoint(to_ap, "to_ap");
	cleanup_endpoint(from_ap, "from_ap");
}

/*
 * Repeatedly perform blocking reads to receive messages arriving
 * from the AP.
 */
void *recv_thread(void *param)
{
	void *rbuf = &cport_rbuf[0];
	size_t rbuf_size = sizeof(cport_rbuf);

	while (1) {
		ssize_t rsize;

		memset(rbuf, 0, rbuf_size);	/* Zero buffer before use */

		rsize = read(from_ap, rbuf, rbuf_size);
		if (rsize < 0) {
			gbsim_error("error %zd receiving from AP\n", rsize);
			return NULL;
		}

		recv_handler(rbuf, rsize);
	}
}
