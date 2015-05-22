
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

#define OP_I2C_PROTOCOL_VERSION		0x01
#define OP_I2C_PROTOCOL_FUNCTIONALITY	0x02
#define OP_I2C_PROTOCOL_TIMEOUT		0x03
#define OP_I2C_PROTOCOL_RETRIES		0x04
#define OP_I2C_PROTOCOL_TRANSFER	0x05

static __u8 data_byte;
static int ifd;

int i2c_handler(uint16_t cport_id, void *rbuf, size_t rsize,
					void *tbuf, size_t tsize)
{
	struct op_header *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	int i, op_count;
	__u8 *write_data;
	bool read_op = false;
	int read_count = 0;
	bool write_fail = false;
	size_t payload_size;
	uint16_t message_size;
	uint8_t module_id;
	uint8_t result = PROTOCOL_STATUS_SUCCESS;
	ssize_t nbytes;

	module_id = cport_to_module_id(cport_id);

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct op_header *)&op_req->header;

	switch (oph->type) {
	case OP_I2C_PROTOCOL_VERSION:
		payload_size = sizeof(struct protocol_version_rsp);
		op_rsp->pv_rsp.version_major = GREYBUS_VERSION_MAJOR;
		op_rsp->pv_rsp.version_minor = GREYBUS_VERSION_MINOR;
		gbsim_debug("Module %hhu -> AP CPort %hu I2C protocol version response\n  ",
			    module_id, cport_id);
		break;
	case OP_I2C_PROTOCOL_FUNCTIONALITY:
		payload_size = sizeof(struct gb_i2c_functionality_response);
		op_rsp->i2c_fcn_rsp.functionality = htole32(I2C_FUNC_I2C);
		gbsim_debug("Module %hhu -> AP CPort %hu I2C protocol functionality response\n  ",
			    module_id, cport_id);
		break;
	case OP_I2C_PROTOCOL_TIMEOUT:
		payload_size = 0;
		gbsim_debug("Module %hhu -> AP CPort %hu I2C protocol timeout response\n  ",
			    module_id, cport_id);
		break;
	case OP_I2C_PROTOCOL_RETRIES:
		payload_size = 0;
		gbsim_debug("Module %hhu -> AP CPort %hu I2C protocol retries response\n  ",
			    module_id, cport_id);
		break;
	case OP_I2C_PROTOCOL_TRANSFER:
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

		gbsim_debug("Module %hhu -> AP CPort %hu I2C transfer response\n  ",
			    module_id, cport_id);
		break;
	default:
		gbsim_error("i2c operation type %02x not supported\n", oph->type);
		return -EINVAL;
	}

	/* Fill in the response header */
	message_size = sizeof(struct op_header) + payload_size;
	op_rsp->header.size = htole16(message_size);
	op_rsp->header.id = oph->id;
	op_rsp->header.type = OP_RESPONSE | oph->type;
	op_rsp->header.result = result;
	/* Store the cport id in the header pad bytes */
	op_rsp->header.pad[0] = cport_id & 0xff;
	op_rsp->header.pad[1] = (cport_id >> 8) & 0xff;

	if (verbose)
		gbsim_dump(op_rsp, message_size);

	nbytes = write(to_ap, op_req, message_size);
	if (nbytes < 0)
		return nbytes;

	return 0;
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
