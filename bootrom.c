/*
 * Greybus Simulator: BOOTROM CPort protocol
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <fcntl.h>
#include <pthread.h>
#include <linux/fs.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "gbsim.h"

static char *firmware_file = "ara_firmware.fw";
static int firmware_size;
static int firmware_read_size;
static int firmware_fetch_size;
static int firmware_fd;

char *bootrom_get_operation(uint8_t type)
{
	switch (type) {
	case GB_REQUEST_TYPE_INVALID:
		return "GB_BOOTROM_TYPE_INVALID";
	case GB_BOOTROM_TYPE_VERSION:
		return "GB_BOOTROM_TYPE_PROTOCOL_VERSION";
	case GB_BOOTROM_TYPE_FIRMWARE_SIZE:
		return "GB_BOOTROM_TYPE_FIRMWARE_SIZE";
	case GB_BOOTROM_TYPE_GET_FIRMWARE:
		return "GB_BOOTROM_TYPE_GET_FIRMWARE";
	case GB_BOOTROM_TYPE_READY_TO_BOOT:
		return "GB_BOOTROM_TYPE_READY_TO_BOOT";
	default:
		return "(Unknown operation)";
	}
}

/* Request from Module to AP */
int bootrom_request_send(uint8_t type, uint16_t hd_cport_id)
{
	struct op_msg msg = { };
	struct gb_operation_msg_hdr *oph = &msg.header;
	struct gb_bootrom_firmware_size_request *size_request;
	struct gb_bootrom_get_firmware_request *get_fw_request;
	struct gb_bootrom_ready_to_boot_request *rbt_request;
	uint16_t message_size = sizeof(*oph);
	size_t payload_size;

	switch (type) {
	case GB_BOOTROM_TYPE_FIRMWARE_SIZE:
		payload_size = sizeof(*size_request);
		size_request = &msg.fw_size_req;
		size_request->stage = GB_BOOTROM_BOOT_STAGE_ONE;
		break;
	case GB_BOOTROM_TYPE_GET_FIRMWARE:
		payload_size = sizeof(*get_fw_request);
		get_fw_request = &msg.fw_get_firmware_req;

		/* Calculate fetch size for remaining data */
		firmware_fetch_size = firmware_size - firmware_read_size;
		if (firmware_fetch_size > GB_BOOTROM_FETCH_MAX)
			firmware_fetch_size = GB_BOOTROM_FETCH_MAX;

		get_fw_request->offset = htole32(firmware_read_size);
		get_fw_request->size = htole32(firmware_fetch_size);
		break;
	case GB_BOOTROM_TYPE_READY_TO_BOOT:
		payload_size = sizeof(*rbt_request);
		rbt_request = &msg.fw_rbt_req;

		rbt_request->status = GB_BOOTROM_BOOT_STATUS_SECURE;
		break;
	default:
		gbsim_error("firmware operation type %02x not supported\n",
			    type);
		return -EINVAL;
	}

	message_size += payload_size;
	return send_request(hd_cport_id, &msg, message_size, 1, type);
}

/* Request from AP to Module */
static int bootrom_handler_request(uint16_t cport_id, uint16_t hd_cport_id,
			       void *rbuf, size_t rsize, void *tbuf,
			       size_t tsize)
{
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp = tbuf;
	struct gb_operation_msg_hdr *oph = &op_req->header;
	struct gb_bootrom_version_response *version_response;
	uint16_t message_size = sizeof(*oph);
	size_t payload_size;
	int ret;

	switch (oph->type) {
	case GB_BOOTROM_TYPE_VERSION:
		payload_size = sizeof(*version_response);

		version_response = &op_rsp->fw_version_response;
		version_response->major = GB_BOOTROM_VERSION_MAJOR;
		version_response->minor = GB_BOOTROM_VERSION_MINOR;

		gbsim_debug("AP Bootrom version (%d %d)\n",
			    version_response->major, version_response->minor);

		break;
	default:
		gbsim_error("%s: Request not supported (%d)\n", __func__,
			    oph->type);
		return -EINVAL;
	}

	message_size += payload_size;
	ret = send_response(hd_cport_id, op_rsp, message_size,
				oph->operation_id, oph->type,
				PROTOCOL_STATUS_SUCCESS);
	if (ret)
		return ret;

	ret = bootrom_request_send(GB_BOOTROM_TYPE_FIRMWARE_SIZE, hd_cport_id);
	if (ret)
		gbsim_error("%s: Failed to get size (%d)\n", __func__, ret);

	return ret;
}

