/*
 * Greybus Simulator
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <fcntl.h>
#include <linux/fs.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "gbsim.h"

#define LIGHTS_COUNT	3

struct gb_channel {
	uint8_t		id;
	uint32_t	mode;
	uint32_t	flags;
	uint8_t		max_brightness;
	uint32_t	color;
	char		color_name[32];
	char		mode_name[32];
	uint8_t		brightness;
	uint8_t		fade_in;
	uint8_t		fade_out;
	uint32_t	current;
	uint32_t	timeout;
	bool		strobe;
	uint16_t	time_on_ms;
	uint16_t	time_off_ms;
	uint32_t	intensity_min_uA;
	uint32_t	intensity_max_uA;
	uint32_t	intensity_step_uA;
	uint32_t	timeout_min_us;
	uint32_t	timeout_max_us;
	uint32_t	timeout_step_us;
};

struct gb_light {
	uint8_t		id;
	uint8_t		channel_count;
	char		name[32];
	struct gb_channel	*channels;
};

static struct gb_light *gbl[LIGHTS_COUNT];

#define GB_CHANNEL_MODE_VENDOR	0x00100000

static const struct gb_channel channel_rgb = {
	.mode		=	GB_CHANNEL_MODE_VENDOR,
	.flags		=	(GB_LIGHT_CHANNEL_MULTICOLOR |
				 GB_LIGHT_CHANNEL_BLINK |
				 GB_LIGHT_CHANNEL_FADER),
	.max_brightness	=	255,
	.color_name	=	"rgb",
	.mode_name	=	"vendor_notification",
};

static const struct gb_channel channel_green = {
	.flags		=	GB_LIGHT_CHANNEL_BLINK,
	.max_brightness	=	255,
	.color		=	0x0000FF00,
	.color_name	=	"green",
};

static const struct gb_channel channel_yellow = {
	.flags		=	0,
	.max_brightness	=	1,
	.color		=	0x00FFFF00,
	.color_name	=	"yellow",
};

static const struct gb_channel channel_red = {
	.flags		=	0,
	.max_brightness	=	255,
	.color		=	0x00FF0000,
	.color_name	=	"red",
};

static const struct gb_channel channel_blue = {
	.flags		=	0,
	.max_brightness	=	255,
	.color		=	0x000000FF,
	.color_name	=	"blue",
};

static const struct gb_channel channel_flash = {
	.mode		=	GB_CHANNEL_MODE_FLASH,
	.flags		=	0,
	.max_brightness	=	255,
	.color_name	=	"white",
	.mode_name	=	"flash",
	/* example values taken from as3645a */
	.intensity_min_uA	=	200000,
	.intensity_max_uA	=	4000000,
	.intensity_step_uA	=	20000,
	.timeout_min_us		=	100000,
	.timeout_max_us		=	800000,
	.timeout_step_us	=	50000,
};

static const struct gb_channel channel_indicator = {
	.mode		=	GB_CHANNEL_MODE_INDICATOR,
	.flags		=	0,
	.max_brightness	=	255,
	.color_name	=	"red",
	.color		=	0x00FF0000,
	.mode_name	=	"indicator",
	/* example values taken from as3645a */
	.intensity_min_uA	=	0,
	.intensity_max_uA	=	10000,
	.intensity_step_uA	=	2500,
};

static const struct gb_channel channel_torch = {
	.mode		=	GB_CHANNEL_MODE_TORCH,
	.flags		=	0,
	.max_brightness	=	255,
	.color_name	=	"white",
	.color		=	0x00FF0000,
	.mode_name	=	"torch",
	/* example values taken from as3645a */
	.intensity_min_uA	=	20000,
	.intensity_max_uA	=	160000,
	.intensity_step_uA	=	20000,
};

