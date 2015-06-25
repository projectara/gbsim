
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

int control_handler(uint16_t cport_id, uint16_t hd_cport_id, void *rbuf,
		    size_t rsize, void *tbuf, size_t tsize)
{
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp = tbuf;
	struct op_header *oph = &op_req->header;
	uint16_t message_size = sizeof(*oph);
	uint8_t module_id = cport_to_module_id(cport_id);
	size_t payload_size;
	ssize_t nbytes;

	switch (oph->type) {
	case GB_CONTROL_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(op_rsp->pv_rsp);
		op_rsp->pv_rsp.version_major = GB_CONTROL_VERSION_MAJOR;
		op_rsp->pv_rsp.version_minor = GB_CONTROL_VERSION_MINOR;

		gbsim_debug("Module %hhu -> AP CPort %hu CONTROL protocol version response\n  ",
			    module_id, cport_id);
		break;
	case GB_CONTROL_TYPE_GET_MANIFEST_SIZE:
		payload_size = sizeof(op_rsp->control_msize_rsp);
		op_rsp->control_msize_rsp.size = htole16(info.manifest_size);

		gbsim_debug("Module %hhu -> AP CPort %hu CONTROL manifest size response\n",
			   module_id, cport_id);
		break;
	case GB_CONTROL_TYPE_GET_MANIFEST:
		payload_size = info.manifest_size;
		memcpy(&op_rsp->control_manifest_rsp.data, info.manifest,
		       payload_size);

		gbsim_debug("Module %hhu -> AP CPort %hu CONTROL get manifest response\n",
			   module_id, cport_id);
		break;
	case GB_CONTROL_TYPE_CONNECTED:
		payload_size = 0;

		gbsim_debug("Module %hhu -> AP CPort %hu CONTROL connected response\n",
			   module_id, cport_id);
		break;
	case GB_CONTROL_TYPE_DISCONNECTED:
		payload_size = 0;

		gbsim_debug("Module %hhu -> AP CPort %hu CONTROL dis-connected response\n",
			   module_id, cport_id);
		break;
	default:
		gbsim_error("control operation type %02x not supported\n", oph->type);
		return -EINVAL;
	}

	/* Fill in the response header */
	message_size += payload_size;
	op_rsp->header.size = htole16(message_size);
	op_rsp->header.id = oph->id;
	op_rsp->header.type = OP_RESPONSE | oph->type;
	op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;

	/* Store the cport id in the header pad bytes */
	op_rsp->header.pad[0] = hd_cport_id & 0xff;
	op_rsp->header.pad[1] = (hd_cport_id >> 8) & 0xff;

	/* Send the response to the AP */
	if (verbose)
		gbsim_dump(op_rsp, message_size);

	nbytes = write(to_ap, op_rsp, message_size);
	if (nbytes < 0)
		return nbytes;

	return 0;
}
