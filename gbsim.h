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

#ifndef BIT
#define BIT(n)	(1UL << (n))
#endif

#include <greybus_manifest.h>
#include <greybus_protocols.h>

/* Wouldn't support types larger than 4 bytes */
#define _ALIGNBYTES		(sizeof(uint32_t) - 1)
#define ALIGN(p)		((typeof(p))(((unsigned)(p) + _ALIGNBYTES) & ~_ALIGNBYTES))

extern int bbb_backend;
extern int i2c_adapter;
extern int uart_portno;
extern int uart_count;
extern int verbose;
extern char *hotplug_basedir;

/* Matches up with the Greybus Protocol specification document */
#define GB_REQUEST_TYPE_PROTOCOL_VERSION 0x01
#define GREYBUS_VERSION_MAJOR	0x00
#define GREYBUS_VERSION_MINOR	0x01

/* Fixed values of AP's interface id and endo id */
#define ENDO_ID 0x4755
#define AP_INTF_ID 0x5

extern int control;
extern int to_ap;
extern int from_ap;

struct gbsim_connection {
	TAILQ_ENTRY(gbsim_connection) cnode;
	uint16_t cport_id;
	uint16_t hd_cport_id;
	int protocol;
};

struct gbsim_interface {
	void *manifest;
	size_t manifest_size;
	TAILQ_HEAD(chead, gbsim_connection) connections;
};

extern struct gbsim_interface interface;

/* CPorts */

#define PROTOCOL_STATUS_SUCCESS	0x00
#define PROTOCOL_STATUS_INVALID	0x01
#define PROTOCOL_STATUS_NOMEM	0x02
#define PROTOCOL_STATUS_BUSY	0x03
#define PROTOCOL_STATUS_RETRY	0x04
#define PROTOCOL_STATUS_BAD	0xff

/* Ops */
struct op_msg {
	struct gb_operation_msg_hdr			header;
	union {
		struct gb_control_version_response	control_version_rsp;
		struct gb_control_get_manifest_size_response control_msize_rsp;
		struct gb_control_get_manifest_response control_manifest_rsp;
		struct gb_protocol_version_response	svc_version_request;
		struct gb_svc_hello_request		hello_request;
		struct gb_svc_intf_device_id_request	svc_intf_device_id_request;
		struct gb_svc_conn_create_request	svc_conn_create_request;
		struct gb_svc_conn_destroy_request	svc_conn_destroy_request;
		struct gb_svc_module_inserted_request	svc_module_inserted_request;
		struct gb_svc_module_removed_request	svc_module_removed_request;
		struct gb_svc_intf_reset_request	svc_intf_reset_request;
		struct gb_svc_dme_peer_get_request	svc_dme_peer_get_request;
		struct gb_svc_dme_peer_get_response	svc_dme_peer_get_response;
		struct gb_svc_dme_peer_set_request	svc_dme_peer_set_request;
		struct gb_svc_dme_peer_set_response	svc_dme_peer_set_response;
		struct gb_svc_route_create_request	svc_route_create_request;
		struct gb_svc_route_destroy_request	svc_route_destroy_request;
		struct gb_svc_pwrmon_rail_count_get_response	svc_pwrmon_rail_count_get_response;
		struct gb_svc_intf_vsys_response	svc_intf_vsys_response;
		struct gb_svc_intf_refclk_response	svc_intf_refclk_response;
		struct gb_svc_intf_unipro_response	svc_intf_unipro_response;
		struct gb_svc_intf_activate_response	svc_intf_activate_response;
		struct gb_svc_intf_set_pwrm_response	svc_intf_set_pwrm_response;
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
		struct gb_bootrom_version_response	fw_version_response;
		struct gb_bootrom_firmware_size_request		fw_size_req;
		struct gb_bootrom_firmware_size_response	fw_size_resp;
		struct gb_bootrom_get_firmware_request	fw_get_firmware_req;
		struct gb_bootrom_get_firmware_response fw_get_firmware_resp;
		struct gb_bootrom_ready_to_boot_request fw_rbt_req;
		struct gb_lights_blink_request		lights_blink_req;
		struct gb_lights_get_lights_response	lights_gl_rsp;
		struct gb_lights_event_request		lights_gl_event_req;
		struct gb_lights_set_brightness_request	lights_glc_bright_req;
		struct gb_lights_set_fade_request	lights_glc_fade_req;
		struct gb_lights_set_color_request	lights_glc_color_req;
		struct gb_lights_blink_request		lights_glc_blink_req;
		struct gb_lights_get_light_config_request	lights_gl_conf_req;
		struct gb_lights_get_light_config_response	lights_gl_conf_rsp;
		struct gb_lights_get_channel_config_request	lights_glc_conf_req;
		struct gb_lights_get_channel_config_response	lights_glc_conf_rsp;
		struct gb_lights_get_channel_flash_config_request	lights_glc_fconf_req;
		struct gb_lights_get_channel_flash_config_response	lights_glc_fconf_rsp;
		struct gb_lights_set_flash_intensity_request	lights_glc_fint_req;
		struct gb_lights_set_flash_strobe_request	lights_glc_fstrobe_req;
		struct gb_lights_set_flash_timeout_request	lights_glc_ftimeout_req;
		struct gb_lights_get_flash_fault_request	lights_glc_ffault_req;
		struct gb_lights_get_flash_fault_response	lights_glc_ffault_rsp;
		struct gb_power_supply_get_supplies_response	psy_get_supplies_rsp;
		struct gb_power_supply_get_description_request	psy_get_desc_req;
		struct gb_power_supply_get_description_response	psy_get_desc_rsp;
		struct gb_power_supply_get_property_descriptors_request	psy_get_props_req;
		struct gb_power_supply_get_property_descriptors_response psy_get_props_rsp;
		struct gb_power_supply_get_property_request		psy_get_prop_req;
		struct gb_power_supply_get_property_response		psy_get_prop_rsp;
		struct gb_power_supply_set_property_request		psy_set_prop_req;
		struct gb_spi_master_config_response	spi_mc_rsp;
		struct gb_spi_device_config_request	spi_dc_req;
		struct gb_spi_device_config_response	spi_dc_rsp;
		struct gb_spi_transfer_request		spi_xfer_req;
		struct gb_spi_transfer_response		spi_xfer_rsp;
		struct gb_fw_download_find_firmware_request	fw_download_find_req;
		struct gb_fw_download_find_firmware_response	fw_download_find_rsp;
		struct gb_fw_download_fetch_firmware_request	fw_download_fetch_req;
		struct gb_fw_download_fetch_firmware_response	fw_download_fetch_rsp;
		struct gb_fw_download_release_firmware_request	fw_download_release_req;
		struct gb_fw_mgmt_interface_fw_version_response	fw_mgmt_intf_fw_version_rsp;
		struct gb_fw_mgmt_load_and_validate_fw_request	fw_mgmt_load_validate_fw_req;
		struct gb_fw_mgmt_loaded_fw_request		fw_mgmt_loaded_fw_req;
		struct gb_fw_mgmt_backend_fw_version_request	fw_mgmt_backend_fw_ver_req;
		struct gb_fw_mgmt_backend_fw_version_response	fw_mgmt_backend_fw_ver_rsp;
		struct gb_fw_mgmt_backend_fw_update_request	fw_mgmt_backend_fw_update_req;
		struct gb_fw_mgmt_backend_fw_updated_request	fw_mgmt_backend_fw_updated_req;
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
	uint8_t *buf = data;
	int i;

