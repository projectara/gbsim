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

#include "gbsim.h"

#define OP_I2C_PROTOCOL_VERSION		0x01
#define OP_I2C_PROTOCOL_FUNCTIONALITY	0x02
#define OP_I2C_PROTOCOL_TIMEOUT		0x03
#define OP_I2C_PROTOCOL_RETRIES		0x04
#define OP_I2C_PROTOCOL_TRANSFER	0x05

static __u8 data_byte;
static int ifd;

void i2c_handler(unsigned int cport, __u8 *rbuf, size_t size)
{
	struct op_header *oph;
	char *tbuf;
	struct op_msg *op_req, *op_rsp;
	int i, op_count;
	__u8 *write_data;
	bool read_op;
	int read_count = 0;
	bool write_fail = false;
	size_t sz;

	tbuf = malloc(4 * 1024);
	if (!tbuf) {
		gbsim_error("failed to allocate i2c handler tx buf\n");
		return;
	}
	op_req = (struct op_msg *)rbuf;
	op_rsp = (struct op_msg *)tbuf;
	oph = (struct op_header *)&op_req->header;

	/* Store the cport id in the header pad bytes */
	op_rsp->header.pad[0] = cport & 0xff;
	op_rsp->header.pad[1] = (cport >> 8) & 0xff;
	
	switch (oph->type) {
	case OP_I2C_PROTOCOL_VERSION:
		sz = sizeof(struct op_header) +
				      sizeof(struct protocol_version_rsp);
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_VERSION;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		op_rsp->pv_rsp.version_major = GREYBUS_VERSION_MAJOR;
		op_rsp->pv_rsp.version_minor = GREYBUS_VERSION_MINOR;
		gbsim_debug("Module %d -> AP CPort %d I2C protocol version response\n  ",
			    cport_to_module_id(cport), cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, sz);
		write(cport_in, op_rsp, sz);
		break;
	case OP_I2C_PROTOCOL_FUNCTIONALITY:
		sz = sizeof(struct op_header) +
				   sizeof(struct gb_i2c_functionality_response);
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_FUNCTIONALITY;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		op_rsp->i2c_fcn_rsp.functionality = htole32(I2C_FUNC_I2C);
		gbsim_debug("Module %d -> AP CPort %d I2C protocol functionality response\n  ",
			    cport_to_module_id(cport), cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, sz);
		write(cport_in, op_rsp, sz);
		break;
	case OP_I2C_PROTOCOL_TIMEOUT:
		sz = sizeof(struct op_header) + 0;
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_TIMEOUT;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("Module %d -> AP CPort %d I2C protocol timeout response\n  ",
			    cport_to_module_id(cport), cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, sz);
		write(cport_in, op_rsp, sz);
		break;
	case OP_I2C_PROTOCOL_RETRIES:
		sz = sizeof(struct op_header) + 0;
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_RETRIES;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("Module %d -> AP CPort %d I2C protocol retries response\n  ",
			    cport_to_module_id(cport), cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, sz);
		write(cport_in, op_rsp, sz);
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

		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_TRANSFER;

		if (write_fail)
			op_rsp->header.result = PROTOCOL_STATUS_RETRY;
		else
			/* FIXME: handle read failure */
			op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;

		if (read_op)
			sz = sizeof(struct op_header) + read_count;
		else
			sz = sizeof(struct op_header);

		op_rsp->header.size = htole16((__u16)sz);
		gbsim_debug("Module %d -> AP CPort %d I2C transfer response\n  ",
			    cport_to_module_id(cport), cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, sz);
		write(cport_in, op_rsp, sz);

		break;
	default:
		gbsim_error("i2c operation type %02x not supported\n", oph->type);
	}

	free(tbuf);
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
