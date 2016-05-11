/*
 * Greybus Simulator: FIRMWARE Download CPort protocol
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

#define GB_FIRMWARE_FETCH_MAX	2000

static void (*download_callback)(void);

static char *firmware_file = "ara_firmware.fw";
static int firmware_size;
static int firmware_read_size;
static int firmware_fetch_size;
static int firmware_fd;

char *fw_download_get_operation(uint8_t type)
{
	switch (type) {
	case GB_FW_DOWNLOAD_TYPE_FIND_FIRMWARE:
		return "GB_FW_DOWNLOAD_TYPE_FIND_FIRMWARE";
	case GB_FW_DOWNLOAD_TYPE_FETCH_FIRMWARE:
		return "GB_FW_DOWNLOAD_TYPE_FETCH_FIRMWARE";
	case GB_FW_DOWNLOAD_TYPE_RELEASE_FIRMWARE:
		return "GB_FW_DOWNLOAD_TYPE_RELEASE_FIRMWARE";
	default:
		return "(Unknown operation)";
	}
}

/* Request from Module to AP */
int fw_download_request_send(uint8_t type, uint16_t hd_cport_id, char *tag,
			uint8_t firmware_id)
{
	struct op_msg msg = { };
	struct gb_operation_msg_hdr *oph = &msg.header;
	struct gb_fw_download_find_firmware_request *fw_download_find_req;
	struct gb_fw_download_fetch_firmware_request *fw_download_fetch_req;
	struct gb_fw_download_release_firmware_request *fw_download_release_req;
	uint16_t message_size = sizeof(*oph);
	size_t payload_size;

	switch (type) {
	case GB_FW_DOWNLOAD_TYPE_FIND_FIRMWARE:
		payload_size = sizeof(*fw_download_find_req);
		fw_download_find_req = &msg.fw_download_find_req;

		strcpy((char *)&fw_download_find_req->firmware_tag, tag);
		break;
	case GB_FW_DOWNLOAD_TYPE_FETCH_FIRMWARE:
		payload_size = sizeof(*fw_download_fetch_req);
		fw_download_fetch_req = &msg.fw_download_fetch_req;

		/* Calculate fetch size for remaining data */
		firmware_fetch_size = firmware_size - firmware_read_size;
		if (firmware_fetch_size > GB_FIRMWARE_FETCH_MAX)
			firmware_fetch_size = GB_FIRMWARE_FETCH_MAX;

		fw_download_fetch_req->offset = htole32(firmware_read_size);
		fw_download_fetch_req->size = htole32(firmware_fetch_size);
		fw_download_fetch_req->firmware_id = firmware_id;
		break;
	case GB_FW_DOWNLOAD_TYPE_RELEASE_FIRMWARE:
		payload_size = sizeof(*fw_download_release_req);
		fw_download_release_req = &msg.fw_download_release_req;

		fw_download_release_req->firmware_id = firmware_id;
		break;
	default:
		gbsim_error("firmware operation type %02x not supported\n",
			    type);
		return -EINVAL;
	}

	message_size += payload_size;
	return send_request(hd_cport_id, &msg, message_size, 1, type);
}

int download_firmware(char *tag, uint16_t hd_cport_id, void (*func)(void))
{
	int ret;

	download_callback = func;

	ret = fw_download_request_send(GB_FW_DOWNLOAD_TYPE_FIND_FIRMWARE,
					hd_cport_id, tag, 0);
	if (ret) {
		download_callback = func;
		gbsim_error("%s: Failed to find firmware (%d)\n", __func__, ret);
	}

	return ret;
}

static int fetch_firmware(uint16_t hd_cport_id, uint8_t firmware_id)
{
	int ret;

	ret = fw_download_request_send(GB_FW_DOWNLOAD_TYPE_FETCH_FIRMWARE,
				    hd_cport_id, NULL, firmware_id);
	if (ret)
		gbsim_error("%s: Failed to get firmware (%d)\n", __func__, ret);

	return ret;
}

