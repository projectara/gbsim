/*
*/

#include <endian.h>
#include <stdbool.h>
#include <stdio.h>
#include <usbg/usbg.h>

#include <svc_msg.h>

extern int bbb_backend;
extern int i2c_adapter;
extern int verbose;

/* Matches up with the Greybus Protocol specification document */
#define GREYBUS_VERSION_MAJOR	0x00
#define GREYBUS_VERSION_MINOR	0x01

/* SVC message header + 2 bytes of payload */
#define HP_BASE_SIZE		sizeof(struct svc_msg_header) + 2

extern int control;
extern int svc_int;
extern int cport_in;
extern int cport_out;

struct gbsim_cport {
	TAILQ_ENTRY(gbsim_cport) cnode;
	int id;
	int protocol;
};

struct gbsim_info {
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

struct cport_msg {
	__u8	cport;
	__u8	data[0];
};

struct op_header {
	__le16	size;
	__le16	id;
	__u8	type;
	__u8	result;
	__u8	pad[2];
};

/* common ops */
struct protocol_version_rsp {
	__u8	version_major;
	__u8	version_minor;
};

/* GPIO */
struct gpio_line_count_rsp {
	__u8	status;
	__u8	count;
};

struct gpio_activate_req {
	__u8	which;
};

struct gpio_activate_rsp {
	__u8	status;
};

struct gpio_deactivate_req {
	__u8	which;
};

struct gpio_deactivate_rsp {
	__u8	status;
};

struct gpio_get_direction_req {
	__u8	which;
};

struct gpio_get_direction_rsp {
	__u8	status;
	__u8	direction;
};

struct gpio_direction_input_req {
	__u8	which;
};

struct gpio_direction_input_rsp {
	__u8	status;
};

struct gpio_direction_output_req {
	__u8	which;
	__u8	value;
};

struct gpio_direction_output_rsp {
	__u8	status;
};

struct gpio_get_value_req {
	__u8	which;
};

struct gpio_get_value_rsp {
	__u8	status;
	__u8	value;
};

struct gpio_set_value_req {
	__u8	which;
	__u8	value;
};

struct gpio_set_value_rsp {
	__u8	status;
};

struct gpio_set_debounce_req {
	__u8	which;
	__u8	usec;
};

struct gpio_set_debounce_rsp {
	__u8	status;
};

/* I2C */
struct i2c_functionality_rsp {
	__le32	functionality;
};

struct i2c_transfer_desc {
	__le16	addr;
	__le16	flags;
	__le16	size;
};

struct i2c_transfer_req {
	__le16	op_count;
	struct i2c_transfer_desc desc[0];
};

struct i2c_transfer_rsp {
	__u8	data[0];
};

/* PWM */
struct pwm_count_rsp {
	__u8	status;
	__u8	count;
};

struct pwm_activate_req {
	__u8	which;
};
struct pwm_activate_rsp {
	__u8	status;
};

struct pwm_deactivate_req {
	__u8	which;
};
struct pwm_deactivate_rsp {
	__u8	status;
};

struct pwm_config_req {
	__u8	which;
	__u32	duty;
	__u32	period;
};
struct pwm_config_rsp {
	__u8	status;
};

struct pwm_polarity_req {
	__u8	which;
	__u8	polarity;
};
struct pwm_polarity_rsp {
	__u8	status;
};

struct pwm_enable_req {
	__u8	which;
};

struct pwm_enable_rsp {
	__u8	status;
};

struct pwm_disable_req {
	__u8	which;
};

struct pwm_disable_rsp {
	__u8	status;
};


/* Ops */
struct op_msg {
	struct op_header	header;
	union {
		struct protocol_version_rsp		pv_rsp;
                struct gpio_line_count_rsp		gpio_lc_rsp;
                struct gpio_activate_req		gpio_act_req;
                struct gpio_activate_rsp		gpio_act_rsp;
                struct gpio_deactivate_req		gpio_deact_req;
                struct gpio_deactivate_rsp		gpio_deact_rsp;
		struct gpio_get_direction_req		gpio_get_dir_req;
		struct gpio_get_direction_rsp		gpio_get_dir_rsp;
		struct gpio_direction_input_req		gpio_dir_input_req;
		struct gpio_direction_input_rsp		gpio_dir_input_rsp;
		struct gpio_direction_output_req	gpio_dir_output_req;
		struct gpio_direction_output_rsp	gpio_dir_output_rsp;
		struct gpio_get_value_req		gpio_get_val_req;
		struct gpio_get_value_rsp		gpio_get_val_rsp;
		struct gpio_set_value_req		gpio_set_val_req;
		struct gpio_set_value_rsp		gpio_set_val_rsp;
		struct gpio_set_debounce_req		gpio_set_db_req;
		struct gpio_set_debounce_rsp		gpio_set_db_rsp;
		struct i2c_functionality_rsp		i2c_fcn_rsp;
		struct i2c_transfer_req			i2c_xfer_req;
		struct i2c_transfer_rsp			i2c_xfer_rsp;
                struct pwm_count_rsp			pwm_cnt_rsp;
                struct pwm_activate_req			pwm_act_req;
                struct pwm_activate_rsp			pwm_act_rsp;
                struct pwm_deactivate_req		pwm_deact_req;
                struct pwm_deactivate_rsp		pwm_deact_rsp;
                struct pwm_config_req			pwm_cfg_req;
                struct pwm_config_rsp			pwm_cfg_rsp;
                struct pwm_polarity_req			pwm_pol_req;
                struct pwm_polarity_rsp			pwm_pol_rsp;
                struct pwm_enable_req			pwm_enb_req;
                struct pwm_enable_rsp			pwm_enb_rsp;
                struct pwm_disable_req			pwm_dis_req;
                struct pwm_disable_rsp			pwm_dis_rsp;
	};
};

#define OP_RESPONSE			0x80

/* debug/info/error macros */
#define gbsim_debug(fmt, ...)						\
        do { if (verbose) fprintf(stdout, "[D] GBSIM: " fmt,  		\
				  ##__VA_ARGS__); } while (0)
#define gbsim_info(fmt, ...)						\
        do { fprintf(stdout, "[I] GBSIM: " fmt, ##__VA_ARGS__); } while (0)
#define gbsim_error(fmt, ...)						\
        do { fprintf(stderr, "[E] GBSIM: " fmt, ##__VA_ARGS__); } while (0)

static inline void gbsim_dump(__u8 *buf, size_t size)
{
	int i;

	for (i = 0; i < size; i++)
		fprintf(stdout, "%02x ", buf[i]);
	fprintf(stdout, "\n");
}

static inline int cport_to_module_id(__le16 cport)
{
	/* FIXME can identify based on registered cport module */
	return 1;
}

int gadget_create(usbg_state **, usbg_gadget **);
int gadget_enable(usbg_gadget *);
int gadget_cleanup(usbg_state *, usbg_gadget *);

int functionfs_init(void);
int functionfs_loop(void);
int functionfs_cleanup(void);
void cleanup_endpoint(int, char *);

int inotify_start(char *);

void send_hot_plug(char *, int);
void send_hot_unplug(int);

void send_link_up(int, int, int);

void *cport_thread(void *);
void cport_thread_cleanup(void *);

void gpio_handler(__u8 *, size_t);
void gpio_init(void);

void i2c_handler(__u8 *, size_t);
void i2c_init(void);

void pwm_handler(__u8 *, size_t);
void pwm_init(void);

bool manifest_parse(void *data, size_t size);
