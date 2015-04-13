/*
 * Greybus Simulator
 *
 * Copyright 2014, 2015 Google Inc.
 * Copyright 2014, 2015 Linaro Ltd.
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

#define CONFIG_COUNT_MAX 32

int i2s_mgmt_handler(uint16_t cport_id, void *rbuf, size_t rsize,
					void *tbuf, size_t tsize)
{
	struct op_header *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	struct gb_i2s_mgmt_configuration *conf;
	size_t sz;
	uint8_t module_id;

	module_id = cport_to_module_id(cport_id);

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct op_header *)&op_req->header;

	/* Store the cport id in the header pad bytes */
	op_rsp->header.pad[0] = cport_id & 0xff;
	op_rsp->header.pad[1] = (cport_id >> 8) & 0xff;

	switch (oph->type) {
	case GB_I2S_MGMT_TYPE_GET_SUPPORTED_CONFIGURATIONS:
		sz = sizeof(struct op_header) +
			sizeof(struct gb_i2s_mgmt_get_supported_configurations_response) +
			sizeof(struct gb_i2s_mgmt_configuration) * CONFIG_COUNT_MAX;

		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;

		op_rsp->header.type = OP_RESPONSE | GB_I2S_MGMT_TYPE_GET_SUPPORTED_CONFIGURATIONS;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;

		op_rsp->i2s_mgmt_get_sup_conf_rsp.config_count = 1;

		conf = &op_rsp->i2s_mgmt_get_sup_conf_rsp.config[0];
		conf->sample_frequency = htole32(48000);
		conf->num_channels = 2;
		conf->bytes_per_channel = 2;
		conf->byte_order = GB_I2S_MGMT_BYTE_ORDER_LE;
		conf->spatial_locations = htole32(
						GB_I2S_MGMT_SPATIAL_LOCATION_FL |
						GB_I2S_MGMT_SPATIAL_LOCATION_FR);
		conf->ll_protocol = htole32(GB_I2S_MGMT_PROTOCOL_I2S);
		conf->ll_bclk_role = GB_I2S_MGMT_ROLE_MASTER;
		conf->ll_wclk_role = GB_I2S_MGMT_ROLE_MASTER;
		conf->ll_wclk_polarity = GB_I2S_MGMT_POLARITY_NORMAL;
		conf->ll_wclk_change_edge = GB_I2S_MGMT_EDGE_FALLING;
		conf->ll_wclk_tx_edge = GB_I2S_MGMT_EDGE_FALLING;
		conf->ll_wclk_rx_edge = GB_I2S_MGMT_EDGE_RISING;
		conf->ll_data_offset = 1;

		gbsim_debug("Module %hhu -> AP CPort %hu I2S GET_CONFIGURATION response\n  ",
			    module_id, cport_id);
		if (verbose)
			gbsim_dump(op_rsp, sz);
		write(to_ap, op_rsp, sz);
		break;
	case GB_I2S_MGMT_TYPE_SET_CONFIGURATION:
		sz = sizeof(struct op_header);
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;

		op_rsp->header.type = OP_RESPONSE | GB_I2S_MGMT_TYPE_SET_CONFIGURATION;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;

		gbsim_debug("Module %hhu -> AP CPort %hu I2S SET_CONFIGURATION response\n  ",
			    module_id, cport_id);
		if (verbose)
			gbsim_dump(op_rsp, sz);
		write(to_ap, op_rsp, sz);
		break;
	case GB_I2S_MGMT_TYPE_SET_SAMPLES_PER_MESSAGE:
		sz = sizeof(struct op_header);
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;

		op_rsp->header.type = OP_RESPONSE | GB_I2S_MGMT_TYPE_SET_SAMPLES_PER_MESSAGE;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;

		gbsim_debug("Module %hhu -> AP CPort %hu I2S SET_SAMPLES_PER_MESSAGE response\n  ",
			    module_id, cport_id);
		if (verbose)
			gbsim_dump(op_rsp, sz);
		write(to_ap, op_rsp, sz);
		break;
	case GB_I2S_MGMT_TYPE_SET_START_DELAY:
		sz = sizeof(struct op_header);
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;

		op_rsp->header.type = OP_RESPONSE | GB_I2S_MGMT_TYPE_SET_START_DELAY;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;

		gbsim_debug("Module %hhu -> AP CPort %hu I2S SET_START_DELAY response\n  ",
			    module_id, cport_id);
		if (verbose)
			gbsim_dump(op_rsp, sz);
		write(to_ap, op_rsp, sz);
		break;
	case GB_I2S_MGMT_TYPE_ACTIVATE_CPORT:
		sz = sizeof(struct op_header);
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;

		op_rsp->header.type = OP_RESPONSE | GB_I2S_MGMT_TYPE_ACTIVATE_CPORT;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;

		gbsim_debug("Module %hhu -> AP CPort %hu I2S ACTIVATE_CPORT response\n  ",
			    module_id, cport_id);
		if (verbose)
			gbsim_dump(op_rsp, sz);
		write(to_ap, op_rsp, sz);
		break;
	case GB_I2S_MGMT_TYPE_DEACTIVATE_CPORT:
		sz = sizeof(struct op_header);
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;

		op_rsp->header.type = OP_RESPONSE | GB_I2S_MGMT_TYPE_DEACTIVATE_CPORT;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;

		gbsim_debug("Module %hhu -> AP CPort %hu I2S DEACTIVATE_CPORT response\n  ",
			    module_id, cport_id);
		if (verbose)
			gbsim_dump(op_rsp, sz);
		write(to_ap, op_rsp, sz);
		break;
	default:
		gbsim_error("i2s mgmt operation type %02x not supported\n", oph->type);
		return -EINVAL;
	}

	return 0;
}


int i2s_data_handler(uint16_t cport_id, void *rbuf, size_t rsize,
					void *tbuf, size_t tsize)
{
	struct op_header *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	size_t sz;
	uint8_t module_id;

	module_id = cport_to_module_id(cport_id);

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct op_header *)&op_req->header;

	/* Store the cport id in the header pad bytes */
	op_rsp->header.pad[0] = cport_id & 0xff;
	op_rsp->header.pad[1] = (cport_id >> 8) & 0xff;

	switch (oph->type) {
	case GB_I2S_DATA_TYPE_SEND_DATA:
		sz = sizeof(struct op_header);
		op_rsp->header.size = htole16((__u16)sz);
		op_rsp->header.id = oph->id;

		op_rsp->header.type = OP_RESPONSE | GB_I2S_DATA_TYPE_SEND_DATA;
		op_rsp->header.result = PROTOCOL_STATUS_SUCCESS;

		gbsim_debug("Module %hhu -> AP CPort %hu I2S SEND_DATA response\n  ",
			    module_id, cport_id);
		if (verbose)
			gbsim_dump(op_rsp, sz);
		write(to_ap, op_rsp, sz);
		break;
	default:
		gbsim_error("i2s data operation type %02x not supported\n", oph->type);
		return -EINVAL;
	}

	return 0;
}

void i2s_init(void)
{

}
