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
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <linux/types.h>

#include "gbsim.h"

#define MAX_NAME 256
#define INOTIFY_EVENT_SIZE  ( sizeof(struct inotify_event) )
#define INOTIFY_EVENT_BUF   ( INOTIFY_EVENT_SIZE + MAX_NAME + 1 )

static pthread_t inotify_pthread;
int notify_fd = -ENXIO;
static char root[256];

static char *get_manifest_blob(char *mnfs)
{
	int mnf_fd, n;
	uint16_t size;
	char *hpb;
	struct greybus_manifest_header *mh;


	if (!(hpb = malloc(HP_BASE_SIZE + 64 * 1024))) {
		gbsim_error("failed to allocate hotplug buffer\n");
		goto out;
	}

	if ((mnf_fd = open(mnfs, O_RDONLY)) < 0) {
		gbsim_error("failed to open manifest blob %s\n", mnfs);
		return NULL;
	}

	if ((n = read(mnf_fd, &size, 2)) != 2) {
		gbsim_error("failed to read manifest size, read %d\n", n);
		return NULL;
	}
	lseek(mnf_fd, 0, SEEK_SET);

	mh = (struct greybus_manifest_header *)(hpb + HP_BASE_SIZE);
	if (read(mnf_fd, mh, size) != size) {
		gbsim_error("failed to read manifest\n");
		return NULL;
	}

out:
	return hpb;
}

static void parse_manifest_blob(char *hpe)
{
	struct greybus_manifest_header *mh =
		(struct greybus_manifest_header *)(hpe + HP_BASE_SIZE);

	manifest_parse(mh, le16toh(mh->size));
}

static int get_interface_id(char *fname)
{
	char *iid_str;
	int iid = 0;
	char tmp[256];

	strcpy(tmp, fname);
	iid_str = strtok(tmp, "-");
	if (!strncmp(iid_str, "MID", 3))
		iid = strtol(iid_str+3, NULL, 0);

	return iid;
}

static void *inotify_thread(void *param)
{
	char buffer[16 * INOTIFY_EVENT_BUF];
	int i, length;
	(void) param;

	do {
		length = read(notify_fd, buffer, sizeof(buffer));

		if (length < 0)
			gbsim_error("inotify read: %s\n", strerror(errno));

		i = 0;
		while (i < length) {
			struct inotify_event *event = (struct inotify_event *)&buffer[i];
			if (event->len) {
				if (event->mask & IN_CLOSE_WRITE) {
					char mnfs[256];
					char *hpe;
					strcpy(mnfs, root);
					strcat(mnfs, "/");
					strcat(mnfs, event->name);
					hpe = get_manifest_blob(mnfs);
					if (hpe) {
						parse_manifest_blob(hpe);
						int iid = get_interface_id(event->name);
						if (iid > 0) {
							gbsim_info("%s Interface inserted\n", event->name);
							send_hot_plug(hpe, iid);
							/*
							 * FIXME: hardcoded
							 * device ID
							 */
							send_link_up(iid, 2);
						} else
							gbsim_error("invalid interface ID, no hotplug plug event sent\n");
					} else
						gbsim_error("missing manifest blob, no hotplug event sent\n");
				}
				else if (event->mask & IN_DELETE) {
					int iid = get_interface_id(event->name);
					if (iid > 0) {
						send_hot_unplug(get_interface_id(event->name));
						gbsim_info("%s interface removed\n", event->name);
					} else
						gbsim_error("invalid interface ID, no hotplug unplug event sent\n");
				}
			}
			i += INOTIFY_EVENT_SIZE + event->len;
		}
	} while (length >= 0);

	return NULL;
}

int inotify_start(char *base_dir)
{
	int ret;
	struct stat root_stat;
	int notify_wd;

	/* Our inotify directory */
	strcpy(root, base_dir);
	strcat(root, "/");
	strcat(root, "hotplug-module");

	ret = stat(root, &root_stat);
	if (ret < 0 || !S_ISDIR(root_stat.st_mode) || access(root, R_OK|W_OK) < 0) {
		gbsim_error("invalid base directory %s\n", root);
		exit(EXIT_FAILURE);
	}

	if ((notify_fd = inotify_init()) < 0)
		perror("inotify init failed");

	if ((notify_wd = inotify_add_watch(notify_fd, root, IN_CLOSE_WRITE|IN_DELETE)) < 0)
		perror("inotify add watch failed");

	ret = pthread_create(&inotify_pthread, NULL, inotify_thread, NULL);
	if (ret < 0) {
		perror("can't create inotify thread");
		exit(EXIT_FAILURE);
	}

	return 0;
}
