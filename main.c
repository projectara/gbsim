/*
 * Greybus Simulator
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>

#include <usbg/usbg.h>

#include "gbsim.h"

int verbose = 1;

static usbg_state *s;
static usbg_gadget *g;

static struct sigaction sigact;

static void cleanup(void)
{
	printf("cleaning up\n");
	sigemptyset(&sigact.sa_mask);

	functionfs_cleanup();

	gadget_cleanup(s, g);
}

static void signal_handler(int sig){
    if (sig == SIGINT)
	cleanup();
}

static void signals_init(void) {
    sigact.sa_handler = signal_handler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGINT, &sigact, (struct sigaction *)NULL);
}

int main(int argc, char *argv[])
{
	int ret = -EINVAL;

	/* parse cmdline options */

	signals_init();

	ret = gadget_create(&s, &g);

	ret = functionfs_init();

	ret = gadget_enable(g);

	/* arg1 is our inotify hotplug base directory */
	ret = inotify_start(argv[1]);

	/* FIXME: init subdev handling here */

	ret = functionfs_loop();

	return ret;
}

