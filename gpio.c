/*
 * Greybus Simulator
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <fcntl.h>
#include <pthread.h>
#include <libsoc_gpio.h>
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
#define GB_GPIO_TYPE_IRQ_TYPE		0x0b
#define GB_GPIO_TYPE_IRQ_ACK		0x0c
#define GB_GPIO_TYPE_IRQ_MASK		0x0d
#define GB_GPIO_TYPE_IRQ_UNMASK		0x0e
#define GB_GPIO_TYPE_IRQ_EVENT		0x0f
#define GB_GPIO_TYPE_RESPONSE		0x80

static int gpio_dir[6];
static gpio *gpios[6];

void gpio_handler(unsigned int cport, __u8 *rbuf, size_t size)
{
	struct op_header *oph;
	char *tbuf;
	struct op_msg *op_req, *op_rsp;

	tbuf = malloc(4 * 1024);
	if (!tbuf) {
		gbsim_error("failed to allocate gpio handler tx buf\n");
		return;
	}
	op_req = (struct op_msg *)rbuf;
	op_rsp = (struct op_msg *)tbuf;
	oph = (struct op_header *)&op_req->header;

	/* Store the cport id in the header pad bytes */
	op_rsp->header.pad[0] = cport & 0xff;
	op_rsp->header.pad[1] = (cport >> 8) & 0xff;

	switch (oph->type) {
	case GB_GPIO_TYPE_PROTOCOL_VERSION:
		op_rsp->header.size = sizeof(struct op_header) +
				      sizeof(struct protocol_version_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_PROTOCOL_VERSION;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		op_rsp->pv_rsp.version_major = GREYBUS_VERSION_MAJOR;
		op_rsp->pv_rsp.version_minor = GREYBUS_VERSION_MINOR;
		gbsim_debug("Module %d -> AP CPort %d GPIO protocol version response\n  ",
			    cport_to_module_id(cport), cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_LINE_COUNT:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_line_count_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_LINE_COUNT;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		op_rsp->gpio_lc_rsp.count = 5; /* Something arbitrary, but useful */
		gbsim_debug("Module %d -> AP CPort %d GPIO line count response\n  ",
			    cport_to_module_id(cport), cport);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_ACTIVATE:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_ACTIVATE;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d activate request\n  ",
			    cport_to_module_id(cport), cport, op_req->gpio_act_req.which);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_DEACTIVATE:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_DEACTIVATE;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d deactivate request\n  ",
			    cport_to_module_id(cport), cport, op_req->gpio_deact_req.which);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_GET_DIRECTION:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_get_direction_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_GET_DIRECTION;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		if (bbb_backend)
			op_rsp->gpio_get_dir_rsp.direction = libsoc_gpio_get_direction(gpios[op_req->gpio_dir_output_req.which]);
		else
			op_rsp->gpio_get_dir_rsp.direction = gpio_dir[op_req->gpio_get_dir_req.which];
		gbsim_debug("Module %d -> AP CPort %d GPIO %d get direction (%d) response\n  ",
			    cport_to_module_id(cport), cport, op_req->gpio_get_dir_req.which, op_rsp->gpio_get_dir_rsp.direction);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_DIRECTION_IN:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_DIRECTION_IN;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d direction input request\n  ",
			    cport_to_module_id(cport), cport, op_req->gpio_dir_input_req.which);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		if (bbb_backend)
			libsoc_gpio_set_direction(gpios[op_req->gpio_dir_output_req.which], INPUT);
		else
			gpio_dir[op_req->gpio_dir_output_req.which] = 0;
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_DIRECTION_OUT:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_DIRECTION_OUT;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d direction output request\n  ",
			    cport_to_module_id(cport), cport, op_req->gpio_dir_output_req.which);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		if (bbb_backend)
			libsoc_gpio_set_direction(gpios[op_req->gpio_dir_output_req.which], OUTPUT);
		else
			gpio_dir[op_req->gpio_dir_output_req.which] = 1;
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_GET_VALUE:
		op_rsp->header.size = sizeof(struct op_header) +
				   sizeof(struct gpio_get_value_rsp);
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_GET_VALUE;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		if (bbb_backend)
			op_rsp->gpio_get_val_rsp.value = libsoc_gpio_get_level(gpios[op_req->gpio_dir_output_req.which]);
		else
			op_rsp->gpio_get_val_rsp.value = 1;
		gbsim_debug("Module %d -> AP CPort %d GPIO %d get value (%d) response\n  ",
			    cport_to_module_id(cport), cport, op_req->gpio_get_val_req.which, op_rsp->gpio_get_val_rsp.value);
		if (verbose)
			gbsim_dump((__u8 *)op_rsp, op_rsp->header.size);
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_SET_VALUE:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_SET_VALUE;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d set value (%d) request\n  ",
			    cport_to_module_id(cport), cport, op_req->gpio_set_val_req.which, op_req->gpio_set_val_req.value);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		if (bbb_backend)
			libsoc_gpio_set_level(gpios[op_req->gpio_set_val_req.which], op_req->gpio_set_val_req.value);
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_SET_DEBOUNCE:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_SET_DEBOUNCE;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP -> Module %d CPort %d GPIO %d set debounce (%d us) request\n  ",
			    cport_to_module_id(cport), cport, op_req->gpio_set_db_req.which, op_req->gpio_set_db_req.usec);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_IRQ_TYPE:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_IRQ_TYPE;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP CPort %d -> Module %d GPIO protocol IRQ type %d request\n  ",
			    cport, cport_to_module_id(cport),
			    op_req->gpio_irq_type_req.type);
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_IRQ_ACK:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_IRQ_ACK;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP CPort %d -> Module %d GPIO protocol IRQ ack request\n  ",
			    cport, cport_to_module_id(cport));
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_IRQ_MASK:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_IRQ_MASK;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP CPort %d -> Module %d GPIO protocol IRQ mask request\n  ",
			    cport, cport_to_module_id(cport));
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, op_rsp, op_rsp->header.size);
		break;
	case GB_GPIO_TYPE_IRQ_UNMASK:
		op_rsp->header.size = sizeof(struct op_header) + 0;
		op_rsp->header.id = oph->id;
		op_rsp->header.type = OP_RESPONSE | GB_GPIO_TYPE_IRQ_UNMASK;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;
		gbsim_debug("AP CPort %d -> Module %d GPIO protocol IRQ unmask request\n  ",
			    cport, cport_to_module_id(cport));
		if (verbose)
			gbsim_dump((__u8 *)op_req, op_req->header.size);
		write(cport_in, op_rsp, op_rsp->header.size);
#define TEST_HACK
#ifdef TEST_HACK
		op_req->header.size = sizeof(struct op_header) + 1;
		op_req->header.id = 0x42;
		op_req->header.type = GB_GPIO_TYPE_IRQ_EVENT;
		op_req->header.result = 0;
		op_req->gpio_irq_event_req.which = 1;
		gbsim_debug("Module %d -> AP CPort %d GPIO protocol IRQ event request\n  ",
			    cport_to_module_id(cport), cport);
		write(cport_in, op_req, op_req->header.size);
#endif
		break;
	case OP_RESPONSE | GB_GPIO_TYPE_IRQ_EVENT:
		gbsim_debug("gpio irq event response received\n");
		break;
	default:
		gbsim_error("gpio operation type %02x not supported\n", oph->type);
	}

	free(tbuf);
}

void gpio_init(void)
{
	int i;

	if (bbb_backend) {
		/*
		 * Grab the four onboard LEDs (gpio1:24-27) and then
		 * P9-12 and P8-26 (gpio1:28-29) to support input. The
		 * pins on the header can be used in loopback mode for
		 * testing.
		 */
		for (i=0; i<6; i++)
			gpios[i] = libsoc_gpio_request(56+i, LS_GREEDY);
	}
}
