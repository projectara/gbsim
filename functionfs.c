/*
 * Greybus Simulator
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/usb/functionfs.h>

#include <svc_msg.h>
#include <greybus_manifest.h>

#include "gbsim.h"

/* #define USE_DEPRECATED_DESC_HEAD */

#define FFS_PREFIX	"/dev/ffs-gbsim/"
#define FFS_GBEMU_EP0	FFS_PREFIX"ep0"
#define FFS_GBEMU_SVC	FFS_PREFIX"ep1"
#define FFS_GBEMU_IN	FFS_PREFIX"ep2"
#define FFS_GBEMU_OUT	FFS_PREFIX"ep3"

#define cpu_to_le16(x)  htole16(x)
#define cpu_to_le32(x)  htole32(x)
#define le32_to_cpu(x)  le32toh(x)
#define le16_to_cpu(x)  le16toh(x)

#define STR_INTERFACE	"gbsim"

#define NEVENT		5

#define HS_PAYLOAD_SIZE		(sizeof(struct svc_function_handshake))
#define HS_MSG_SIZE		(sizeof(struct svc_msg_header) +	\
					HS_PAYLOAD_SIZE)
#define HS_VALID(m)							\
	((m->handshake.version_major == GREYBUS_VERSION_MAJOR) &&	\
	(m->handshake.version_minor == GREYBUS_VERSION_MINOR) &&	\
	(m->handshake.handshake_type == SVC_HANDSHAKE_AP_HELLO))

#define CPORT_BUF_SIZE		(sizeof(struct svc_msg) + 64 * 1024)

enum gbsim_state {
	GBEMU_IDLE		= 0,
	GBEMU_HS_COMPLETE	= 1,
};

static int control = -ENXIO;
static int svc_int = -ENXIO;
static int cport_in = -ENXIO;
static int cport_out = -ENXIO;

static pthread_t cport_pthread;

static int state = GBEMU_IDLE;

/* 
 * Descriptors:
 *
 * EP0 [control]	- Ch9 and SVC inbound messages
 * EP1 [interrupt in]	- SVC outbound events/messages
 * EP2 [bulk in]	- CPort outbound messages
 * EP3 [bulk out]	- CPort inbound messages
 */
