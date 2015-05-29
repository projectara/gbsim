/*
 * Greybus Simulator
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <errno.h>
#include <stdio.h>
#include <usbg/usbg.h>

#include "gbsim.h"

#define VENDOR		0xffff
#define PRODUCT		0x0001

int gadget_create(usbg_state **s, usbg_gadget **g)
{
	usbg_config *c;
	usbg_function *f;
	int ret = -EINVAL;
	int usbg_ret;

	usbg_gadget_attrs g_attrs = {
			0x0200, /* bcdUSB */
			0x00, /* Defined at interface level */
			0x00, /* subclass */
			0x00, /* device protocol */
			0x0040, /* Max allowed packet size */
			VENDOR,
			PRODUCT,
			0x0001, /* Verson of device */
	};

	usbg_gadget_strs g_strs = {
			"0123456789", /* Serial number */
			"Toshiba", /* Manufacturer */
			"AP Bridge" /* Product string */
	};

	usbg_config_strs c_strs = {
			"AP Bridge"
	};

	usbg_ret = usbg_init("/sys/kernel/config", s);
	if (usbg_ret != USBG_SUCCESS) {
		gbsim_error("Error on USB gadget init\n");
		gbsim_error("Error: %s : %s\n", usbg_error_name(usbg_ret),
			    usbg_strerror(usbg_ret));
		goto out1;
	}

	usbg_ret = usbg_create_gadget(*s, "g1", &g_attrs, &g_strs, g);
	if (usbg_ret != USBG_SUCCESS) {
		gbsim_error("Error on create gadget\n");
		gbsim_error("Error: %s : %s\n", usbg_error_name(usbg_ret),
			    usbg_strerror(usbg_ret));
		goto out2;
	}

	usbg_ret = usbg_create_function(*g, F_FFS, "gbsim", NULL, &f);
	if (usbg_ret != USBG_SUCCESS) {
		gbsim_error("Error creating gbsim function\n");
		gbsim_error("Error: %s : %s\n", usbg_error_name(usbg_ret),
			    usbg_strerror(usbg_ret));
		goto out2;
	}

	usbg_ret = usbg_create_config(*g, 1, NULL, NULL, &c_strs, &c);
	if (usbg_ret != USBG_SUCCESS) {
		gbsim_error("Error creating config\n");
		gbsim_error("Error: %s : %s\n", usbg_error_name(usbg_ret),
			    usbg_strerror(usbg_ret));
		goto out2;
	}

	usbg_ret = usbg_add_config_function(c, "gbsim", f);
	if (usbg_ret != USBG_SUCCESS) {
		gbsim_error("Error adding gbsim configuration\n");
		gbsim_error("Error: %s : %s\n", usbg_error_name(usbg_ret),
			    usbg_strerror(usbg_ret));
		goto out2;
	}

	gbsim_info("USB gadget created\n");

	return 0;

out2:
	gadget_cleanup(*s, *g);

out1:
	return ret;
}

int gadget_enable(usbg_gadget *g)
{
	return usbg_enable_gadget(g, NULL);	
}

int gadget_cleanup(usbg_state *s, usbg_gadget *g)
{
	gbsim_debug("gadget_cleanup\n");

	if (g) {
		usbg_disable_gadget(g);
		usbg_rm_gadget(g, USBG_RM_RECURSE);
	}

	usbg_cleanup(s);

	return 0;
}
