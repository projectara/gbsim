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

void i2c_handler(__u8 *rbuf, size_t size)
{
	struct op_header *oph;
	char *tbuf;
	struct op_msg *op_req, *op_rsp;
	struct cport_msg *cport_req, *cport_rsp;
	int i, op_count;
	__u8 *write_data;
	bool read_op;
	int read_count = 0;
	bool write_fail = false;

	tbuf = malloc(4 * 1024);
	if (!tbuf) {
		gbsim_error("failed to allocate i2c handler tx buf\n");
		return;
	}
	cport_req = (struct cport_msg *)rbuf;
	op_req = (struct op_msg *)cport_req->data;
	cport_rsp = (struct cport_msg *)tbuf;
	cport_rsp->cport = 0;	/* FIXME hardcoded until we have connections */
	op_rsp = (struct op_msg *)cport_rsp->data;
	oph = (struct op_header *)&op_req->header;
	
	switch (oph->type) {
	case OP_I2C_PROTOCOL_VERSION:
		op_rsp->header.size = sizeof(struct op_header) +
				      sizeof(struct protocol_version_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_VERSION;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		op_rsp->pv_rsp.version_major = GREYBUS_VERSION_MAJOR;
		op_rsp->pv_rsp.version_minor = GREYBUS_VERSION_MINOR;
		gbsim_debug("Module %d -> AP CPort %d I2C protocol version response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case OP_I2C_PROTOCOL_FUNCTIONALITY:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct i2c_functionality_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_FUNCTIONALITY;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		op_rsp->i2c_fcn_rsp.functionality = I2C_FUNC_I2C;
		gbsim_debug("Module %d -> AP CPort %d I2C protocol functionality response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case OP_I2C_PROTOCOL_TIMEOUT:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_TIMEOUT;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("Module %d -> AP CPort %d I2C protocol timeout response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case OP_I2C_PROTOCOL_RETRIES:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_RETRIES;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("Module %d -> AP CPort %d I2C protocol retries response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case OP_I2C_PROTOCOL_TRANSFER:
		op_count = op_req->i2c_xfer_req.op_count;
		write_data = (__u8 *)&op_req->i2c_xfer_req.desc[op_count];
		gbsim_debug("Number of transfer ops %d\n", op_count);
		for (i = 0; i < op_count; i++) {
			struct i2c_transfer_desc *desc;
			desc = &op_req->i2c_xfer_req.desc[i];
			read_op = (desc->flags & I2C_M_RD) ? true : false;
			gbsim_debug("op %d: %s address %04x size %04x\n",
				    i, (read_op ? "read" : "write"),
				    desc->addr, desc->size);
			/* FIXME: need some error handling */
			if (ioctl(ifd, I2C_SLAVE, desc->addr) < 0)
				gbsim_error("failed setting i2c slave address\n");
			if (read_op) {
				if (bbb_backend) {
					int count;
					ioctl(ifd, BLKFLSBUF);
					count = read(ifd, &op_rsp->i2c_xfer_rsp.data[read_count], desc->size);
					if (count != desc->size)
						gbsim_error("op %d: failed to read %04x bytes\n", i, desc->size);
				} else {
					for (i = read_count; i < (read_count + desc->size); i++)
					op_rsp->i2c_xfer_rsp.data[i] = data_byte++;
				}
				read_count += desc->size;
			} else {
				if (bbb_backend) {
					int count;
					count = write(ifd, write_data, desc->size);
					if (count != desc->size) {
						gbsim_debug("op %d: failed to write %04x bytes\n", i, desc->size);
						write_fail = true;
					}
				}
				write_data += desc->size;
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
			op_rsp->header.size = sizeof(struct op_header) + 1 + read_count;
		else
			op_rsp->header.size = sizeof(struct op_header) + 1;

		gbsim_debug("Module %d -> AP CPort %d I2C transfer response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);

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