#define define_get_channel(__type)					\
static struct gb_channel *_get_channel_##__type(struct op_msg *op_req)	\
{									\
	uint8_t light_id;						\
	uint8_t channel_id;						\
	struct gb_light *light = NULL;					\
									\
	light_id = op_req->lights_glc_##__type##_req.light_id;		\
	light = gbl[light_id];						\
	channel_id = op_req->lights_glc_conf_req.channel_id;		\
	return &light->channels[channel_id];				\
}

#define get_channel(__req, __type) _get_channel_##__type(__req)

define_get_channel(conf);
define_get_channel(bright);
define_get_channel(blink);
define_get_channel(fconf);
define_get_channel(fint);
define_get_channel(ftimeout);
define_get_channel(fstrobe);
define_get_channel(fade);
define_get_channel(color);

static void light_example_channels_init(struct gb_light *light)
{
	int i = 0;

	light->channel_count = 4;
	light->channels = calloc(light->channel_count,
				 sizeof(struct gb_channel));

	/* channel 0 */
	light->channels[i] = channel_rgb;
	i++;

	/* channel 1 */
	light->channels[i] = channel_green;
	light->channels[i].mode = GB_CHANNEL_MODE_BATTERY;
	snprintf(light->channels[i].mode_name,
		 sizeof(light->channels[i].mode_name), "battery");
	i++;

	/* channel 2 */
	light->channels[i] = channel_yellow;
	light->channels[i].mode = GB_CHANNEL_MODE_BATTERY;
	snprintf(light->channels[i].mode_name,
		 sizeof(light->channels[i].mode_name), "battery");
	i++;

	/* channel 3 */
	light->channels[i] = channel_red;
	light->channels[i].mode = GB_CHANNEL_MODE_BATTERY;
	snprintf(light->channels[i].mode_name,
		 sizeof(light->channels[i].mode_name), "battery");
	i++;
}

static void light_example_channels_flash_init(struct gb_light *light)
{
	int i = 0;

	light->channel_count = 4;
	light->channels = calloc(light->channel_count,
				 sizeof(struct gb_channel));

	/* channel 0 */
	light->channels[i] = channel_flash;
	i++;

	/* channel 1 */
	light->channels[i] = channel_green;
	light->channels[i].mode = GB_CHANNEL_MODE_NOTIFICATION;
	snprintf(light->channels[i].mode_name,
		 sizeof(light->channels[i].mode_name), "notification");
	i++;

	/* channel 2 */
	light->channels[i] = channel_indicator;
	i++;

	/* channel 3 */
	light->channels[i] = channel_torch;
	i++;
}

static struct gb_light *light_init(uint8_t id)
{
	struct gb_light *light;

	light = calloc(1, sizeof(*light));

	light->id = id;
	snprintf(light->name, sizeof(light->name), "gbsim%d", id);

	switch (id) {
	case 0:
		light_example_channels_init(light);
		break;
	case 1:
		light_example_channels_init(light);
		break;
	case 2:
		light_example_channels_flash_init(light);
		break;
	default:
		light_example_channels_init(light);
		break;
	}

	return light;
}

static ssize_t lights_send_event(struct op_msg *op_req, uint16_t hd_cport_id,
				 uint8_t light_id, uint8_t event)
{
	size_t payload_size = sizeof(struct gb_lights_event_request);
	uint16_t message_size;

	op_req->lights_gl_event_req.event = event;
	op_req->lights_gl_event_req.light_id = light_id;

	gbsim_debug("Module -> AP Light_Id %hu Lights protocol event request\n",
		    light_id);

	message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
	return send_request(hd_cport_id, op_req, message_size, 0,
			    GB_LIGHTS_TYPE_EVENT);
}