static int fetch_bootrom(uint16_t hd_cport_id)
{
	int ret;

	ret = bootrom_request_send(GB_BOOTROM_TYPE_GET_FIRMWARE,
				    hd_cport_id);
	if (ret)
		gbsim_error("%s: Failed to get firmware (%d)\n", __func__, ret);

	return ret;
}

static int dump_bootrom(uint8_t *data)
{
	int ret;

	if (!firmware_fd) {
		firmware_fd = open(firmware_file, O_WRONLY);
		if (firmware_fd < 0) {
			gbsim_debug("%s: Failed to open file %s\n", __func__,
				    firmware_file);
			return firmware_fd;
		}
	}

	/* dump data */
	ret = write(firmware_fd, data, firmware_fetch_size);
	gbsim_debug("%s: Dumped %d bytes of data\n", __func__, ret);

	firmware_read_size += firmware_fetch_size;
	if (firmware_read_size == firmware_size) {
		close(firmware_fd);
		firmware_fd = 0;
		firmware_size = 0;
		firmware_read_size = 0;
		firmware_fetch_size = 0;
	}

	return 0;
}

/* Response from AP to Module, in response to a request Module has sent earlier */
static int bootrom_handler_response(uint16_t cport_id, uint16_t hd_cport_id,
				void *rbuf, size_t rsize)
{
	struct op_msg *op_rsp = rbuf;
	struct gb_operation_msg_hdr *oph = &op_rsp->header;
	struct gb_bootrom_firmware_size_response *size_response;
	struct gb_bootrom_get_firmware_response *get_fw_response;
	int ret = 0;
	int type = oph->type & ~OP_RESPONSE;

	/* Did the request fail? */
	if (oph->result) {
		gbsim_error("%s: Operation type: %s FAILED (%d)\n", __func__,
			    bootrom_get_operation(type), oph->result);
		return oph->result;
	}

	switch (type) {
	case GB_BOOTROM_TYPE_FIRMWARE_SIZE:
		size_response = &op_rsp->fw_size_resp;
		firmware_size = le32toh(size_response->size);

		gbsim_debug("%s: Firmware size returned is %d bytes\n",
			    __func__, firmware_size);

		ret = fetch_bootrom(hd_cport_id);
		break;
	case GB_BOOTROM_TYPE_GET_FIRMWARE:
		get_fw_response = &op_rsp->fw_get_firmware_resp;
		ret = dump_bootrom(get_fw_response->data);
		if (ret)
			break;

		if (firmware_read_size < firmware_size) {
			ret = fetch_bootrom(hd_cport_id);
			break;
		}

		ret = bootrom_request_send(GB_BOOTROM_TYPE_READY_TO_BOOT,
					    hd_cport_id);
		if (ret)
			gbsim_error("%s: Failed to send ready to boot message(%d)\n",
				    __func__, ret);
		break;
	case GB_BOOTROM_TYPE_READY_TO_BOOT:
		gbsim_debug("%s: AP granted permission to boot.\n", __func__);
		break;
	default:
		gbsim_error("%s: Response not supported (%d)\n", __func__,
			    type);
		return -EINVAL;
	}

	return ret;
}

int bootrom_handler(struct gbsim_connection *connection, void *rbuf,
		    size_t rsize, void *tbuf, size_t tsize)
{
	struct op_msg *op = rbuf;
	struct gb_operation_msg_hdr *oph = &op->header;
	uint16_t cport_id = connection->cport_id;
	uint16_t hd_cport_id = connection->hd_cport_id;

	if (oph->type & OP_RESPONSE)
		return bootrom_handler_response(cport_id, hd_cport_id, rbuf, rsize);
	else
		return bootrom_handler_request(cport_id, hd_cport_id, rbuf, rsize,
					   tbuf, tsize);
}
