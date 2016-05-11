/*
 * Greybus Simulator: Firmware Management protocol
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
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

char *fw_mgmt_get_operation(uint8_t type)
{
	switch (type) {
	case GB_FW_MGMT_TYPE_INTERFACE_FW_VERSION:
		return "GB_FW_MGMT_TYPE_INTERFACE_FW_VERSION";
	case GB_FW_MGMT_TYPE_LOAD_AND_VALIDATE_FW:
		return "GB_FW_MGMT_TYPE_LOAD_AND_VALIDATE_FW";
	case GB_FW_MGMT_TYPE_LOADED_FW:
		return "GB_FW_MGMT_TYPE_LOADED_FW";
	case GB_FW_MGMT_TYPE_BACKEND_FW_VERSION:
		return "GB_FW_MGMT_TYPE_BACKEND_FW_VERSION";
	case GB_FW_MGMT_TYPE_BACKEND_FW_UPDATE:
		return "GB_FW_MGMT_TYPE_BACKEND_FW_UPDATE";
	case GB_FW_MGMT_TYPE_BACKEND_FW_UPDATED:
		return "GB_FW_MGMT_TYPE_BACKEND_FW_UPDATED";
	default:
		return "(Unknown operation)";
	}
}

static uint8_t fw_down_type;
static uint16_t fw_mgmt_hd_cport_id;
static uint16_t fw_down_request_id;

/* Request from Module to AP */
int fw_mgmt_request_send(uint8_t type, uint16_t hd_cport_id, uint8_t request_id)
{
	struct op_msg msg = { };
	struct gb_operation_msg_hdr *oph = &msg.header;
	struct gb_fw_mgmt_loaded_fw_request *fw_mgmt_loaded_fw_req;
	struct gb_fw_mgmt_backend_fw_updated_request *fw_mgmt_backend_fw_updated_req;
	uint16_t message_size = sizeof(*oph);
	size_t payload_size;

	switch (type) {
	case GB_FW_MGMT_TYPE_LOADED_FW:
		payload_size = sizeof(*fw_mgmt_loaded_fw_req);
		fw_mgmt_loaded_fw_req = &msg.fw_mgmt_loaded_fw_req;

		fw_mgmt_loaded_fw_req->request_id = request_id;
		fw_mgmt_loaded_fw_req->status = GB_FW_LOAD_STATUS_VALIDATED;
		fw_mgmt_loaded_fw_req->major = htole16(2);
		fw_mgmt_loaded_fw_req->minor = htole16(1);
		break;
	case GB_FW_MGMT_TYPE_BACKEND_FW_UPDATED:
		payload_size = sizeof(*fw_mgmt_backend_fw_updated_req);
		fw_mgmt_backend_fw_updated_req = &msg.fw_mgmt_backend_fw_updated_req;

		fw_mgmt_backend_fw_updated_req->request_id = request_id;
		fw_mgmt_backend_fw_updated_req->status = GB_FW_BACKEND_FW_STATUS_SUCCESS;
		break;
	default:
		gbsim_error("firmware operation type %02x not supported\n",
			    type);
		return -EINVAL;
	}

	message_size += payload_size;
	return send_request(hd_cport_id, &msg, message_size, 1, type);
}

static void download_callback(void)
{
	gbsim_debug("Firmware Downloaded: (type=%u cport-id=%u request-id=%u)\n",
			fw_down_type, fw_mgmt_hd_cport_id, fw_down_request_id);
	fw_mgmt_request_send(fw_down_type, fw_mgmt_hd_cport_id, fw_down_request_id);
}

