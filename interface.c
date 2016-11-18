/*
 * Greybus Simulator
 *
 * Copyright 2016 Rui Miguel Silva <rmfrfs@gmail.com>
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/queue.h>

#include "gbsim.h"

struct gbsim_interface *interface_get_by_id(struct gbsim_svc *svc, uint8_t id)
{
	struct gbsim_interface *intf;

	TAILQ_FOREACH(intf, &svc->intfs, intf_node)
		if (intf->interface_id == id)
			return intf;

	return NULL;
}

void interface_free(struct gbsim_svc *svc, struct gbsim_interface *intf)
{
	struct gbsim_connection *connection;

	gbsim_debug("free interface %u\n", intf->interface_id);
	TAILQ_FOREACH(connection, &intf->connections, cnode)
		free_connection(connection);

	TAILQ_REMOVE(&svc->intfs, intf, intf_node);
	free(intf->manifest);
	free(intf);
}

struct gbsim_interface *interface_alloc(struct gbsim_svc *svc, uint8_t id)
{
	struct gbsim_interface *intf;

	intf = interface_get_by_id(svc, id);
	if (intf) {
		gbsim_error("allocated an already existent interface %u\n", id);
		return intf;
	}

	intf = calloc(1, sizeof(*intf));
	if (!intf)
		return NULL;

	TAILQ_INIT(&intf->connections);
	intf->interface_id = id;

	intf->svc = svc;

	TAILQ_INSERT_TAIL(&svc->intfs, intf, intf_node);

	return intf;
}