	fprintf(stdout, "[R] GBSIM: DUMP ->");
	for (i = 0; i < size; i++)
		fprintf(stdout, " %02hhx", buf[i]);
	fprintf(stdout, "\n");
	fflush(stdout);
}

static inline uint8_t cport_to_module_id(uint16_t cport_id)
{
	/* FIXME can identify based on registered cport module */
	return 1;
}

struct gbsim_connection *connection_find(uint16_t cport_id);
void allocate_connection(uint16_t cport_id, uint16_t hd_cport_id, int protocol_id);
uint16_t find_hd_cport_for_protocol(int protocol_id);
void free_connection(struct gbsim_connection *connections);
void free_connections(void);

int gadget_create(usbg_state **, usbg_gadget **);
int gadget_enable(usbg_gadget *);
int gadget_cleanup(usbg_state *, usbg_gadget *);

int functionfs_init(void);
int functionfs_loop(void);
int functionfs_cleanup(void);
void cleanup_endpoint(int, char *);

int inotify_start(char *);

void *recv_thread(void *);
void recv_thread_cleanup(void *);

int control_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
char *control_get_operation(uint8_t type);

int svc_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
int svc_request_send(uint8_t, uint8_t);
char *svc_get_operation(uint8_t type);
void svc_init(void);
void svc_exit(void);

int gpio_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
char *gpio_get_operation(uint8_t type);
void gpio_init(void);

int i2c_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
char *i2c_get_operation(uint8_t type);
void i2c_init(void);

int pwm_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
char *pwm_get_operation(uint8_t type);
void pwm_init(void);

int sdio_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
char *sdio_get_operation(uint8_t type);
void sdio_init(void);

int spi_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
char *spi_get_operation(uint8_t type);

int lights_handler(struct gbsim_connection *,  void *, size_t, void *, size_t);
char *lights_get_operation(uint8_t type);

int power_supply_handler(struct gbsim_connection *,  void *, size_t, void *, size_t);
char *power_supply_get_operation(uint8_t type);

int uart_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
char *uart_get_operation(uint8_t type);
void uart_init(void);
void uart_cleanup(void);

int loopback_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
char *loopback_get_operation(uint8_t type);
void loopback_init(void);
void loopback_cleanup(void);

int bootrom_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
char *bootrom_get_operation(uint8_t type);

int fw_mgmt_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
char *fw_mgmt_get_operation(uint8_t type);

int fw_download_handler(struct gbsim_connection *, void *, size_t, void *, size_t);
char *fw_download_get_operation(uint8_t type);
int download_firmware(char *tag, uint16_t hd_cport_id, void (*func)(void));

bool manifest_parse(void *data, size_t size);
void reset_hd_cport_id(void);
int send_response(uint16_t hd_cport_id,
			struct op_msg *message, uint16_t message_size,
			uint16_t operation_id, uint8_t type, uint8_t result);
int send_request(uint16_t hd_cport_id,
			struct op_msg *message, uint16_t message_size,
			uint16_t operation_id, uint8_t type);

#endif /* __GBSIM_H */