static const struct {
	struct {
		__le32 magic;
		__le32 length;
#ifndef USE_DEPRECATED_DESC_HEAD
		__le32 flags;
#endif
		__le32 fs_count;
		__le32 hs_count;
	} __attribute__((packed)) header;
	struct {
		struct usb_interface_descriptor intf;
		struct usb_endpoint_descriptor_no_audio svc_in;
		struct usb_endpoint_descriptor_no_audio cport_in;
		struct usb_endpoint_descriptor_no_audio cport_out;
	} __attribute__((packed)) fs_descs, hs_descs;
} __attribute__((packed)) descriptors = {
	.header = {
#ifdef USE_DEPRECATED_DESC_HEAD
		.magic = cpu_to_le32(FUNCTIONFS_DESCRIPTORS_MAGIC),
#else
		.magic = cpu_to_le32(FUNCTIONFS_DESCRIPTORS_MAGIC_V2),
		.flags = cpu_to_le32(FUNCTIONFS_HAS_FS_DESC |
				     FUNCTIONFS_HAS_HS_DESC),
#endif
		.length = cpu_to_le32(sizeof descriptors),
		.fs_count = cpu_to_le32(4),
		.hs_count = cpu_to_le32(4),
	},
	.fs_descs = {
		.intf = {
			.bLength = sizeof descriptors.fs_descs.intf,
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 3,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.svc_in = {
			.bLength = sizeof descriptors.fs_descs.svc_in,
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_INT,
			.bInterval = 10,
			.wMaxPacketSize = 64
		},
		.cport_in = {
			.bLength = sizeof descriptors.fs_descs.cport_in,
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = 64
		},
		.cport_out = {
			.bLength = sizeof descriptors.fs_descs.cport_out,
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 3 | USB_DIR_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = 64
		},
	},
	.hs_descs = {
		.intf = {
			.bLength = sizeof descriptors.hs_descs.intf,
			.bDescriptorType = USB_DT_INTERFACE,
			.bNumEndpoints = 3,
			.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
			.iInterface = 1,
		},
		.svc_in = {
			.bLength = sizeof descriptors.hs_descs.svc_in,
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 1 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_INT,
			.bInterval = 10,
			.wMaxPacketSize = 512,
		},
		.cport_in = {
			.bLength = sizeof descriptors.hs_descs.cport_in,
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 2 | USB_DIR_IN,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = 512,
		},
		.cport_out = {
			.bLength = sizeof descriptors.hs_descs.cport_out,
			.bDescriptorType = USB_DT_ENDPOINT,
			.bEndpointAddress = 3 | USB_DIR_OUT,
			.bmAttributes = USB_ENDPOINT_XFER_BULK,
			.wMaxPacketSize = 512,
		},
	},
};

static const struct {
	struct usb_functionfs_strings_head header;
	struct {
		__le16 code;
		const char str1[sizeof STR_INTERFACE];
	} __attribute__((packed)) lang0;
} __attribute__((packed)) strings = {
	.header = {
		.magic = cpu_to_le32(FUNCTIONFS_STRINGS_MAGIC),
		.length = cpu_to_le32(sizeof strings),
		.str_count = cpu_to_le32(1),
		.lang_count = cpu_to_le32(1),
	},
	.lang0 = {
		cpu_to_le16(0x0409), /* en-us */
		STR_INTERFACE,
	},
};

/*
 * Endpoint handling
 */

static int cport_write(void *buf, size_t length)
{
	int ret = write(cport_in, buf, length);
	if (ret < length)
		gbsim_error("Failed CPort write (%ld bytes) to AP\n", length);

	return ret;
}

static int cport_read(void *buf, size_t length)
{
	int ret = read(cport_out, buf, length);
	if (ret < 0)
		gbsim_error("Failed CPort read (%ld bytes) from AP\n", length);
	else
		gbsim_debug("Successful CPort read (%ld bytes) from AP\n", length);

	return ret;
}

static int svc_int_write(void *buf, size_t length)
{
	return write(svc_int, buf, length);
};

static void send_svc_handshake(void)
{
	uint8_t buf[256];
	struct svc_msg *m = (struct svc_msg *)buf;

	m->header.function_id = SVC_FUNCTION_HANDSHAKE;
	m->header.message_type = SVC_MSG_DATA;
	m->header.payload_length = cpu_to_le16(HS_PAYLOAD_SIZE);
	m->handshake.version_major = 0;
	m->handshake.version_minor = 0;
	m->handshake.handshake_type = SVC_HANDSHAKE_SVC_HELLO;

	svc_int_write(m, HS_MSG_SIZE);
	gbsim_debug("SVC->AP handshake sent\n");
}

void send_hot_plug(char *hpe, int mid)
{
	struct svc_msg *msg = (struct svc_msg *)hpe;
	struct greybus_manifest_header *mh =
		(struct greybus_manifest_header *)(hpe + HP_BASE_SIZE);
	
	msg->header.function_id = SVC_FUNCTION_HOTPLUG;
	msg->header.message_type = SVC_MSG_DATA;
	msg->header.payload_length = mh->size + 2;
	msg->hotplug.hotplug_event = SVC_HOTPLUG_EVENT;
	msg->hotplug.module_id = mid;

	/* Write out hotplug message with manifest payload */
	svc_int_write(hpe, HP_BASE_SIZE + mh->size);

	gbsim_debug("SVC->AP hotplug event (plug) sent\n");
}

void send_hot_unplug(int mid)
{
	struct svc_msg msg;
	
	msg.header.function_id = SVC_FUNCTION_HOTPLUG;
	msg.header.message_type = SVC_MSG_DATA;
	msg.header.payload_length = 2;
	msg.hotplug.hotplug_event = SVC_HOTUNPLUG_EVENT;
	msg.hotplug.module_id = mid;

	/* Write out hotplug message */
	svc_int_write(&msg, HP_BASE_SIZE);

	gbsim_debug("SVC->AP hotplug event (unplug) sent\n");
}

static void cleanup_endpoint(int ep_fd, char *ep_name)
{
	int ret;

	if (ep_fd < 0)
		return;

	ret = ioctl(ep_fd, FUNCTIONFS_FIFO_STATUS);
	if (ret < 0) {
		/* ENODEV reported after disconnect */
		if(errno != ENODEV)
			gbsim_error("get fifo status(%s): %s \n", ep_name, strerror(errno));
	} else if(ret) {
			gbsim_error("%s: unclaimed = %d \n", ep_name, ret);
		if(ioctl(ep_fd, FUNCTIONFS_FIFO_FLUSH) < 0)
			gbsim_error("%s: fifo flush \n", ep_name);
	}

	if(close(ep_fd) < 0)
		gbsim_error("%s: close \n", ep_name);
}

static void cport_thread_cleanup(void *arg)
{
	cleanup_endpoint(svc_int, "svc_int");
	cleanup_endpoint(cport_in, "cport_in");
	cleanup_endpoint(cport_out, "cport_out");
}

static void *cport_thread(void *param)
{
	char *buf;

	/* FIXME: just a placeholder for now */

	pthread_cleanup_push(cport_thread_cleanup, NULL);

	/* alloc send/receive buffers */
	buf = malloc(CPORT_BUF_SIZE);

	do {
		size_t size = cport_read(buf, CPORT_BUF_SIZE);
		if (size < 0)
			continue;
		cport_write(buf, size);
	} while (1);

	pthread_cleanup_pop(1);

	pthread_exit(NULL);
}

static int enable_endpoints(void)
{
	int ret;

	/* Start SVC/CPort endpoints here */
	gbsim_debug("Start SVC/CPort endpoints\n");

	svc_int = open(FFS_GBEMU_SVC, O_RDWR);
	if (svc_int < 0)
		return svc_int;

	cport_in = open(FFS_GBEMU_IN, O_RDWR);
	if (cport_in < 0)
		return cport_in;

	cport_out = open(FFS_GBEMU_OUT, O_RDWR);
	if (cport_out < 0)
		return cport_out;

	ret = pthread_create(&cport_pthread, NULL, cport_thread, NULL);
	if (ret < 0) {
		perror("can't create cport thread");
		return ret;
	}

	send_svc_handshake();

	return 0;
}

static void disable_endpoints(void)
{
	gbsim_debug("Disable SVC/CPort endpoints\n");

	if (cport_in < 0 || cport_out < 0)
		return;

	pthread_cancel(cport_pthread);
	pthread_join(cport_pthread, NULL);

	state = GBEMU_IDLE;

	close(cport_out);
	cport_out = -EINVAL;
	close(cport_in);
	cport_in = -EINVAL;

	close(svc_int);
	svc_int = -EINVAL;
}

static void handle_setup(const struct usb_ctrlrequest *setup)
{
	uint8_t buf[256];
	struct svc_msg *m = (struct svc_msg *)buf;
	int count;

	if (verbose) {
		gbsim_debug("AP->AP Bridge setup message:\n");
		gbsim_debug("  bRequestType = %02x\n", setup->bRequestType);
		gbsim_debug("  bRequest     = %02x\n", setup->bRequest);
		gbsim_debug("  wValue       = %04x\n", le16_to_cpu(setup->wValue));
		gbsim_debug("  wIndex       = %04x\n", le16_to_cpu(setup->wIndex));
		gbsim_debug("  wLength      = %04x\n", le16_to_cpu(setup->wLength));
	}

	if ((setup->bRequest == 0x01) &&
	    (setup->bRequestType & USB_TYPE_VENDOR)) {

		if ((count = read(control, buf, setup->wLength)) < 0) {
			perror("SVC message data not present");
			return;
		}

		if (verbose) {
			int i;
			gbsim_debug("AP->SVC message:\n  ");
			for (i = 0; i < count; i++)
				fprintf(stdout, "%02x ", buf[i]);
			fprintf(stdout, "\n");
		}

		if (m->header.message_type == SVC_MSG_ERROR) {
			perror("SVC message session error");
			return;
		}

		switch (m->header.function_id) {
		case SVC_FUNCTION_HANDSHAKE:
			if (HS_VALID(m)) {
				gbsim_info("AP handshake complete\n");
				state = GBEMU_HS_COMPLETE;
			} else
				perror("AP handshake invalid");
			break;

		default:
			perror("SVC message ID invalid");
			return;
		}
		
	}
}

static int read_control(void)
{
	struct usb_functionfs_event event[NEVENT];
	int i, nevent, ret;

	static const char *const names[] = {
		[FUNCTIONFS_BIND] = "BIND",
		[FUNCTIONFS_UNBIND] = "UNBIND",
		[FUNCTIONFS_ENABLE] = "ENABLE",
		[FUNCTIONFS_DISABLE] = "DISABLE",
		[FUNCTIONFS_SETUP] = "SETUP",
		[FUNCTIONFS_SUSPEND] = "SUSPEND",
		[FUNCTIONFS_RESUME] = "RESUME",
	};

	ret = read(control, &event, sizeof(event));
	if (ret < 0) {
		if (errno == EAGAIN) {
			sleep(1);
			return ret;
		}
		perror("ep0 read after poll");
		return ret;
	}
	nevent = ret/ sizeof event[0];

	for (i = 0; i < nevent; i++) {
		gbsim_debug("event %s,%d\n", names[event->type], event[i].type);

		switch (event[i].type) {
		case FUNCTIONFS_BIND:
			break;
		case FUNCTIONFS_UNBIND:
			break;
		case FUNCTIONFS_ENABLE:
			enable_endpoints();
			break;
		case FUNCTIONFS_DISABLE:
			disable_endpoints();
			break;
		case FUNCTIONFS_SETUP:
			handle_setup(&event[i].u.setup);
			break;
		case FUNCTIONFS_SUSPEND:
			break;
		case FUNCTIONFS_RESUME:
			break;
		default:
			gbsim_error("unknown event %d\n", event[i].type);
		}
	}

	return ret;
}

static void functionfs_init_gb(void)
{
	int ret;

	control = open(FFS_GBEMU_EP0, O_RDWR);
	if (control < 0) {
		perror(FFS_GBEMU_EP0);
		control = -errno;
		return;
	}

	ret = write(control, &descriptors, sizeof(descriptors));
	if (ret < 0) {
		perror("write dev descriptors");
		close(control);
		control = -errno;
		return;
	}

	ret = write(control, &strings, sizeof(strings));
	if (ret < 0) {
		perror("write dev strings");
		close(control);
		control = -errno;
		return;
	}

	return;
}

int functionfs_loop(void)
{
	struct pollfd ep_poll[1];
	int ret;

	do {
		/* Always listen on control */
		ep_poll[0].fd = control;
		ep_poll[0].events = POLLIN | POLLHUP;

		ret = poll(ep_poll, 1, -1);
		if (ret < 0) {
			perror("poll");
			break;
		}

		/* TODO: What to do with HUP? */
		if (ep_poll[0].revents & POLLIN) {
			ret = read_control();
			if (ret < 0) {
				if (errno == EAGAIN)
					continue;
				goto done;
			}
		}
	} while (1);

	return 0;

done:
	return ret;
}

int functionfs_init(void)
{
	/* Mount functionfs */
	mkdir(FFS_PREFIX, S_IRWXU|S_IRWXG|S_IRWXO);
	mount("gbsim", FFS_PREFIX, "functionfs", 0, NULL);

	/* Configure the Greybus emulator */
	functionfs_init_gb();

	return 0;
}

int functionfs_cleanup(void)
{
	cport_thread_cleanup(NULL);

	return 0;
}
