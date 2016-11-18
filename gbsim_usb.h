/*
 * Greybus Simulator
 *
 * Copyright 2016 Rui Miguel Silva <rmfrfs@gmail.com>
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#ifndef __GBSIM_USB_H
#define __GBSIM_USB_H

#include <usbg/usbg.h>

int gadget_create(usbg_state **, usbg_gadget **);
int gadget_enable(usbg_gadget *);
void gadget_cleanup(usbg_state *, usbg_gadget *);

int functionfs_init(void);
int functionfs_loop(void);
int functionfs_cleanup(void);
void cleanup_endpoint(int, char *);

int gbsim_usb_init(void);
void gbsim_usb_cleanup(void);

#endif
