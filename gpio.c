
/*
 * Greybus Simulator
 *
 * Copyright 2014-2016 Google Inc.
 * Copyright 2014-2016 Linaro Ltd.
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

struct gb_gpio {
	uint8_t activated;
	uint8_t direction;
	uint8_t value;
	uint8_t irq_type;
	uint8_t irq_unmasked;
};

static struct gb_gpio gb_gpios[6];
static gpio *gpios[6];

static int gb_gpio_set_value(uint8_t which, uint8_t value)
{
	uint8_t which_con;

	gb_gpios[which].value = value;

	/*
	 * check to see if it is even, we do nothig. If it is odd, we simulate
	 * that the gpio is loopback to the even one before it. Of course, if
	 * activated and if it is set as input
	 */
	if (!(which % 2))
		return 0;

	which_con = which - 1;

	if (!gb_gpios[which_con].activated || gb_gpios[which_con].direction != 1)
		return 0;

	if (gb_gpios[which_con].value == value)
		return 0;

	gb_gpios[which_con].value = value;

	 /* check to see if we need to send an irq event */
	if (!gb_gpios[which_con].irq_type || !gb_gpios[which_con].irq_unmasked)
		return 0;

	if (gb_gpios[which_con].irq_type == GB_GPIO_IRQ_TYPE_EDGE_RISING &&
	    value)
		return 1;
	else if (gb_gpios[which_con].irq_type == GB_GPIO_IRQ_TYPE_EDGE_FALLING &&
		 !value)
		return 1;
	else if (gb_gpios[which_con].irq_type == GB_GPIO_IRQ_TYPE_EDGE_BOTH)
		return 1;

	return 0;
}

int gpio_handler(struct gbsim_connection *connection, void *rbuf,
		 size_t rsize, void *tbuf, size_t tsize)
{
	struct gb_operation_msg_hdr *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	size_t payload_size;
	ssize_t nbytes;
	uint16_t message_size;
	uint16_t hd_cport_id = connection->hd_cport_id;
	uint8_t which = 0;
	int send_event = 0;

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct gb_operation_msg_hdr *)&op_req->header;

	switch (oph->type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(struct gb_protocol_version_response);
		op_rsp->pv_rsp.major = GREYBUS_VERSION_MAJOR;
		op_rsp->pv_rsp.minor = GREYBUS_VERSION_MINOR;
		break;
	case GB_GPIO_TYPE_LINE_COUNT:
		payload_size = sizeof(struct gb_gpio_line_count_response);
		op_rsp->gpio_lc_rsp.count = 5; /* Something arbitrary, but useful */
		break;
	case GB_GPIO_TYPE_ACTIVATE:
		payload_size = 0;
		which = op_req->gpio_act_req.which;
		gbsim_debug("GPIO %d activate request\n", which);
		gb_gpios[which].activated = 1;
		break;
	case GB_GPIO_TYPE_DEACTIVATE:
		payload_size = 0;
		which = op_req->gpio_deact_req.which;
		gbsim_debug("GPIO %d deactivate request\n", which);
		gb_gpios[which].activated = 0;
		break;
	case GB_GPIO_TYPE_GET_DIRECTION:
		payload_size = sizeof(struct gb_gpio_get_direction_response);
		which = op_req->gpio_get_dir_req.which;
		if (bbb_backend)
			op_rsp->gpio_get_dir_rsp.direction = libsoc_gpio_get_direction(gpios[which]);
		else
			op_rsp->gpio_get_dir_rsp.direction = gb_gpios[which].direction;
		gbsim_debug("GPIO %d get direction (%d) response\n",
			    which, op_rsp->gpio_get_dir_rsp.direction);
		break;
	case GB_GPIO_TYPE_DIRECTION_IN:
		payload_size = 0;
		which = op_req->gpio_dir_input_req.which;
		gbsim_debug("GPIO %d direction input request\n", which);
		if (bbb_backend)
			libsoc_gpio_set_direction(gpios[which], INPUT);
		else
			gb_gpios[which].direction = 1;
		break;
	case GB_GPIO_TYPE_DIRECTION_OUT:
		payload_size = 0;
		which = op_req->gpio_dir_output_req.which;
		gbsim_debug("GPIO %d direction output request\n", which);
		if (bbb_backend)
			libsoc_gpio_set_direction(gpios[which], OUTPUT);
		else
			gb_gpios[which].direction = 0;
		break;
	case GB_GPIO_TYPE_GET_VALUE:
		payload_size = sizeof(struct gb_gpio_get_value_response);
		which = op_req->gpio_get_val_req.which;
		if (bbb_backend)
			op_rsp->gpio_get_val_rsp.value = libsoc_gpio_get_level(gpios[which]);
		else
			op_rsp->gpio_get_val_rsp.value = gb_gpios[which].value;
		gbsim_debug("GPIO %d get value (%d) response\n  ",
			    which, op_rsp->gpio_get_val_rsp.value);
		break;
	case GB_GPIO_TYPE_SET_VALUE:
		payload_size = 0;
		which = op_req->gpio_set_val_req.which;
		gbsim_debug("GPIO %d set value (%d) request\n  ",
			    which, op_req->gpio_set_val_req.value);
		if (bbb_backend)
			libsoc_gpio_set_level(gpios[which], op_req->gpio_set_val_req.value);
		else
			send_event = gb_gpio_set_value(which, op_req->gpio_set_val_req.value);
		break;
	case GB_GPIO_TYPE_SET_DEBOUNCE:
		payload_size = 0;
		gbsim_debug("GPIO %d set debounce (%d us) request\n  ",
			    op_req->gpio_set_db_req.which, op_req->gpio_set_db_req.usec);
		break;
	case GB_GPIO_TYPE_IRQ_TYPE:
		payload_size = 0;
		which = op_req->gpio_irq_type_req.which;
		gbsim_debug("GPIO %d set IRQ type %d request\n  ",
			    which, op_req->gpio_irq_type_req.type);
		gb_gpios[which].irq_type = op_req->gpio_irq_type_req.type;
		break;
	case GB_GPIO_TYPE_IRQ_MASK:
		payload_size = 0;
		which = op_req->gpio_irq_mask_req.which;
		gb_gpios[which].irq_unmasked = 0;
		break;
	case GB_GPIO_TYPE_IRQ_UNMASK:
		payload_size = 0;
		which = op_req->gpio_irq_unmask_req.which;
		gb_gpios[which].irq_unmasked = 1;
		break;
	default:
		return -EINVAL;
	}

	message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
	nbytes = send_response(hd_cport_id, op_rsp, message_size,
				oph->operation_id, oph->type,
				PROTOCOL_STATUS_SUCCESS);
	if (nbytes)
		return nbytes;

