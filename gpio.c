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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "gbsim.h"

#define GB_GPIO_TYPE_INVALID		0x00
#define GB_GPIO_TYPE_PROTOCOL_VERSION	0x01
#define GB_GPIO_TYPE_LINE_COUNT		0x02
#define GB_GPIO_TYPE_ACTIVATE		0x03
#define GB_GPIO_TYPE_DEACTIVATE		0x04
#define GB_GPIO_TYPE_GET_DIRECTION	0x05
#define GB_GPIO_TYPE_DIRECTION_IN	0x06
#define GB_GPIO_TYPE_DIRECTION_OUT	0x07
#define GB_GPIO_TYPE_GET_VALUE		0x08
#define GB_GPIO_TYPE_SET_VALUE		0x09
#define GB_GPIO_TYPE_SET_DEBOUNCE	0x0a
#define GB_GPIO_TYPE_RESPONSE		0x80

static int gpio_dir[4];

void gpio_handler(__u8 *rbuf, size_t size)
{
	struct op_header *oph;
	char *tbuf;
	struct op_msg *op_req, *op_rsp;
	struct cport_msg *cport_req, *cport_rsp;

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
	case GB_GPIO_TYPE_PROTOCOL_VERSION:
		op_rsp->header.size = sizeof(struct op_header) +
				      sizeof(struct protocol_version_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_PROTOCOL_VERSION;
		op_rsp->pv_rsp.status = PROTOCOL_STATUS_SUCCESS;
		op_rsp->pv_rsp.version_major = GREYBUS_VERSION_MAJOR;
		op_rsp->pv_rsp.version_minor = GREYBUS_VERSION_MINOR;
		gbsim_debug("Module %d -> AP CPort %d GPIO protocol version response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case GB_GPIO_TYPE_LINE_COUNT:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_line_count_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_LINE_COUNT;
		op_rsp->gpio_lc_rsp.status = PROTOCOL_STATUS_SUCCESS;
		op_rsp->gpio_lc_rsp.count = 3; /* Something arbitrary, but useful */
		gbsim_debug("Module %d -> AP CPort %d GPIO line count response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case GB_GPIO_TYPE_ACTIVATE:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_activate_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_ACTIVATE;
		op_rsp->gpio_act_rsp.status = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d activate request\n  ",
			    cport_to_module_id(cport_req->cport), cport_req->cport, op_req->gpio_act_req.which);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case GB_GPIO_TYPE_DEACTIVATE:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_deactivate_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_DEACTIVATE;
		op_rsp->gpio_deact_rsp.status = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d deactivate request\n  ",
			    cport_to_module_id(cport_req->cport), cport_req->cport, op_req->gpio_deact_req.which);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, cport_rsp, op_req->header.size + 1);
		break;
	case GB_GPIO_TYPE_GET_DIRECTION:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_get_direction_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_GET_DIRECTION;
		op_rsp->gpio_get_dir_rsp.status = PROTOCOL_STATUS_SUCCESS;
		op_rsp->gpio_get_dir_rsp.direction = gpio_dir[op_req->gpio_get_dir_req.which];
		gbsim_debug("Module %d -> AP CPort %d GPIO %d get direction (%d) response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport, op_req->gpio_get_dir_req.which, op_rsp->gpio_get_dir_rsp.direction);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case GB_GPIO_TYPE_DIRECTION_IN:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_direction_input_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_DIRECTION_IN;
		op_rsp->gpio_get_dir_rsp.status = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d direction input request\n  ",
			    cport_to_module_id(cport_req->cport), cport_req->cport, op_req->gpio_dir_input_req.which);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case GB_GPIO_TYPE_DIRECTION_OUT:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_direction_output_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_DIRECTION_OUT;
		op_rsp->gpio_dir_output_rsp.status = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d direction output request\n  ",
			    cport_to_module_id(cport_req->cport), cport_req->cport, op_req->gpio_dir_output_req.which);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case GB_GPIO_TYPE_GET_VALUE:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_get_value_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_GET_VALUE;
		op_rsp->gpio_get_val_rsp.status = PROTOCOL_STATUS_SUCCESS;
		op_rsp->gpio_get_val_rsp.value = 1;
		gbsim_debug("Module %d -> AP CPort %d GPIO %d get value (%d) response\n  ",
			    cport_to_module_id(cport_req->cport), cport_rsp->cport, op_req->gpio_get_val_req.which, op_rsp->gpio_get_val_rsp.value);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case GB_GPIO_TYPE_SET_VALUE:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_set_value_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_SET_VALUE;
		op_rsp->gpio_set_val_rsp.status = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d set value (%d) request\n  ",
			    cport_to_module_id(cport_req->cport), cport_req->cport, op_req->gpio_set_val_req.which, op_req->gpio_set_val_req.value);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	case GB_GPIO_TYPE_SET_DEBOUNCE:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_set_debounce_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_SET_DEBOUNCE;
		op_rsp->gpio_set_db_rsp.status = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d set debounce (%d us) request\n  ",
			    cport_to_module_id(cport_req->cport), cport_req->cport, op_req->gpio_set_db_req.which, op_req->gpio_set_db_req.usec);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, cport_rsp, op_rsp->header.size + 1);
		break;
	default:
		gbsim_error("gpio operation type %02x not supported\n", oph->type);
	}

	free(tbuf);
}

void gpio_init(void)
{
	if (bbb_backend)
		;
}
