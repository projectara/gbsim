/*
 * Greybus Simulator
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

/* Required for build, as greybus core uses __packed */
#ifndef __GBSIM_H
#define __GBSIM_H

#define __packed  __attribute__((__packed__))

#include <endian.h>
#include <stdbool.h>
#include <stdio.h>
#include <usbg/usbg.h>

#include <greybus_manifest.h>
#include <greybus_protocols.h>
#include <svc_msg.h>

#ifndef BIT
#define BIT(n)	(1UL << (n))
#endif

/* Wouldn't support types larger than 4 bytes */
#define _ALIGNBYTES		(sizeof(uint32_t) - 1)
#define ALIGN(p)		((typeof(p))(((unsigned)(p) + _ALIGNBYTES) & ~_ALIGNBYTES))

extern int bbb_backend;
extern int i2c_adapter;
extern int uart_portno;
extern int uart_count;
extern int verbose;

/* Matches up with the Greybus Protocol specification document */
#define GREYBUS_VERSION_MAJOR	0x00
#define GREYBUS_VERSION_MINOR	0x01

extern int control;
extern int svc_int;
extern int to_ap;
extern int from_ap;

struct gbsim_cport {
	TAILQ_ENTRY(gbsim_cport) cnode;
	uint16_t id;
	uint16_t hd_cport_id;
	int protocol;
};

struct gbsim_info {
	void *manifest;
	size_t manifest_size;
	TAILQ_HEAD(chead, gbsim_cport) cports;
};

extern struct gbsim_info info;

/* CPorts */

#define PROTOCOL_STATUS_SUCCESS	0x00
#define PROTOCOL_STATUS_INVALID	0x01
#define PROTOCOL_STATUS_NOMEM	0x02
#define PROTOCOL_STATUS_BUSY	0x03
#define PROTOCOL_STATUS_RETRY	0x04
#define PROTOCOL_STATUS_BAD	0xff

struct op_header {
	__le16	size;
	__le16	id;
	__u8	type;
	__u8	result;
	__u8	pad[2];
};

/* Ops */
struct op_msg {
	struct op_header	header;
	union {
		struct gb_protocol_version_response	pv_rsp;
		struct gb_control_get_manifest_size_response control_msize_rsp;
		struct gb_control_get_manifest_response control_manifest_rsp;
		struct gb_gpio_line_count_response	gpio_lc_rsp;
		struct gb_gpio_activate_request		gpio_act_req;
		struct gb_gpio_deactivate_request	gpio_deact_req;
		struct gb_gpio_get_direction_request	gpio_get_dir_req;
		struct gb_gpio_get_direction_response	gpio_get_dir_rsp;
		struct gb_gpio_direction_in_request	gpio_dir_input_req;
		struct gb_gpio_direction_out_request	gpio_dir_output_req;
		struct gb_gpio_get_value_request	gpio_get_val_req;
		struct gb_gpio_get_value_response	gpio_get_val_rsp;
		struct gb_gpio_set_value_request	gpio_set_val_req;
		struct gb_gpio_set_debounce_request	gpio_set_db_req;
		struct gb_gpio_irq_type_request		gpio_irq_type_req;
		struct gb_gpio_irq_mask_request		gpio_irq_mask_req;
		struct gb_gpio_irq_unmask_request	gpio_irq_unmask_req;
		struct gb_gpio_irq_event_request	gpio_irq_event_req;
		struct gb_i2c_functionality_response	i2c_fcn_rsp;
		struct gb_i2c_transfer_request		i2c_xfer_req;
		struct gb_i2c_transfer_response		i2c_xfer_rsp;
		struct gb_pwm_count_response		pwm_cnt_rsp;
		struct gb_pwm_activate_request		pwm_act_req;
		struct gb_pwm_deactivate_request	pwm_deact_req;
		struct gb_pwm_config_request		pwm_cfg_req;
		struct gb_pwm_polarity_request		pwm_pol_req;
		struct gb_pwm_enable_request		pwm_enb_req;
		struct gb_pwm_disable_request		pwm_dis_req;
		struct gb_i2s_mgmt_get_supported_configurations_response i2s_mgmt_get_sup_conf_rsp;
		struct gb_i2s_mgmt_get_processing_delay_response i2s_mgmt_get_proc_delay_rsp;
		struct gb_uart_send_data_request	uart_send_data_req;
		struct gb_uart_recv_data_request	uart_recv_data_rsp;
		struct gb_uart_set_break_request	uart_sb_req;
		struct gb_uart_serial_state_request	uart_ss_resp;
		struct gb_uart_set_line_coding_request	uart_slc_req;
		struct gb_uart_set_control_line_state_request uart_sls_req;
		struct gb_sdio_get_caps_response	sdio_caps_rsp;
		struct gb_sdio_event_request		sdio_event_req;
		struct gb_sdio_command_request		sdio_cmd_req;
		struct gb_sdio_command_response		sdio_cmd_rsp;
		struct gb_sdio_transfer_request		sdio_xfer_req;
		struct gb_sdio_transfer_response	sdio_xfer_rsp;
		struct gb_loopback_transfer_request	loopback_xfer_req;
		struct gb_loopback_transfer_response	loopback_xfer_resp;
	};
};

