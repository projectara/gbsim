
/*
 * Greybus Simulator
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <fcntl.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "gbsim.h"

static __u8 data_byte;
static int ifd;

int i2c_handler(struct gbsim_cport *cport, void *rbuf,
		size_t rsize, void *tbuf, size_t tsize)
{
	struct gb_operation_msg_hdr *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	int i, op_count;
	__u8 *write_data;
	bool read_op = false;
	int read_count = 0;
	bool write_fail = false;
	size_t payload_size;
	uint16_t message_size;
	uint16_t hd_cport_id = cport->hd_cport_id;
	uint8_t result = PROTOCOL_STATUS_SUCCESS;

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct gb_operation_msg_hdr *)&op_req->header;

	switch (oph->type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(struct gb_protocol_version_response);
		op_rsp->pv_rsp.major = GREYBUS_VERSION_MAJOR;
		op_rsp->pv_rsp.minor = GREYBUS_VERSION_MINOR;
		break;
	case GB_I2C_TYPE_FUNCTIONALITY:
		payload_size = sizeof(struct gb_i2c_functionality_response);
		op_rsp->i2c_fcn_rsp.functionality = htole32(I2C_FUNC_I2C);
		break;
	case GB_I2C_TYPE_TIMEOUT:
		payload_size = 0;
		break;
	case GB_I2C_TYPE_RETRIES:
		payload_size = 0;
		break;
	case GB_I2C_TYPE_TRANSFER:
		op_count = le16toh(op_req->i2c_xfer_req.op_count);
		write_data = (__u8 *)&op_req->i2c_xfer_req.ops[op_count];
		gbsim_debug("Number of transfer ops %d\n", op_count);
		for (i = 0; i < op_count; i++) {
			struct gb_i2c_transfer_op *op;
			__u16 addr;
			__u16 flags;
			__u16 size;

			op = &op_req->i2c_xfer_req.ops[i];
			addr = le16toh(op->addr);
			flags = le16toh(op->flags);
			size = le16toh(op->size);
			read_op = (flags & I2C_M_RD) ? true : false;
			gbsim_debug("op %d: %s address %04x size %04x\n",
				    i, (read_op ? "read" : "write"),
				    addr, size);
			/* FIXME: need some error handling */
			if (bbb_backend)
				if (ioctl(ifd, I2C_SLAVE, addr) < 0)
					gbsim_error("failed setting i2c slave address\n");
			if (read_op) {
				if (bbb_backend) {
					int count;
					ioctl(ifd, BLKFLSBUF);
					count = read(ifd, &op_rsp->i2c_xfer_rsp.data[read_count], size);
					if (count != size)
						gbsim_error("op %d: failed to read %04x bytes\n", i, size);
				} else {
					for (i = read_count; i < (read_count + size); i++)
					op_rsp->i2c_xfer_rsp.data[i] = data_byte++;
				}
				read_count += size;
			} else {
				if (bbb_backend) {
					int count;
					count = write(ifd, write_data, size);
					if (count != size) {
						gbsim_debug("op %d: failed to write %04x bytes\n", i, size);
						write_fail = true;
					}
				}
				write_data += size;
			}
		}

		/* FIXME: handle read failure */
		if (write_fail)
			result = PROTOCOL_STATUS_RETRY;

		payload_size = read_op ? read_count : 0;
		break;
	default:
		return -EINVAL;
	}

	message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
	return send_response(hd_cport_id, op_rsp, message_size,
				oph->operation_id, oph->type, result);
}

char *i2c_get_operation(uint8_t type)
{
	switch (type) {
	case GB_REQUEST_TYPE_INVALID:
		return "GB_I2C_TYPE_INVALID";
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		return "GB_I2C_TYPE_PROTOCOL_VERSION";
	case GB_I2C_TYPE_FUNCTIONALITY:
		return "GB_I2C_TYPE_FUNCTIONALITY";
	case GB_I2C_TYPE_TIMEOUT:
		return "GB_I2C_TYPE_TIMEOUT";
	case GB_I2C_TYPE_RETRIES:
		return "GB_I2C_TYPE_RETRIES";
	case GB_I2C_TYPE_TRANSFER:
		return "GB_I2C_TYPE_TRANSFER";
	default:
		return "(Unknown operation)";
	}
}

void i2c_init(void)
{
	char filename[20];

	if (bbb_backend) {
		snprintf(filename, 19, "/dev/i2c-%d", i2c_adapter);
		ifd = open(filename, O_RDWR);
		if (ifd < 0)
			gbsim_error("failed opening i2c-dev node read/write\n");
	}
}
