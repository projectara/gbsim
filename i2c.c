/*
 * Greybus Simulator
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <linux/i2c.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "gbsim.h"

#define OP_I2C_PROTOCOL_VERSION		0x01
#define OP_I2C_PROTOCOL_FUNCTIONALITY	0x02
#define OP_I2C_PROTOCOL_TIMEOUT		0x03
#define OP_I2C_PROTOCOL_RETRIES		0x04
#define OP_I2C_PROTOCOL_TRANSFER	0x05

static __u8 data_byte;

void i2c_handler(__u8 *rbuf, size_t size)
{
	struct op_header *oph;
	char *tbuf;
	struct op_msg *op_req, *op_rsp;
	struct cport_msg *cport_req, *cport_rsp;
	int i, op_count;
	__u8 *data;
	bool read;
	int read_count = 0;

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
		op_rsp->pv_rsp.status = PROTOCOL_STATUS_SUCCESS;
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
		op_rsp->i2c_fcn_rsp.status = PROTOCOL_STATUS_SUCCESS;
		op_rsp->i2c_fcn_rsp.functionality = (I2C_FUNC_SMBUS_READ_BYTE |
						     I2C_FUNC_SMBUS_WRITE_BYTE |
						     I2C_FUNC_SMBUS_WRITE_I2C_BLOCK);
		gbsim_debug("Module %d -> AP CPort %d I2C protocol functionality response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case OP_I2C_PROTOCOL_TIMEOUT:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct i2c_timeout_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_TIMEOUT;
		op_rsp->i2c_to_rsp.status = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("Module %d -> AP CPort %d I2C protocol timeout response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case OP_I2C_PROTOCOL_RETRIES:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct i2c_retries_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_RETRIES;
		op_rsp->i2c_rt_rsp.status = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("Module %d -> AP CPort %d I2C protocol retries response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case OP_I2C_PROTOCOL_TRANSFER:
		op_count = op_req->i2c_xfer_req.op_count;
		data = (__u8 *)&op_req->i2c_xfer_req.desc[op_count];
		gbsim_debug("Number of transfer ops %d\n", op_count);
		for (i = 0; i < op_count; i++) {
			struct i2c_transfer_desc *desc;
			desc = &op_req->i2c_xfer_req.desc[i];
			read = (desc->flags & I2C_M_RD) ? true : false;
			gbsim_debug("op %d: %s address %04x size %04x\n",
				    i, (read ? "read" : "write"),
				    desc->addr, desc->size);
			if (read)
				read_count += desc->size;
			else
				data += desc->size;
		}

		if (read) {
			op_rsp->header.size = sizeof(struct op_header) + 1 + read_count;
			op_rsp->header.id = oph->id;
			op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_TRANSFER;
			op_rsp->i2c_xfer_rsp.status = PROTOCOL_STATUS_SUCCESS;
			for (i = 0; i < read_count; i++)
				op_rsp->i2c_xfer_rsp.data[i] = data_byte++;
		} else {
			/* Dummy write completion always responds with success */
			op_rsp->header.size = sizeof(struct op_header) + 1;
			op_rsp->header.id = oph->id;
			op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_TRANSFER;
			op_rsp->i2c_xfer_rsp.status = PROTOCOL_STATUS_SUCCESS;
		}

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