#define TEST_HACK
#ifdef TEST_HACK
	/*
	 * Test GPIO interrupts by sending one when they become unmasked, or
	 * when set value trigger one
	 */
	if (send_event) {
		payload_size = sizeof(struct gb_gpio_irq_event_request);
		op_req->gpio_irq_event_req.which = which - 1;

		/* mask the irq to mimic fw action on event send */
		gb_gpios[which - 1].irq_unmasked = 0;
		message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
		return send_request(hd_cport_id, op_req, message_size, 0,
				    GB_GPIO_TYPE_IRQ_EVENT);
	}
#endif

	return 0;
}

char *gpio_get_operation(uint8_t type)
{
	switch (type) {
	case GB_REQUEST_TYPE_INVALID:
		return "GB_GPIO_TYPE_INVALID";
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		return "GB_GPIO_TYPE_PROTOCOL_VERSION";
	case GB_GPIO_TYPE_LINE_COUNT:
		return "GB_GPIO_TYPE_LINE_COUNT";
	case GB_GPIO_TYPE_ACTIVATE:
		return "GB_GPIO_TYPE_ACTIVATE";
	case GB_GPIO_TYPE_DEACTIVATE:
		return "GB_GPIO_TYPE_DEACTIVATE";
	case GB_GPIO_TYPE_GET_DIRECTION:
		return "GB_GPIO_TYPE_GET_DIRECTION";
	case GB_GPIO_TYPE_DIRECTION_IN:
		return "GB_GPIO_TYPE_DIRECTION_IN";
	case GB_GPIO_TYPE_DIRECTION_OUT:
		return "GB_GPIO_TYPE_DIRECTION_OUT";
	case GB_GPIO_TYPE_GET_VALUE:
		return "GB_GPIO_TYPE_GET_VALUE";
	case GB_GPIO_TYPE_SET_VALUE:
		return "GB_GPIO_TYPE_SET_VALUE";
	case GB_GPIO_TYPE_SET_DEBOUNCE:
		return "GB_GPIO_TYPE_SET_DEBOUNCE";
	case GB_GPIO_TYPE_IRQ_TYPE:
		return "GB_GPIO_TYPE_IRQ_TYPE";
	case GB_GPIO_TYPE_IRQ_MASK:
		return "GB_GPIO_TYPE_IRQ_MASK";
	case GB_GPIO_TYPE_IRQ_UNMASK:
		return "GB_GPIO_TYPE_IRQ_UNMASK";
	case GB_GPIO_TYPE_IRQ_EVENT:
		return "GB_GPIO_TYPE_IRQ_EVENT";
	default:
		return "(Unknown operation)";
	}
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
