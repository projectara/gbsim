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
#include <sys/queue.h>
#include <errno.h>

#include "gbsim.h"
#include "gbsim_usb.h"

#define ES1_MSG_SIZE	(2 * 1024)

/* Receive buffer for all data arriving from the AP */
static char cport_rbuf[ES1_MSG_SIZE];
static char cport_tbuf[ES1_MSG_SIZE];

extern struct gbsim_svc *svc;

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

struct gbsim_connection *connection_find(uint16_t cport_id)
{
	struct gbsim_connection *connection;
	struct gbsim_interface *intf;

	TAILQ_FOREACH(intf, &svc->intfs, intf_node)
		TAILQ_FOREACH(connection, &intf->connections, cnode)
			if (connection->hd_cport_id == cport_id)
				return connection;

	return NULL;
}

uint16_t find_hd_cport_for_protocol(int protocol_id)
{
	struct gbsim_connection *connection;
	struct gbsim_interface *intf;

	TAILQ_FOREACH(intf, &svc->intfs, intf_node)
		TAILQ_FOREACH(connection, &intf->connections, cnode)
			if (connection->protocol == protocol_id)
				return connection->hd_cport_id;

	return 0;
}

void connection_set_protocol(struct gbsim_connection *connection,
			     uint16_t cport_id)
{
	int protocol_id;

	protocol_id = cport_get_protocol(connection->intf, cport_id);
	if (protocol_id < 0)
		gbsim_error("fail to get protocol to cport_id: %u\n", cport_id);

	connection->protocol = protocol_id;
}

struct gbsim_connection *allocate_connection(struct gbsim_interface *intf,
					     uint16_t cport_id,
					     uint16_t hd_cport_id)
{
	struct gbsim_connection *connection;

	connection = calloc(1, sizeof(*connection));
	if (!connection)
		return NULL;

	connection->cport_id = cport_id;

	connection->hd_cport_id = hd_cport_id;

	TAILQ_INSERT_TAIL(&intf->connections, connection, cnode);

	if (cport_id == GB_CONTROL_CPORT_ID)
		intf->control_conn = connection;

	connection->intf = intf;

	return connection;
}

void free_connection(struct gbsim_connection *connection)
{
	struct gbsim_interface *intf = connection->intf;

	TAILQ_REMOVE(&intf->connections, connection, cnode);
	free(connection);
}

static void get_protocol_operation(uint16_t cport_id, char **protocol,
				   char **operation, uint8_t type)
{
	struct gbsim_connection *connection;

	connection = connection_find(cport_id);
	if (!connection) {
		*protocol = "N/A";
		*operation = "N/A";
		return;
	}

	switch (connection->protocol) {
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
	case GREYBUS_PROTOCOL_SPI:
		*protocol = "SPI";
		*operation = spi_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_LIGHTS:
		*protocol = "LIGHTS";
		*operation = lights_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_POWER_SUPPLY:
		*protocol = "POWER_SUPPLY";
		*operation = power_supply_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_BOOTROM:
		*protocol = "BOOTROM";
		*operation = bootrom_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_FW_MANAGEMENT:
		*protocol = "fw-mgmt";
		*operation = fw_mgmt_get_operation(type);
		break;
	case GREYBUS_PROTOCOL_FW_DOWNLOAD:
		*protocol = "fw-download";
		*operation = fw_download_get_operation(type);
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

static int connection_recv_handler(struct gbsim_connection *connection,
				void *rbuf, size_t rsize)
{
	void *tbuf = &cport_tbuf[0];
	size_t tsize = sizeof(cport_tbuf);

	memset(tbuf, 0, tsize);	/* Zero buffer before use */

	switch (connection->protocol) {
	case GREYBUS_PROTOCOL_CONTROL:
		return control_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_SVC:
		return svc_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_GPIO:
		return gpio_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_I2C:
		return i2c_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_UART:
		return uart_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_PWM:
		return pwm_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_SDIO:
		return sdio_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_SPI:
		return spi_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_LIGHTS:
		return lights_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_POWER_SUPPLY:
		return power_supply_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_LOOPBACK:
		return loopback_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_BOOTROM:
		return bootrom_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_FW_MANAGEMENT:
		return fw_mgmt_handler(connection, rbuf, rsize, tbuf, tsize);
	case GREYBUS_PROTOCOL_FW_DOWNLOAD:
		return fw_download_handler(connection, rbuf, rsize, tbuf, tsize);
	default:
		gbsim_error("handler not found for cport %u\n",
				connection->cport_id);
		return -EINVAL;
	}
}

static void recv_handler(void *rbuf, size_t rsize)
{
	struct gb_operation_msg_hdr *hdr = rbuf;
	uint16_t hd_cport_id;
	struct gbsim_connection *connection;
	char *protocol, *operation, *type;
	int ret;

	if (rsize < sizeof(*hdr)) {
		gbsim_error("short message received\n");
		return;
	}

	/* Retreive the cport id stored in the header pad bytes */
	hd_cport_id = gbsim_message_cport_unpack(hdr);

	connection = connection_find(hd_cport_id);
	if (!connection) {
		gbsim_error("message received for unknown cport id %u\n",
			hd_cport_id);
		return;
	}

	type = hdr->type & OP_RESPONSE ? "response" : "request";
	get_protocol_operation(hd_cport_id, &protocol, &operation,
			       hdr->type & ~OP_RESPONSE);

	/* FIXME: can identify module from our cport connection */
	gbsim_debug("AP -> Module %hhu CPort %hu %s %s %s\n",
		    cport_to_module_id(hd_cport_id), connection->cport_id,
		    protocol, operation, type);

	if (verbose)
		gbsim_dump(rbuf, rsize);

	gbsim_message_cport_clear(hdr);

	ret = connection_recv_handler(connection, rbuf, rsize);
	if (ret)
		gbsim_debug("connection_recv_handler() returned %d\n", ret);
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