static int dump_firmware(uint8_t *data)
{
	int ret;

	if (!firmware_fd) {
		firmware_fd = open(firmware_file, O_WRONLY);
		if (firmware_fd < 0) {
			gbsim_debug("%s: Failed to open file %s\n", __func__,
				    firmware_file);
		}
	}

	/* dump data */
	if (firmware_fd > 0) {
		ret = write(firmware_fd, data, firmware_fetch_size);
		gbsim_debug("%s: Dumped %d bytes of data\n", __func__, ret);
	}

	firmware_read_size += firmware_fetch_size;
	if (firmware_read_size == firmware_size) {
		if (firmware_fd > 0)
			close(firmware_fd);
		firmware_fd = 0;
		firmware_size = 0;
		firmware_read_size = 0;
		firmware_fetch_size = 0;
	}

	return 0;
}

/* Response from AP to Module, in response to a request Module has sent earlier */
static int fw_download_handler_response(uint16_t cport_id, uint16_t hd_cport_id,
				void *rbuf, size_t rsize)
{
	struct op_msg *op_rsp = rbuf;
	struct gb_operation_msg_hdr *oph = &op_rsp->header;
	struct gb_fw_download_find_firmware_response *fw_download_find_rsp;
	struct gb_fw_download_fetch_firmware_response *fw_download_fetch_rsp;
	int ret = 0;
	static uint8_t firmware_id;
	int type = oph->type & ~OP_RESPONSE;

	/* Did the request fail? */
	if (oph->result) {
		gbsim_error("%s: Operation type: %s FAILED (%d)\n", __func__,
			    fw_download_get_operation(type), oph->result);
		return oph->result;
	}

	switch (type) {
	case GB_FW_DOWNLOAD_TYPE_FIND_FIRMWARE:
		fw_download_find_rsp = &op_rsp->fw_download_find_rsp;
		firmware_id = fw_download_find_rsp->firmware_id;
		firmware_size = le32toh(fw_download_find_rsp->size);

		gbsim_debug("%s: Firmware size returned is %d bytes, id: %d\n",
			    __func__, firmware_size, firmware_id);

		ret = fetch_firmware(hd_cport_id, firmware_id);
		break;
	case GB_FW_DOWNLOAD_TYPE_FETCH_FIRMWARE:
		fw_download_fetch_rsp = &op_rsp->fw_download_fetch_rsp;
		ret = dump_firmware(fw_download_fetch_rsp->data);
		if (ret)
			break;

		if (firmware_read_size < firmware_size) {
			ret = fetch_firmware(hd_cport_id, firmware_id);
			break;
		}

		ret = fw_download_request_send(GB_FW_DOWNLOAD_TYPE_RELEASE_FIRMWARE,
					hd_cport_id, NULL, firmware_id);
		if (ret)
			gbsim_error("%s: Failed to release firmware with id: %d (%d)\n",
				    __func__, firmware_id, ret);
		break;
	case GB_FW_DOWNLOAD_TYPE_RELEASE_FIRMWARE:
		gbsim_debug("%s: AP released firmware\n", __func__);
		if (download_callback)
			download_callback();
		else
			gbsim_debug("%s: No callback to call\n", __func__);
		break;
	default:
		gbsim_error("%s: Response not supported (%d)\n", __func__,
			    type);
		return -EINVAL;
	}

	return ret;
}

int fw_download_handler(struct gbsim_connection *connection, void *rbuf,
		    size_t rsize, void *tbuf, size_t tsize)
{
	struct op_msg *op = rbuf;
	struct gb_operation_msg_hdr *oph = &op->header;
	uint16_t cport_id = connection->cport_id;
	uint16_t hd_cport_id = connection->hd_cport_id;

	if (oph->type & OP_RESPONSE)
		return fw_download_handler_response(cport_id, hd_cport_id, rbuf, rsize);
	else
		return -EINVAL;
}
