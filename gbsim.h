/*
*/

#include <usbg/usbg.h>

#include <svc_msg.h>

extern int verbose;

/* Matches up with the Greybus Protocol specification document */
#define GREYBUS_VERSION_MAJOR	0x00
#define GREYBUS_VERSION_MINOR	0x01

/* SVC message header + 2 bytes of payload */
#define HP_BASE_SIZE		sizeof(struct svc_msg_header) + 2

/* debug/info/error macros */
#define gbsim_debug(fmt, ...)						\
        do { if (verbose) fprintf(stdout, "[D] GBSIM: " fmt,  		\
				  ##__VA_ARGS__); } while (0)
#define gbsim_info(fmt, ...)						\
        do { fprintf(stdout, "[I] GBSIM: " fmt, ##__VA_ARGS__); } while (0)
#define gbsim_error(fmt, ...)						\
        do { fprintf(stderr, "[E] GBSIM: " fmt, ##__VA_ARGS__); } while (0)

int gadget_create(usbg_state **, usbg_gadget **);
int gadget_enable(usbg_gadget *);
int gadget_cleanup(usbg_state *, usbg_gadget *);

int functionfs_init(void);
int functionfs_loop(void);
int functionfs_cleanup(void);

int inotify_start(char *);

void send_hot_plug(char *, int);
void send_hot_unplug(int);
