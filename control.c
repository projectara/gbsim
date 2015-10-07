/*
 * Greybus Simulator: Control CPort protocol
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

int control_handler(struct gbsim_cport *cport, void *rbuf,
		    size_t rsize, void *tbuf, size_t tsize)
{
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp = tbuf;
	struct gb_operation_msg_hdr *oph = &op_req->header;
	size_t payload_size;
	uint16_t message_size = sizeof(*oph);
	uint16_t hd_cport_id = cport->hd_cport_id;

	switch (oph->type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(op_rsp->pv_rsp);
		op_rsp->pv_rsp.major = GB_CONTROL_VERSION_MAJOR;
		op_rsp->pv_rsp.minor = GB_CONTROL_VERSION_MINOR;
		break;
	case GB_CONTROL_TYPE_GET_MANIFEST_SIZE:
		payload_size = sizeof(op_rsp->control_msize_rsp);
		op_rsp->control_msize_rsp.size = htole16(info.manifest_size);
		break;
	case GB_CONTROL_TYPE_GET_MANIFEST:
		payload_size = info.manifest_size;
		memcpy(&op_rsp->control_manifest_rsp.data, info.manifest,
		       payload_size);
		break;
	case GB_CONTROL_TYPE_CONNECTED:
		payload_size = 0;
		break;
	case GB_CONTROL_TYPE_DISCONNECTED:
		payload_size = 0;
		break;
	default:
		gbsim_error("control operation type %02x not supported\n", oph->type);
		return -EINVAL;
	}

	message_size += payload_size;
	return send_response(hd_cport_id, op_rsp, message_size,
				oph->operation_id, oph->type,
				PROTOCOL_STATUS_SUCCESS);
}

char *control_get_operation(uint8_t type)
{
	switch (type) {
	case GB_REQUEST_TYPE_INVALID:
		return "GB_CONTROL_TYPE_INVALID";
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		return "GB_CONTROL_TYPE_PROTOCOL_VERSION";
	case GB_CONTROL_TYPE_PROBE_AP:
		return "GB_CONTROL_TYPE_PROBE_AP";
	case GB_CONTROL_TYPE_GET_MANIFEST_SIZE:
		return "GB_CONTROL_TYPE_GET_MANIFEST_SIZE";
	case GB_CONTROL_TYPE_GET_MANIFEST:
		return "GB_CONTROL_TYPE_GET_MANIFEST";
	case GB_CONTROL_TYPE_CONNECTED:
		return "GB_CONTROL_TYPE_CONNECTED";
	case GB_CONTROL_TYPE_DISCONNECTED:
		return "GB_CONTROL_TYPE_DISCONNECTED";
	default:
		return "(Unknown operation)";
	}
}
