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

static struct greybus_manifest_header *get_manifest_blob(char *mnfs)
{
	struct greybus_manifest_header *mh;
	int mnf_fd;
	ssize_t n;
	__le16 file_size;
	uint16_t size;

	if ((mnf_fd = open(mnfs, O_RDONLY)) < 0) {
		gbsim_error("failed to open manifest blob %s\n", mnfs);
		return NULL;
	}

	/* First just get the size */
	if ((n = read(mnf_fd, &file_size, 2)) != 2) {
		gbsim_error("failed to read manifest size, read %zd\n", n);
		goto out;
	}
	size = le16toh(file_size);

	/* Size has to cover at least itself */
	if (size < 2) {
		gbsim_error("bad manifest size %hu\n", size);
		goto out;
	}

	/* Allocate a big enough buffer */
	if (!(mh = malloc(size))) {
		gbsim_error("failed to allocate manifest buffer\n");
		goto out;
	}

	/* Now go back and read the whole thing */
	if (lseek(mnf_fd, 0, SEEK_SET)) {
		gbsim_error("failed to seek to front of manifest\n");
		goto out_free;
	}
	if (read(mnf_fd, mh, size) != size) {
		gbsim_error("failed to read manifest\n");
		goto out_free;
	}
	close(mnf_fd);

	return mh;
out_free:
	free(mh);
out:
	close(mnf_fd);

	return NULL;
}

static int get_interface_id_from_fname(char *fname)
{
	char *iid_str;
	char tmp[256];

	/* if manifest name start with IID fetch the interface from there */
	if (fname) {
		strcpy(tmp, fname);
		iid_str = strtok(tmp, "-");
		if (!strncmp(iid_str, "IID", 3))
			return strtol(iid_str + 3, NULL, 0);
	}

	return -ENOENT;
}

/* djb2 string hash function */
static uint32_t hash_filename(char *fname)
{
	uint32_t hash = 5381;
	int c;

	while ((c = *fname++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}

static void *inotify_thread(void *param)
{
	char buffer[16 * INOTIFY_EVENT_BUF];
	ssize_t length;
	struct gbsim_svc *svc = param;
	struct gbsim_interface *intf;
	uint32_t hash;
	int intf_id;
	int i;

	(void) param;
	do {
		size_t size;

		length = read(notify_fd, buffer, sizeof(buffer));
		if (length < 0) {
			gbsim_error("inotify read: %s\n", strerror(errno));
			return NULL;
		}
		for (i = 0; i < length; i += size) {
			struct inotify_event *event = (struct inotify_event *)&buffer[i];
			size = sizeof(*event);
			if (length - i < size) {
				gbsim_error("inotify: partial event: %zd < %zu\n",
					length - i, size);
				return NULL;
			}

			if (!event->len)
				continue;

			size += event->len;
			if (i + size > length) {
				gbsim_error("inotify: short event: %zd < %zu\n",
					length - i, size);
				return NULL;
			}

			if (event->mask & IN_CLOSE_WRITE) {
				char mnfs[256];
				struct greybus_manifest_header *mh;
				strcpy(mnfs, root);
				strcat(mnfs, "/");
				strcat(mnfs, event->name);

				/* get interface id by filename or next available */
				intf_id = get_interface_id_from_fname(event->name);
				if (intf_id < 0)
					intf_id = svc_get_next_intf_id(svc);

				/* allocate interface with given interface id */
				intf = interface_alloc(svc, intf_id);
				if (!intf)
					return false;

				hash = hash_filename(event->name);
				intf->manifest_fname_hash = hash;

				mh = get_manifest_blob(mnfs);
				if (mh) {
					manifest_parse(svc, intf_id, mh,
						       le16toh(mh->size));

					gbsim_info("%s Interface %d inserted\n",
						   event->name, intf_id);

					svc_request_send(GB_SVC_TYPE_MODULE_INSERTED,
							 intf_id);
				} else
					gbsim_error("missing manifest blob, no hotplug event sent\n");
			} else if (event->mask & IN_DELETE) {
				/* get interface by filename hash */
				hash = hash_filename(event->name);

				intf = interface_get_by_hash(svc, hash);
				if (!intf) {
					gbsim_error("interface not found for file: %s\n",
						    event->name);
					return NULL;
				}

				svc_request_send(GB_SVC_TYPE_MODULE_REMOVED,
						 intf->interface_id);
				gbsim_info("%s interface removed\n", event->name);
			}
		}
	} while (length >= 0);

	return NULL;
}

int inotify_start(struct gbsim_svc *svc, char *base_dir)
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

	ret = pthread_create(&inotify_pthread, NULL, inotify_thread, svc);
	if (ret < 0) {
		perror("can't create inotify thread");
		exit(EXIT_FAILURE);
	}

	return 0;
}