#define OP_RESPONSE			0x80

/* debug/info/error macros */
#define gbsim_debug(fmt, ...)						\
        do { if (verbose) { fprintf(stdout, "[D] GBSIM: " fmt,  	\
				  ##__VA_ARGS__); fflush(stdout); } } while (0)
#define gbsim_info(fmt, ...)						\
        do { fprintf(stdout, "[I] GBSIM: " fmt, ##__VA_ARGS__); fflush(stdout); } while (0)
#define gbsim_error(fmt, ...)						\
        do { fprintf(stderr, "[E] GBSIM: " fmt, ##__VA_ARGS__); fflush(stderr); } while (0)

static inline void gbsim_dump(void *data, size_t size)
{
	char *buf = data;
	int i;

	fprintf(stdout, "[R] GBSIM: DUMP -> ");
	for (i = 0; i < size; i++)
		fprintf(stdout, "%02x ", buf[i]);
	fprintf(stdout, "\n");
	fflush(stdout);
}

static inline uint8_t cport_to_module_id(uint16_t cport)
{
	/* FIXME can identify based on registered cport module */
	return 1;
}

void allocate_cport(uint16_t cport_id, uint16_t hd_cport_id, int protocol_id);

int gadget_create(usbg_state **, usbg_gadget **);
int gadget_enable(usbg_gadget *);
int gadget_cleanup(usbg_state *, usbg_gadget *);

int functionfs_init(void);
int functionfs_loop(void);
int functionfs_cleanup(void);
void cleanup_endpoint(int, char *);

int inotify_start(char *);

void send_hot_plug(int);
void send_hot_unplug(int);

void send_link_up(int, int);

void *recv_thread(void *);
void recv_thread_cleanup(void *);

int control_handler(uint16_t, uint16_t, void *, size_t, void *, size_t);
char *control_get_operation(uint8_t type);

int gpio_handler(uint16_t, uint16_t, void *, size_t, void *, size_t);
char *gpio_get_operation(uint8_t type);
void gpio_init(void);

int i2c_handler(uint16_t, uint16_t, void *, size_t, void *, size_t);
char *i2c_get_operation(uint8_t type);
void i2c_init(void);

int pwm_handler(uint16_t, uint16_t, void *, size_t, void *, size_t);
char *pwm_get_operation(uint8_t type);
void pwm_init(void);

int sdio_handler(uint16_t, uint16_t, void *, size_t, void *, size_t);
char *sdio_get_operation(uint8_t type);
void sdio_init(void);

int i2s_mgmt_handler(uint16_t, uint16_t, void *, size_t, void *, size_t);
int i2s_data_handler(uint16_t, uint16_t, void *, size_t, void *, size_t);
char *i2s_mgmt_get_operation(uint8_t type);
char *i2s_data_get_operation(uint8_t type);
void i2s_init(void);

int uart_handler(uint16_t, uint16_t, void *, size_t, void *, size_t);
char *uart_get_operation(uint8_t type);
void uart_init(void);
void uart_cleanup(void);

int loopback_handler(uint16_t, uint16_t, void *, size_t, void *, size_t);
char *loopback_get_operation(uint8_t type);
void loopback_init(void);
void loopback_cleanup(void);

bool manifest_parse(void *data, size_t size);
int send_response(struct op_msg *op, uint16_t hd_cport_id,
		   uint16_t message_size, struct op_header *oph,
		   uint8_t result);
int send_request(struct op_msg *op, uint16_t hd_cport_id,
		 uint16_t message_size, uint16_t id, uint8_t type);

#endif /* __GBSIM_H */
