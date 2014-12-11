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
#define OP_I2C_PROTOCOL_IRQ_TYPE	0x06
#define OP_I2C_PROTOCOL_IRQ_ACK		0x07
#define OP_I2C_PROTOCOL_IRQ_MASK	0x08
#define OP_I2C_PROTOCOL_IRQ_UNMASK	0x09
#define OP_I2C_PROTOCOL_IRQ_EVENT	0x0a

static __u8 data_byte;
static int ifd;
static int mms114_state;

#define READ_PACKETSIZE		1
#define READ_INFOMATION		2

void i2c_handler(__u8 cport_id, __u8 *rbuf, size_t size)
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
	size_t sz;

	tbuf = malloc(4 * 1024);
	if (!tbuf) {
		gbsim_error("failed to allocate i2c handler tx buf\n");
		return;
	}
	cport_req = (struct cport_msg *)rbuf;
	op_req = (struct op_msg *)cport_req->data;
	cport_rsp = (struct cport_msg *)tbuf;
	cport_rsp->cport = cport_id;
	op_rsp = (struct op_msg *)cport_rsp->data;
	oph = (struct op_header *)&op_req->header;
	
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
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, sz);
		write(cport_in, cport_rsp, sz + 1);
		break;
	case OP_I2C_PROTOCOL_FUNCTIONALITY:
		sz = sizeof(struct op_header) +
				   sizeof(struct i2c_functionality_rsp);
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_FUNCTIONALITY;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		op_rsp->i2c_fcn_rsp.functionality = htole32(I2C_FUNC_I2C|I2C_FUNC_PROTOCOL_MANGLING);
		gbsim_debug("Module %d -> AP CPort %d I2C protocol functionality response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, sz);
		write(cport_in, cport_rsp, sz + 1);
		break;
	case OP_I2C_PROTOCOL_TIMEOUT:
		sz = sizeof(struct op_header) + 0;
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_TIMEOUT;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("Module %d -> AP CPort %d I2C protocol timeout response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, sz);
		write(cport_in, cport_rsp, sz + 1);
		break;
	case OP_I2C_PROTOCOL_RETRIES:
		sz = sizeof(struct op_header) + 0;
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_RETRIES;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("Module %d -> AP CPort %d I2C protocol retries response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, sz);
		write(cport_in, cport_rsp, sz + 1);
		break;
	case OP_I2C_PROTOCOL_TRANSFER:
		op_count = le16toh(op_req->i2c_xfer_req.op_count);
		write_data = (__u8 *)&op_req->i2c_xfer_req.desc[op_count];
		gbsim_debug("Number of transfer ops %d\n", op_count);
		for (i = 0; i < op_count; i++) {
			struct i2c_transfer_desc *desc;
			__u16 addr;
			__u16 flags;
			__u16 size;

			desc = &op_req->i2c_xfer_req.desc[i];
			addr = le16toh(desc->addr);
			flags = le16toh(desc->flags);
			size = le16toh(desc->size);
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
					if (mms114_state == READ_PACKETSIZE)
						op_rsp->i2c_xfer_rsp.data[0] = 8;
					else if (mms114_state == READ_INFOMATION) {
						op_rsp->i2c_xfer_rsp.data[0] = 0xa2;
						op_rsp->i2c_xfer_rsp.data[1] = 0x11;
						op_rsp->i2c_xfer_rsp.data[2] = 0x08;
						op_rsp->i2c_xfer_rsp.data[3] = 0x80;
						op_rsp->i2c_xfer_rsp.data[4] = 0x0a;
						op_rsp->i2c_xfer_rsp.data[5] = 0xa0;
						op_rsp->i2c_xfer_rsp.data[6] = 0x00;
						op_rsp->i2c_xfer_rsp.data[7] = 0x00;
					} else
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
				if ((write_data[0] == 0x10) && (write_data[1] == 0x0f)) {
					mms114_state = READ_PACKETSIZE;
					gbsim_debug("i2c mms114 state: read packet size\n");
				} else if ((write_data[0] == 0x10) && (write_data[1] == 0x10)) {
					mms114_state = READ_INFOMATION;
					gbsim_debug("i2c mms114 state: read infomation\n");
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
			sz = sizeof(struct op_header) + 1 + read_count;
		else
			sz = sizeof(struct op_header) + 1;

		op_rsp->header.size = htole16((__u16)sz);
		gbsim_debug("Module %d -> AP CPort %d I2C transfer response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, sz);
		write(cport_in, cport_rsp, sz + 1);

		break;
	case OP_I2C_PROTOCOL_IRQ_TYPE:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_IRQ_TYPE;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP CPort %d -> Module %d I2C protocol IRQ type %d request\n  ",
			    cport_req->cport, cport_to_module_id(cport_req->cport),
			    op_req->i2c_type_req.type);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case OP_I2C_PROTOCOL_IRQ_ACK:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_IRQ_ACK;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP CPort %d -> Module %d I2C protocol IRQ ack request\n  ",
			    cport_req->cport, cport_to_module_id(cport_req->cport));
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case OP_I2C_PROTOCOL_IRQ_MASK:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_IRQ_MASK;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP CPort %d -> Module %d I2C protocol IRQ mask request\n  ",
			    cport_req->cport, cport_to_module_id(cport_req->cport));
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case OP_I2C_PROTOCOL_IRQ_UNMASK:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | OP_I2C_PROTOCOL_IRQ_UNMASK;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP CPort %d -> Module %d I2C protocol IRQ unmask request\n  ",
			    cport_req->cport, cport_to_module_id(cport_req->cport));
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
#define TEST_HACK
#ifdef TEST_HACK
		op_req->header.size = sizeof(struct op_header) + 1;
		op_req->header.id = 0x42;
		op_req->header.type = OP_I2C_PROTOCOL_IRQ_EVENT;
		op_req->header.result = 0;
		op_req->i2c_event_req.which = 0;
		cport_req->cport = cport_id;
		write(cport_in, cport_req, op_req->header.size + 1);
#endif
		break;
	case OP_RESPONSE | OP_I2C_PROTOCOL_IRQ_EVENT:
		gbsim_debug("i2c irq event response received\n");
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