int lights_handler(struct gbsim_cport *cport, void *rbuf,
		   size_t rsize, void *tbuf, size_t tsize)
{
	struct gb_operation_msg_hdr *oph;
	struct op_msg *op_req = rbuf;
	ssize_t ret = 0;
	struct op_msg *op_rsp;
	struct gb_light *light = NULL;
	struct gb_channel *channel = NULL;
	size_t payload_size = 0;
	uint16_t message_size;
	uint16_t hd_cport_id = cport->hd_cport_id;
	uint8_t light_id = 0;
	int i;

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct gb_operation_msg_hdr *)&op_req->header;

	switch (oph->type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(struct gb_protocol_version_response);
		op_rsp->pv_rsp.major = GB_LIGHTS_VERSION_MAJOR;
		op_rsp->pv_rsp.minor = GB_LIGHTS_VERSION_MINOR;
		/* init lights only after we get a version request */
		for (i = 0; i < LIGHTS_COUNT; i++)
			gbl[i] = light_init(i);
		break;
	case GB_LIGHTS_TYPE_GET_LIGHTS:
		payload_size = sizeof(struct gb_lights_get_lights_response);

		op_rsp->lights_gl_rsp.lights_count = LIGHTS_COUNT;
		break;
	case GB_LIGHTS_TYPE_GET_LIGHT_CONFIG:
		payload_size = sizeof(struct gb_lights_get_light_config_response);
		light_id = op_req->lights_gl_conf_req.id;
		light = gbl[light_id];

		op_rsp->lights_gl_conf_rsp.channel_count = light->channel_count;
		memcpy(op_rsp->lights_gl_conf_rsp.name, light->name,
		       sizeof(op_rsp->lights_gl_conf_rsp.name));
		break;
	case GB_LIGHTS_TYPE_GET_CHANNEL_CONFIG:
		payload_size = sizeof(struct gb_lights_get_channel_config_response);
		channel = get_channel(op_req, conf);

		op_rsp->lights_glc_conf_rsp.max_brightness =
			channel->max_brightness;
		op_rsp->lights_glc_conf_rsp.flags = htole32(channel->flags);
		op_rsp->lights_glc_conf_rsp.color = htole32(channel->color);
		op_rsp->lights_glc_conf_rsp.mode = htole32(channel->mode);

		memcpy(op_rsp->lights_glc_conf_rsp.color_name,
		       channel->color_name, sizeof(channel->color_name));
		memcpy(op_rsp->lights_glc_conf_rsp.mode_name,
		       channel->mode_name, sizeof(channel->mode_name));
		break;
	case GB_LIGHTS_TYPE_SET_BRIGHTNESS:
		/*
		 * we will need light_id for test hack bellow to send a
		 * reconfigure event
		 */
		light_id = op_req->lights_glc_bright_req.light_id;
		channel = get_channel(op_req, bright);

		channel->brightness = op_req->lights_glc_bright_req.brightness;
		break;
	case GB_LIGHTS_TYPE_SET_FADE:
		channel = get_channel(op_req, fade);

		channel->fade_in = op_req->lights_glc_fade_req.fade_in;
		channel->fade_out = op_req->lights_glc_fade_req.fade_out;
		break;
	case GB_LIGHTS_TYPE_SET_COLOR:
		channel = get_channel(op_req, color);

		channel->color = le32toh(op_req->lights_glc_color_req.color);
		break;
	case GB_LIGHTS_TYPE_SET_BLINK:
		channel = get_channel(op_req, blink);

		channel->time_on_ms = le16toh(op_req->lights_glc_blink_req.time_on_ms);
		channel->time_off_ms = le16toh(op_req->lights_glc_blink_req.time_off_ms);
		break;
	case GB_LIGHTS_TYPE_GET_CHANNEL_FLASH_CONFIG:
		payload_size = sizeof(struct gb_lights_get_channel_flash_config_response);
		channel = get_channel(op_req, fconf);

		op_rsp->lights_glc_fconf_rsp.intensity_min_uA =
			channel->intensity_min_uA;
		op_rsp->lights_glc_fconf_rsp.intensity_max_uA =
			channel->intensity_max_uA;
		op_rsp->lights_glc_fconf_rsp.intensity_step_uA =
			channel->intensity_step_uA;

		op_rsp->lights_glc_fconf_rsp.timeout_min_us =
			channel->timeout_min_us;
		op_rsp->lights_glc_fconf_rsp.timeout_max_us =
			channel->timeout_max_us;
		op_rsp->lights_glc_fconf_rsp.timeout_step_us =
			channel->timeout_step_us;
		break;
	case GB_LIGHTS_TYPE_SET_FLASH_INTENSITY:
		channel = get_channel(op_req, fint);

		channel->current = le32toh(op_req->lights_glc_fint_req.intensity_uA);
		break;
	case GB_LIGHTS_TYPE_SET_FLASH_TIMEOUT:
		channel = get_channel(op_req, ftimeout);

		channel->timeout = le32toh(op_req->lights_glc_ftimeout_req.timeout_us);
		break;
	case GB_LIGHTS_TYPE_SET_FLASH_STROBE:
		channel = get_channel(op_req, fstrobe);

		channel->strobe = op_req->lights_glc_fstrobe_req.state ? true : false;
		break;
	}

	/* send response */
	message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
	ret = send_response(hd_cport_id, op_rsp, message_size,
				oph->operation_id, oph->type,
				PROTOCOL_STATUS_SUCCESS);

	/* Test hack: send release event if brightness is set to 254 */
	if (oph->type == GB_LIGHTS_TYPE_SET_BRIGHTNESS &&
	    channel && channel->brightness == 254) {
		return lights_send_event(op_req, hd_cport_id, light_id,
					 GB_LIGHTS_LIGHT_CONFIG);
	}

	return ret;
}