/* Request from AP to Module */
static int fw_mgmt_handler_request(uint16_t cport_id, uint16_t hd_cport_id,
			       void *rbuf, size_t rsize, void *tbuf,
			       size_t tsize)
{
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp = tbuf;
	struct gb_operation_msg_hdr *oph = &op_req->header;
	struct gb_fw_mgmt_interface_fw_version_response *fw_mgmt_intf_fw_version_rsp;
	struct gb_fw_mgmt_load_and_validate_fw_request *fw_mgmt_load_validate_fw_req = NULL;
	struct gb_fw_mgmt_backend_fw_version_request *fw_mgmt_backend_fw_ver_req;
	struct gb_fw_mgmt_backend_fw_version_response *fw_mgmt_backend_fw_ver_rsp;
	struct gb_fw_mgmt_backend_fw_update_request *fw_mgmt_backend_fw_update_req;
	uint16_t fw_download_hd_cport_id;
	uint16_t message_size = sizeof(*oph);
	size_t payload_size = 0;
	uint8_t request_id = 0;
	char *tag = NULL;
	int ret;

	switch (oph->type) {
	case GB_FW_MGMT_TYPE_INTERFACE_FW_VERSION:
		fw_mgmt_intf_fw_version_rsp = &op_rsp->fw_mgmt_intf_fw_version_rsp;
		payload_size = sizeof(*fw_mgmt_intf_fw_version_rsp);

		strcpy((char *)fw_mgmt_intf_fw_version_rsp->firmware_tag, "s2loader");
		fw_mgmt_intf_fw_version_rsp->major = htole16(2);
		fw_mgmt_intf_fw_version_rsp->minor = htole16(1);
		break;
	case GB_FW_MGMT_TYPE_LOAD_AND_VALIDATE_FW:
		fw_mgmt_load_validate_fw_req = &op_req->fw_mgmt_load_validate_fw_req;

		gbsim_debug("AP fw load (method=%u id=%u tag=%s)\n",
			    fw_mgmt_load_validate_fw_req->load_method,
			    fw_mgmt_load_validate_fw_req->request_id,
			    fw_mgmt_load_validate_fw_req->firmware_tag);

		request_id = fw_mgmt_load_validate_fw_req->request_id;
		tag = (char *)fw_mgmt_load_validate_fw_req->firmware_tag;
		fw_down_type = GB_FW_MGMT_TYPE_LOADED_FW;
		break;
	case GB_FW_MGMT_TYPE_BACKEND_FW_VERSION:
		fw_mgmt_backend_fw_ver_rsp = &op_rsp->fw_mgmt_backend_fw_ver_rsp;
		fw_mgmt_backend_fw_ver_req = &op_req->fw_mgmt_backend_fw_ver_req;
		payload_size = sizeof(*fw_mgmt_backend_fw_ver_rsp);

		gbsim_debug("AP backend firmware version (tag=%s)\n",
				fw_mgmt_backend_fw_ver_req->firmware_tag);

		fw_mgmt_backend_fw_ver_rsp->major = htole16(1);
		fw_mgmt_backend_fw_ver_rsp->minor = htole16(2);
		break;
	case GB_FW_MGMT_TYPE_BACKEND_FW_UPDATE:
		fw_mgmt_backend_fw_update_req = &op_req->fw_mgmt_backend_fw_update_req;

		gbsim_debug("AP backend fw update (id=%u tag=%s)\n",
			    fw_mgmt_backend_fw_update_req->request_id,
			    fw_mgmt_backend_fw_update_req->firmware_tag);

		request_id = fw_mgmt_backend_fw_update_req->request_id;
		tag = (char *)fw_mgmt_backend_fw_update_req->firmware_tag;
		fw_down_type = GB_FW_MGMT_TYPE_BACKEND_FW_UPDATED;
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

	fw_download_hd_cport_id = find_hd_cport_for_protocol(0x17);

	if (!fw_download_hd_cport_id) {
		gbsim_error("%s: couldn't find hd_cport_id for firmware download cport (%d)\n",
				__func__, fw_download_hd_cport_id);
		return 0;
	}

	fw_mgmt_hd_cport_id = hd_cport_id;
	fw_down_request_id = request_id;

	switch (oph->type) {
	case GB_FW_MGMT_TYPE_LOAD_AND_VALIDATE_FW:
		if (fw_mgmt_load_validate_fw_req->load_method != GB_FW_LOAD_METHOD_UNIPRO) {
			fw_mgmt_request_send(GB_FW_MGMT_TYPE_LOADED_FW, hd_cport_id, request_id);
			return 0;
		}
		/* Fallback */
	case GB_FW_MGMT_TYPE_BACKEND_FW_UPDATE:
		/* Download firmware over unipro using fw-download cport */
		ret = download_firmware(tag, fw_download_hd_cport_id, download_callback);
		if (ret) {
			gbsim_error("%s: failed to download firmware over unipro (%d)\n",
					__func__, ret);
			return 0;
		}
		break;
	default:
		break;
	}

	return 0;
}

/* Response from AP to Module, in response to a request Module has sent earlier */
static int fw_mgmt_handler_response(uint16_t cport_id, uint16_t hd_cport_id,
				void *rbuf, size_t rsize)
{
	struct op_msg *op_rsp = rbuf;
	struct gb_operation_msg_hdr *oph = &op_rsp->header;

	/* Did the request fail? */
	if (oph->result) {
		gbsim_error("%s: Operation type: %s FAILED (%d)\n", __func__,
			    fw_mgmt_get_operation(oph->type & ~OP_RESPONSE),
			    oph->result);
		return oph->result;
	}

	return 0;
}

int fw_mgmt_handler(struct gbsim_connection *connection, void *rbuf,
		    size_t rsize, void *tbuf, size_t tsize)
{
	struct op_msg *op = rbuf;
	struct gb_operation_msg_hdr *oph = &op->header;
	uint16_t cport_id = connection->cport_id;
	uint16_t hd_cport_id = connection->hd_cport_id;

	if (oph->type & OP_RESPONSE)
		return fw_mgmt_handler_response(cport_id, hd_cport_id, rbuf, rsize);
	else
		return fw_mgmt_handler_request(cport_id, hd_cport_id, rbuf, rsize,
					   tbuf, tsize);
}
