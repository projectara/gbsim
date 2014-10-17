/*
*/

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
	__u8	pad[3];
};

/* common ops */
struct protocol_version_rsp {
	__u8	status;
	__u8	version_major;
	__u8	version_minor;
};

/* I2C */
struct i2c_functionality_rsp {
	__u8	status;
	__le32	functionality;
};

struct i2c_timeout_rsp {
	__u8	status;
};

struct i2c_retries_rsp {
	__u8	status;
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
	__u8	status;
	__u8	data[0];
};

/* Ops */
struct op_msg {
	struct op_header	header;
	union {
		struct protocol_version_rsp		pv_rsp;
		struct i2c_functionality_rsp		i2c_fcn_rsp;
		struct i2c_timeout_rsp			i2c_to_rsp;
		struct i2c_retries_rsp			i2c_rt_rsp;
		struct i2c_transfer_req			i2c_xfer_req;
		struct i2c_transfer_rsp			i2c_xfer_rsp;
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

void *cport_thread(void *);
void cport_thread_cleanup(void *);

void i2c_handler(__u8 *, size_t);
void i2c_init(void);
