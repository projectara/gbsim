/*
 * Greybus Simulator
 *
 * Copyright 2016 Rui Miguel Silva <rmfrfs@gmail.com>
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <usbg/usbg.h>

#include "gbsim_usb.h"

static usbg_state *s;
static usbg_gadget *g;

void gbsim_usb_cleanup(void)
{
	gadget_cleanup(s, g);
	functionfs_cleanup();
}

int gbsim_usb_init(void)
{
	int ret;

	ret = gadget_create(&s, &g);
	if (ret < 0)
		goto out;

	ret = functionfs_init();
	if (ret < 0)
		goto gadget_cleanup;

	ret = gadget_enable(g);
	if (ret < 0)
		goto functionfs_cleanup;

	return ret;

functionfs_cleanup:
	functionfs_cleanup();
gadget_cleanup:
	gadget_cleanup(s, g);
out:
	return ret;
}
