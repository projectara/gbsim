/*
 * Greybus Simulator
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <stdlib.h>
#include <string.h>

#include "gbsim.h"

#define PSY_COUNT 4
#define PSY_MAX 32

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct psy_property {
	int	key;
	int	val;
	int	is_writeable;
};

struct gb_power_supply {
	uint8_t		id;
	char		manufacturer[32];
	char		*model;
	char		serial_number[32];
	uint16_t	type;
	uint16_t	technology;
	uint8_t		props_count;
	struct psy_property	*props;
};

static struct gb_power_supply *gpsy[PSY_COUNT];

static struct gb_power_supply bq27510 = {
	.manufacturer	= "gl",
	.serial_number	= "EAEA-AEAE",
	.model		= "gb_bat",
	.type		= GB_POWER_SUPPLY_BATTERY_TYPE,
	.technology	= GB_POWER_SUPPLY_TECH_LION,
	.props_count	= 10
};

static const struct psy_property psy_prop_bq27510[] = {
		{
			.key		= GB_POWER_SUPPLY_PROP_STATUS,
			.val		= GB_POWER_SUPPLY_STATUS_DISCHARGING,
			.is_writeable	= 1,
		},
		{
			.key		= GB_POWER_SUPPLY_PROP_PRESENT,
			.val		= 1,
			.is_writeable	= 0,
		},
		{
			.key		= GB_POWER_SUPPLY_PROP_VOLTAGE_NOW,
			.val		= 0,
			.is_writeable	= 0,
		},
		{
			.key		= GB_POWER_SUPPLY_PROP_CURRENT_NOW,
			.val		= 0,
			.is_writeable	= 0,
		},
		{
			.key		= GB_POWER_SUPPLY_PROP_CAPACITY,
			.val		= 0,
			.is_writeable	= 0,
		},
		{
			.key		= GB_POWER_SUPPLY_PROP_CAPACITY_LEVEL,
			.val		= 0,
			.is_writeable	= 0,
		},
		{
			.key		= GB_POWER_SUPPLY_PROP_TEMP,
			.val		= 0,
			.is_writeable	= 0,
		},
		{
			.key		= GB_POWER_SUPPLY_PROP_CHARGE_FULL,
			.val		= 0,
			.is_writeable	= 0,
		},
		{
			.key		= GB_POWER_SUPPLY_PROP_CHARGE_NOW,
			.val		= 0,
			.is_writeable	= 0,
		},
		{
			.key		= GB_POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
			.val		= 0,
			.is_writeable	= 0,
		}
};

static struct gb_power_supply bq24190 = {
	.manufacturer	= "gl",
	.serial_number	= "EAEA-AEAE",
	.model		= "gb_charger",
	.type		= GB_POWER_SUPPLY_USB_TYPE,
	.technology	= GB_POWER_SUPPLY_TECH_UNKNOWN,
	.props_count	= 7,
};

static const struct psy_property charger_prop_bq24190[] = {
	{
		.key		= GB_POWER_SUPPLY_PROP_HEALTH,
		.val		= GB_POWER_SUPPLY_HEALTH_GOOD,
		.is_writeable	= 1,
	},
	{
		.key		= GB_POWER_SUPPLY_PROP_ONLINE,
		.val		= 1,
		.is_writeable	= 0,
	},
	{
		.key		= GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
		.val		= 0,
		.is_writeable	= 0,
	},
	{
		.key		= GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
		.val		= 0,
		.is_writeable	= 0,
	},
	{
		.key		= GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
		.val		= 0,
		.is_writeable	= 0,
	},
	{
		.key		= GB_POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
		.val		= 0,
		.is_writeable	= 0,
	},
	{
		.key		= GB_POWER_SUPPLY_PROP_SCOPE,
		.val		= 1,
		.is_writeable	= 0,
	},
};

static struct gb_power_supply *set_properties(struct gb_power_supply *psy_type,
					 const struct psy_property *props)
{
	struct gb_power_supply *psy;
	int i;

	psy = calloc(1, sizeof(*psy));

	*psy = *psy_type;
	psy->props = calloc(psy->props_count, sizeof(struct psy_property));
	for (i = 0; i < psy->props_count; i++)
		psy->props[i] = props[i];

	return psy;
}

static struct gb_power_supply *power_supply_init(int id)
{
	struct gb_power_supply *psy;

	switch(id) {
	case 0:
		psy = set_properties(&bq27510, psy_prop_bq27510);
		break;
	case 1:
		psy = set_properties(&bq27510, psy_prop_bq27510);
		break;
	case 2:
		psy = set_properties(&bq24190, charger_prop_bq24190);
		break;
	case 3:
		psy = set_properties(&bq24190, charger_prop_bq24190);
		break;
	default:
		gbsim_debug("power_supply: wrong power supply id %d\n", id);
		break;
	}
	psy->id = id;

	return psy;
}

static struct psy_property *power_supply_get_psy_prop(struct gb_power_supply *psy, int prop)
{
	int i;

	for (i = 0; i < psy->props_count; i++)
		if (psy->props[i].key == prop)
			return &psy->props[i];
	return NULL;
}

int power_supply_handler(struct gbsim_connection *connection, void *rbuf,
		    size_t rsize, void *tbuf, size_t tsize)
{
	struct gb_operation_msg_hdr *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	struct psy_property *psy_prop;
	size_t payload_size = 0;
	uint16_t message_size;
	uint16_t hd_cport_id = connection->hd_cport_id;
	uint8_t id;
	uint8_t prop;
	uint16_t prop_val;
	int ret;
	int i;


	op_rsp = (struct op_msg *)tbuf;
	oph = (struct gb_operation_msg_hdr *)&op_req->header;

	switch (oph->type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(struct gb_protocol_version_response);
		op_rsp->pv_rsp.major = GB_POWER_SUPPLY_VERSION_MAJOR;
		op_rsp->pv_rsp.minor = GB_POWER_SUPPLY_VERSION_MINOR;
		/* init psy only after we get a version request */
		for (i = 0; i < PSY_COUNT; i++)
			gpsy[i] = power_supply_init(i);
		break;
	case GB_POWER_SUPPLY_TYPE_GET_SUPPLIES:
		payload_size = sizeof(struct gb_power_supply_get_supplies_response);
		op_rsp->psy_get_supplies_rsp.supplies_count = PSY_COUNT;
		break;
	case GB_POWER_SUPPLY_TYPE_GET_DESCRIPTION:
		payload_size = sizeof(struct gb_power_supply_get_description_response);
		id = op_req->psy_get_desc_req.psy_id;

		memcpy(&op_rsp->psy_get_desc_rsp.manufacturer,
		       gpsy[id]->manufacturer, strlen(gpsy[id]->manufacturer));
		memcpy(&op_rsp->psy_get_desc_rsp.model,
		       gpsy[id]->model, strlen(gpsy[id]->model));
		memcpy(&op_rsp->psy_get_desc_rsp.serial_number,
		       gpsy[id]->serial_number, strlen(gpsy[id]->serial_number));

		op_rsp->psy_get_desc_rsp.properties_count = gpsy[id]->props_count;
		op_rsp->psy_get_desc_rsp.type = htole16(gpsy[id]->type);

		break;
	case GB_POWER_SUPPLY_TYPE_GET_PROP_DESCRIPTORS:
		id = op_req->psy_get_props_req.psy_id;
		payload_size = sizeof(struct gb_power_supply_get_property_descriptors_response)
			+ (gpsy[id]->props_count * sizeof(struct gb_power_supply_props_desc));
		op_rsp->psy_get_props_rsp.properties_count = gpsy[id]->props_count;

		for (i = 0; i < gpsy[id]->props_count; i++) {
			op_rsp->psy_get_props_rsp.props[i].property = gpsy[id]->props[i].key;
			op_rsp->psy_get_props_rsp.props[i].is_writeable = gpsy[id]->props[i].is_writeable;
		}

		break;
	case GB_POWER_SUPPLY_TYPE_GET_PROPERTY:
		payload_size = sizeof(struct gb_power_supply_get_property_response);
		id = op_req->psy_get_prop_req.psy_id;
		prop = op_req->psy_get_prop_req.property;

		psy_prop = power_supply_get_psy_prop(gpsy[id], prop);
		if (!psy_prop)
			break;

		op_rsp->psy_get_prop_rsp.prop_val = htole32(psy_prop->val);
		break;
	case GB_POWER_SUPPLY_TYPE_SET_PROPERTY:
		id = op_req->psy_set_prop_req.psy_id;
		prop = op_req->psy_set_prop_req.property;
		prop_val = le32toh(op_req->psy_set_prop_req.prop_val);

		psy_prop = power_supply_get_psy_prop(gpsy[id], prop);
		if (!psy_prop)
			break;
		if (!psy_prop->is_writeable)
			break;
		psy_prop->val = prop_val;
		break;
	}

	/* send response */
	message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
	ret = send_response(hd_cport_id, op_rsp, message_size,
			    oph->operation_id, oph->type,
			    PROTOCOL_STATUS_SUCCESS);

	return ret;
}

char *power_supply_get_operation(uint8_t type)
{
	switch (type) {
	case GB_REQUEST_TYPE_INVALID:
		return "GB_POWER_SUPPLY_TYPE_INVALID";
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		return "GB_POWER_SUPPLY_TYPE_PROTOCOL_VERSION";
	case GB_POWER_SUPPLY_TYPE_GET_SUPPLIES:
		return "GB_POWER_SUPPLY_TYPE_GET_SUPPLIES";
	case GB_POWER_SUPPLY_TYPE_GET_DESCRIPTION:
		return "GB_POWER_SUPPLY_TYPE_GET_DESCRIPTION";
	case GB_POWER_SUPPLY_TYPE_GET_PROP_DESCRIPTORS:
		return "GB_POWER_SUPPLY_TYPE_GET_PROP_DESCRIPTORS";
	case GB_POWER_SUPPLY_TYPE_GET_PROPERTY:
		return "GB_POWER_SUPPLY_TYPE_GET_PROPERTY";
	case GB_POWER_SUPPLY_TYPE_SET_PROPERTY:
		return "GB_POWER_SUPPLY_TYPE_SET_PROPERTY";
	default:
		return "(Unknown operation)";
	}
}
