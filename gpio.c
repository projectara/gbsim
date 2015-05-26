
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
#include <errno.h>

#include "gbsim.h"

static int gpio_dir[6];
static gpio *gpios[6];

int gpio_handler(uint16_t cport_id, void *rbuf, size_t rsize,
					void *tbuf, size_t tsize)
{
	struct op_header *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	size_t payload_size;
	uint16_t message_size;
	uint8_t module_id;
	uint8_t result = PROTOCOL_STATUS_SUCCESS;
	ssize_t nbytes;

	module_id = cport_to_module_id(cport_id);

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct op_header *)&op_req->header;

	switch (oph->type) {
	case GB_GPIO_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(struct protocol_version_rsp);
		op_rsp->pv_rsp.version_major = GREYBUS_VERSION_MAJOR;
		op_rsp->pv_rsp.version_minor = GREYBUS_VERSION_MINOR;
		gbsim_debug("Module %hhu -> AP CPort %hu GPIO protocol version response\n  ",
			    module_id, cport_id);
		break;
	case GB_GPIO_TYPE_LINE_COUNT:
		payload_size = sizeof(struct gb_gpio_line_count_response);
		op_rsp->gpio_lc_rsp.count = 5; /* Something arbitrary, but useful */
		gbsim_debug("Module %hhu -> AP CPort %hu GPIO line count response\n  ",
			    module_id, cport_id);
		break;
	case GB_GPIO_TYPE_ACTIVATE:
		payload_size = 0;
		gbsim_debug("AP -> Module %hhu CPort %hu GPIO %d activate request\n  ",
			    module_id, cport_id, op_req->gpio_act_req.which);
		break;
	case GB_GPIO_TYPE_DEACTIVATE:
		payload_size = 0;
		gbsim_debug("AP -> Module %hhu CPort %hu GPIO %d deactivate request\n  ",
			    module_id, cport_id, op_req->gpio_deact_req.which);
		break;
	case GB_GPIO_TYPE_GET_DIRECTION:
		payload_size = sizeof(struct gb_gpio_get_direction_response);
		if (bbb_backend)
			op_rsp->gpio_get_dir_rsp.direction = libsoc_gpio_get_direction(gpios[op_req->gpio_dir_output_req.which]);
		else
			op_rsp->gpio_get_dir_rsp.direction = gpio_dir[op_req->gpio_get_dir_req.which];
		gbsim_debug("Module %hhu -> AP CPort %hu GPIO %d get direction (%d) response\n  ",
			    module_id, cport_id, op_req->gpio_get_dir_req.which, op_rsp->gpio_get_dir_rsp.direction);
		break;
	case GB_GPIO_TYPE_DIRECTION_IN:
		payload_size = 0;
		gbsim_debug("AP -> Module %hhu CPort %hu GPIO %d direction input request\n  ",
			    module_id, cport_id, op_req->gpio_dir_input_req.which);
		if (bbb_backend)
			libsoc_gpio_set_direction(gpios[op_req->gpio_dir_output_req.which], INPUT);
		else
			gpio_dir[op_req->gpio_dir_output_req.which] = 0;
		break;
	case GB_GPIO_TYPE_DIRECTION_OUT:
		payload_size = 0;
		gbsim_debug("AP -> Module %hhu CPort %hu GPIO %d direction output request\n  ",
			    module_id, cport_id, op_req->gpio_dir_output_req.which);
		if (bbb_backend)
			libsoc_gpio_set_direction(gpios[op_req->gpio_dir_output_req.which], OUTPUT);
		else
			gpio_dir[op_req->gpio_dir_output_req.which] = 1;
		break;
	case GB_GPIO_TYPE_GET_VALUE:
		payload_size = sizeof(struct gb_gpio_get_value_response);
		if (bbb_backend)
			op_rsp->gpio_get_val_rsp.value = libsoc_gpio_get_level(gpios[op_req->gpio_dir_output_req.which]);
		else
			op_rsp->gpio_get_val_rsp.value = 1;
		gbsim_debug("Module %hhu -> AP CPort %hu GPIO %d get value (%d) response\n  ",
			    module_id, cport_id, op_req->gpio_get_val_req.which, op_rsp->gpio_get_val_rsp.value);
		break;
	case GB_GPIO_TYPE_SET_VALUE:
		payload_size = 0;
		gbsim_debug("AP -> Module %hhu CPort %hu GPIO %d set value (%d) request\n  ",
			    module_id, cport_id, op_req->gpio_set_val_req.which, op_req->gpio_set_val_req.value);
		if (bbb_backend)
			libsoc_gpio_set_level(gpios[op_req->gpio_set_val_req.which], op_req->gpio_set_val_req.value);
		break;
	case GB_GPIO_TYPE_SET_DEBOUNCE:
		payload_size = 0;
		gbsim_debug("AP -> Module %hhu CPort %hu GPIO %d set debounce (%d us) request\n  ",
			    module_id, cport_id, op_req->gpio_set_db_req.which, op_req->gpio_set_db_req.usec);
		break;
	case GB_GPIO_TYPE_IRQ_TYPE:
		payload_size = 0;
		gbsim_debug("AP CPort %hu -> Module %hhu GPIO protocol IRQ type %d request\n  ",
			    cport_id, module_id,
			    op_req->gpio_irq_type_req.type);
		break;
	case GB_GPIO_TYPE_IRQ_MASK:
		payload_size = 0;
		gbsim_debug("AP CPort %hu -> Module %hhu GPIO protocol IRQ mask request\n  ",
			    cport_id, module_id);
		break;
	case GB_GPIO_TYPE_IRQ_UNMASK:
		payload_size = 0;
		gbsim_debug("AP CPort %hu -> Module %hhu GPIO protocol IRQ unmask request\n  ",
			    cport_id, module_id);
		break;
	default:
		gbsim_error("gpio operation type %02x not supported\n", oph->type);
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

	/* Send the response to the AP */
	if (verbose)
		gbsim_dump(op_rsp, message_size);
	nbytes = write(to_ap, op_rsp, message_size);
	if (nbytes < 0)
		return nbytes;

#define TEST_HACK
#ifdef TEST_HACK
	/* Test GPIO interrupts by sending one when they become unmasked */
	if (oph->type == GB_GPIO_TYPE_IRQ_UNMASK) {
		payload_size = sizeof(struct gb_gpio_irq_event_request);
		result = PROTOCOL_STATUS_SUCCESS;
		op_req->gpio_irq_event_req.which = 1;	/* XXX HACK */
		gbsim_debug("Module %hhu -> AP CPort %hu GPIO protocol IRQ event request\n  ",
			    module_id, cport_id);

		/* Fill in the request header */
		message_size = sizeof(struct op_header) + payload_size;
		op_req->header.size = htole16(message_size);
		op_req->header.id = 0;	/* unidirectional */
		op_req->header.type = GB_GPIO_TYPE_IRQ_EVENT;
		op_rsp->header.result = result;
		/* Store the cport id in the header pad bytes */
		op_req->header.pad[0] = cport_id & 0xff;
		op_req->header.pad[1] = (cport_id >> 8) & 0xff;

		if (verbose)
			gbsim_dump(op_req, message_size);
		nbytes = write(to_ap, op_req, message_size);
		if (nbytes < 0)
			return nbytes;
	}
#endif

	return 0;
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