char *lights_get_operation(uint8_t type)
{
	switch (type) {
	case GB_REQUEST_TYPE_INVALID:
		return "GB_LIGHTS_TYPE_INVALID";
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		return "GB_LIGHTS_TYPE_PROTOCOL_VERSION";
	case GB_LIGHTS_TYPE_GET_LIGHTS:
		return "GB_LIGHTS_TYPE_GET_LIGHTS";
	case GB_LIGHTS_TYPE_GET_LIGHT_CONFIG:
		return "GB_LIGHTS_TYPE_GET_LIGHT_CONFIG";
	case GB_LIGHTS_TYPE_GET_CHANNEL_CONFIG:
		return "GB_LIGHTS_TYPE_GET_CHANNEL_CONFIG";
	case GB_LIGHTS_TYPE_GET_CHANNEL_FLASH_CONFIG:
		return "GB_LIGHTS_TYPE_GET_CHANNEL_FLASH_CONFIG";
	case GB_LIGHTS_TYPE_SET_BRIGHTNESS:
		return "GB_LIGHTS_TYPE_SET_BRIGHTNESS";
	case GB_LIGHTS_TYPE_SET_BLINK:
		return "GB_LIGHTS_TYPE_SET_BLINK";
	case GB_LIGHTS_TYPE_SET_COLOR:
		return "GB_LIGHTS_TYPE_SET_COLOR";
	case GB_LIGHTS_TYPE_SET_FADE:
		return "GB_LIGHTS_TYPE_SET_FADE";
	case GB_LIGHTS_TYPE_EVENT:
		return "GB_LIGHTS_TYPE_EVENT";
	case GB_LIGHTS_TYPE_SET_FLASH_INTENSITY:
		return "GB_LIGHTS_TYPE_SET_FLASH_INTENSITY";
	case GB_LIGHTS_TYPE_SET_FLASH_STROBE:
		return "GB_LIGHTS_TYPE_SET_FLASH_STROBE";
	case GB_LIGHTS_TYPE_SET_FLASH_TIMEOUT:
		return "GB_LIGHTS_TYPE_SET_FLASH_TIMEOUT";
	case GB_LIGHTS_TYPE_GET_FLASH_FAULT:
		return "GB_LIGHTS_TYPE_GET_FLASH_FAULT";
	default:
		return "(Unknown operation)";
	}
}
