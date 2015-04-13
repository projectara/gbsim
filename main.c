/*
 * Greybus Simulator
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <usbg/usbg.h>

#include "gbsim.h"

int bbb_backend = 0;
int i2c_adapter = 0;
int verbose = 0;

static usbg_state *s;
static usbg_gadget *g;

static struct sigaction sigact;

struct gbsim_info info;

static void cleanup(void)
{
	printf("cleaning up\n");
	sigemptyset(&sigact.sa_mask);

	gadget_cleanup(s, g);

	functionfs_cleanup();
}

static void signal_handler(int sig)
{
	if (sig == SIGINT || sig == SIGHUP || sig == SIGTERM)
		cleanup();
}

static void signals_init(void)
{
	sigact.sa_handler = signal_handler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, (struct sigaction *)NULL);
	sigaction(SIGHUP, &sigact, (struct sigaction *)NULL);
	sigaction(SIGTERM, &sigact, (struct sigaction *)NULL);
}

int main(int argc, char *argv[])
{
	int ret = -EINVAL;
	int o;
	char *hotplug_basedir;

	while ((o = getopt(argc, argv, "bh:i:v")) != -1) {
		switch (o) {
		case 'b':
			bbb_backend = 1;
			printf("bbb_backend %d\n", bbb_backend);
			break;
		case 'h':
			hotplug_basedir = optarg;
			printf("hotplug_basedir %s\n", hotplug_basedir);
			break;
		case 'i':
			i2c_adapter = atoi(optarg);
			printf("i2c_adapter %d\n", i2c_adapter);
			break;
		case 'v':
			verbose = 1;
			printf("verbose %d\n", verbose);
			break;
		case '?':
			if (optopt == 'i')
				gbsim_error("-%c needs an i2c adapter argument\n", optopt);
			else if (isprint(optopt))
				gbsim_error("unknown option -%c'\n", optopt);
			else
				gbsim_error("unknown option character `\\x%x'\n", optopt);
			return 1;
		default:
			abort();
		}
	}

	signals_init();

	TAILQ_INIT(&info.cports);

	ret = gadget_create(&s, &g);
	if (ret < 0)
		goto out;

	ret = functionfs_init();
	if (ret < 0)
		goto out;

	ret = gadget_enable(g);
	if (ret < 0)
		goto out;

	ret = inotify_start(hotplug_basedir);
	if (ret < 0)
		goto out;

	/* Protocol handlers */
	gpio_init();
	i2c_init();
	i2s_init();

	ret = functionfs_loop();

out:
	return ret;
}

