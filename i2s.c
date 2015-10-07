
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

#define CONFIG_COUNT_MAX 20

int i2s_mgmt_handler(struct gbsim_cport *cport, void *rbuf,
		     size_t rsize, void *tbuf, size_t tsize)
{
	struct gb_operation_msg_hdr *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	struct gb_i2s_mgmt_configuration *conf;
	size_t payload_size;
	uint16_t message_size;
	uint16_t hd_cport_id = cport->hd_cport_id;
	uint8_t result = PROTOCOL_STATUS_SUCCESS;

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct gb_operation_msg_hdr *)&op_req->header;

	switch (oph->type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(struct gb_protocol_version_response);
		op_rsp->pv_rsp.major = GREYBUS_VERSION_MAJOR;
		op_rsp->pv_rsp.minor = GREYBUS_VERSION_MINOR;
		break;
	case GB_I2S_MGMT_TYPE_GET_SUPPORTED_CONFIGURATIONS:
		payload_size = sizeof(struct gb_i2s_mgmt_get_supported_configurations_response) +
			sizeof(struct gb_i2s_mgmt_configuration) * CONFIG_COUNT_MAX;

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
		conf->ll_mclk_role = GB_I2S_MGMT_ROLE_MASTER;
		conf->ll_bclk_role = GB_I2S_MGMT_ROLE_MASTER;
		conf->ll_wclk_role = GB_I2S_MGMT_ROLE_MASTER;
		conf->ll_wclk_polarity = GB_I2S_MGMT_POLARITY_NORMAL;
		conf->ll_wclk_change_edge = GB_I2S_MGMT_EDGE_FALLING;
		conf->ll_wclk_tx_edge = GB_I2S_MGMT_EDGE_RISING;
		conf->ll_wclk_rx_edge = GB_I2S_MGMT_EDGE_FALLING;
		conf->ll_data_offset = 1;
		break;
	case GB_I2S_MGMT_TYPE_SET_CONFIGURATION:
		payload_size = 0;
		break;
	case GB_I2S_MGMT_TYPE_SET_SAMPLES_PER_MESSAGE:
		payload_size = 0;
		break;
	case GB_I2S_MGMT_TYPE_SET_START_DELAY:
		payload_size = 0;
		break;
	case GB_I2S_MGMT_TYPE_ACTIVATE_CPORT:
		payload_size = 0;
		break;
	case GB_I2S_MGMT_TYPE_DEACTIVATE_CPORT:
		payload_size = 0;
		break;
	default:
		gbsim_error("i2s mgmt operation type %02x not supported\n", oph->type);
		return -EINVAL;
	}

	message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
	return send_response(hd_cport_id, op_rsp, message_size,
				oph->operation_id, oph->type, result);
}


int i2s_data_handler(struct gbsim_cport *cport, void *rbuf,
		     size_t rsize, void *tbuf, size_t tsize)
{
	struct gb_operation_msg_hdr *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	size_t payload_size;
	uint16_t message_size;
	uint16_t hd_cport_id = cport->hd_cport_id;
	uint8_t result = PROTOCOL_STATUS_SUCCESS;

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct gb_operation_msg_hdr *)&op_req->header;

	switch (oph->type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(struct gb_protocol_version_response);
		op_rsp->pv_rsp.major = GREYBUS_VERSION_MAJOR;
		op_rsp->pv_rsp.minor = GREYBUS_VERSION_MINOR;
		break;
	case GB_I2S_DATA_TYPE_SEND_DATA:
		payload_size = 0;
		break;
	default:
		gbsim_error("i2s data operation type %02x not supported\n", oph->type);
		return -EINVAL;
	}

	message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
	return send_response(hd_cport_id, op_rsp, message_size,
			oph->operation_id, oph->type, result);
}

char *i2s_mgmt_get_operation(uint8_t type)
{
	switch (type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		return "GB_I2S_MGMT_TYPE_PROTOCOL_VERSION";
	case GB_I2S_MGMT_TYPE_GET_SUPPORTED_CONFIGURATIONS:
		return "GB_I2S_MGMT_TYPE_GET_SUPPORTED_CONFIGURATIONS";
	case GB_I2S_MGMT_TYPE_SET_CONFIGURATION:
		return "GB_I2S_MGMT_TYPE_SET_CONFIGURATION";
	case GB_I2S_MGMT_TYPE_SET_SAMPLES_PER_MESSAGE:
		return "GB_I2S_MGMT_TYPE_SET_SAMPLES_PER_MESSAGE";
	case GB_I2S_MGMT_TYPE_GET_PROCESSING_DELAY:
		return "GB_I2S_MGMT_TYPE_GET_PROCESSING_DELAY";
	case GB_I2S_MGMT_TYPE_SET_START_DELAY:
		return "GB_I2S_MGMT_TYPE_SET_START_DELAY";
	case GB_I2S_MGMT_TYPE_ACTIVATE_CPORT:
		return "GB_I2S_MGMT_TYPE_ACTIVATE_CPORT";
	case GB_I2S_MGMT_TYPE_DEACTIVATE_CPORT:
		return "GB_I2S_MGMT_TYPE_DEACTIVATE_CPORT";
	case GB_I2S_MGMT_TYPE_REPORT_EVENT:
		return "GB_I2S_MGMT_TYPE_REPORT_EVENT";
	default:
		return "(Unknown operation)";
	}
}

char *i2s_data_get_operation(uint8_t type)
{
	switch (type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		return "GB_I2S_DATA_TYPE_PROTOCOL_VERSION";
	case GB_I2S_DATA_TYPE_SEND_DATA:
		return "GB_I2S_DATA_TYPE_SEND_DATA";
	default:
		return "(Unknown operation)";
	}
}

void i2s_init(void)
{

}
