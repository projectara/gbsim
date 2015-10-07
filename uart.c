
/*
 * Greybus Simulator
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <libsoc_gpio.h>
#include <linux/fs.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "gbsim.h"

/* TODO: BOD derive these sizes by interrogating the link size */
#define GB_UART_MESSAGE_SIZE_MAX		GB_OPERATION_DATA_SIZE_MAX
#define GB_UART_DATA_SIZE_MAX \
	(GB_UART_MESSAGE_SIZE_MAX - sizeof(struct gb_uart_send_data_request))
#define BREAK_DURATION_MS 300			/* break duration tcsendbreak() */

/* greybus-spec/build/html/bridged_phy.html#uart-protocol */
#define GB_UART_MAX				255
#define GB_OPERATION_DATA_SIZE_MAX		0x400	/* TODO: BOD */

#define UART_MAXNAME				20
#define UART_IDX_TX				1
#define UART_IDX_RX				0
#define UART_IDX_COUNT				2

/*
 * This code works in the following way.
 * Each tty has a handle to the /dev/ttyOx port represented by a handle 'fd'.
 * Each tty has a pipe for signalling that the AP ACKed Module -> AP UART data.
 * A single thread is responsible for running select on all open tty ports
 * and relaying data from each tty to the AP as data arrives on the tty handle.
 * This thread will wait for up to 2 seconds for the AP to send back the
 * corresponding ACK. If the ACK never comes, the data is not resent.
 * When the AP wants to send data to the UART then this is written directly
 * to the fd for the relevant tty.
 * The RX thread has a pipe file-descriptor used to signal thread termination.
 * This pipe along with the file descriptors for the open tty ports is run
 * though a timeless select() in uart_thread().
 */
struct gb_uart_port {
	uint16_t	cport_id;
	uint16_t	hd_cport_id;
	int		fd;
	uint8_t		id;
	bool		init;
	bool		esc;
	char		name[UART_MAXNAME];
	uint8_t		module_id;
	int		tiocm_bits;
	pthread_mutex_t	uart_port;
};

static struct gb_uart_port up[GB_UART_MAX];
static int uart_sig_pipe[UART_IDX_COUNT] = {-1, -1};
static bool terminate_thread;
static int thread_started;
static int port_count;
static int up_count;
static pthread_t uart_pthread;
static pthread_barrier_t uart_barrier;

/* Only used when bbb_backend is true */
static int gb_uart_send(int i, void *tbuf, size_t tsize, __u8 type, __u8 flags)
{
	char uart_buf[GB_OPERATION_DATA_SIZE_MAX] = { };
	struct op_msg *msg = (struct op_msg *)uart_buf;
	struct gb_operation_msg_hdr *oph = &msg->header;
	size_t payload_size = 0;
	uint16_t message_size = sizeof(*oph);
	struct gb_uart_recv_data_request *rdr =
		(struct gb_uart_recv_data_request *)(uart_buf + sizeof(struct gb_operation_msg_hdr));
	struct gb_uart_serial_state_request *ssr =
		(struct gb_uart_serial_state_request *)(uart_buf + sizeof(struct gb_operation_msg_hdr));

	switch (type) {
	case GB_UART_TYPE_RECEIVE_DATA:
		rdr->size = htole16(tsize);
		rdr->flags = flags;
		memcpy(&rdr->data, tbuf, tsize);
		payload_size = sizeof(*rdr) + tsize;
		break;
	case GB_UART_TYPE_SERIAL_STATE:
		memcpy(&ssr->control, tbuf, sizeof(ssr->control));
		payload_size = sizeof(*ssr) + sizeof(ssr->control);
		break;
	default:
		gbsim_error("UART send operation %02x invalid\n", type);
		return -EINVAL;

	}
	message_size += payload_size;

	/* Operation id is 0 (unidirectional operation) */

	return send_request(up[i].hd_cport_id, msg, message_size, 0, type);
}

static int tty_find_port(uint8_t module_id, uint16_t cport_id)
{
	int i;

	for (i = 0; i < port_count; i++) {
		if (up[i].cport_id == cport_id &&
		    up[i].module_id == module_id)
		    break;
	}
	return i;
}

/* Only used when bbb_backend is true */
static void tty_poll_modem_state(int i)
{
	int ret;
	int tiocm_bits;
	extern int errno;

	pthread_mutex_lock(&up[i].uart_port);
	ret = ioctl(up[i].fd, TIOCMGET, &tiocm_bits);
	if (ret == 0 && up[i].tiocm_bits != tiocm_bits) {
		up[i].tiocm_bits = tiocm_bits;
		tiocm_bits =  up[i].tiocm_bits & TIOCM_CD  ? GB_UART_CTRL_DCD : 0;
		tiocm_bits |= up[i].tiocm_bits & TIOCM_DSR ? GB_UART_CTRL_DSR : 0;
		tiocm_bits |= up[i].tiocm_bits & TIOCM_RI  ? GB_UART_CTRL_RI  : 0;
		gb_uart_send(i, &tiocm_bits, sizeof(tiocm_bits),
			     GB_UART_TYPE_SERIAL_STATE, 0);
		if (verbose)
			gbsim_debug("UART DCD=%d DSR=%d RI=%d",
				    tiocm_bits & GB_UART_CTRL_DCD,
				    tiocm_bits & GB_UART_CTRL_DSR,
				    tiocm_bits & GB_UART_CTRL_RI);
	}
	pthread_mutex_unlock(&up[i].uart_port);
}

/* Only used when bbb_backend is true */
static unsigned char *gb_uart_send_escape_sequences(int i, unsigned char *data,
						    int size)
{
	unsigned char *begin = data;
	unsigned char *end = data + size;
	unsigned char *send_data = data;
	__u8 flags = 0;

	while (data < end && flags == 0) {
		/* With PARMRK set 0xff indicates an escape sequence */
		if (*data == 0xff) {
			data += 1;
			if (data == end)
				goto err;
			switch (*data) {
			case 0xff:
				/* 0xff received : 0xff, 0xff */
				*send_data = *data;
				break;
			case 0x00:
				data += 1;
				if (data == end)
					goto err;
				if (*data == 0x00) {
					/* Break condition : 0xff, 0x00, 0x00 */
					flags = GB_UART_RECV_FLAG_BREAK;
				} else {
					/* Pairty/framing error for byte 'n' : 0xff, 0x00, n */
					*send_data = *data;
					flags = GB_UART_RECV_FLAG_PARITY | GB_UART_RECV_FLAG_FRAMING;
				}
				break;
			default:
				gbsim_error("Unexpected byte in escape 0x%02x\n",
					*data);
			}
		} else {
			*send_data = *data;
		}
		data += 1;
		send_data += 1;
	}

	/* Send the parsed message */
	size = send_data - begin;
	gb_uart_send(i, begin, size, GB_UART_TYPE_RECEIVE_DATA, flags);

	/* Return offset */
	return data;
err:
	gbsim_error("UART: parsing esc sequence");
	return end;

}

/* Only used when bbb_backend is true */
static int tty_read(int i)
{
	unsigned char data[GB_UART_DATA_SIZE_MAX];
	unsigned char *next_frame;
	unsigned char *end;
	int ret;
	extern int errno;

	pthread_mutex_lock(&up[i].uart_port);
	ret = read(up[i].fd, data, sizeof(data));
	pthread_mutex_unlock(&up[i].uart_port);
	if (ret < 0) {
		if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
			return ret;
	} else {
		if (up[i].esc) {
			next_frame = data;
			end = &data[ret];
			while (next_frame < end) {
				next_frame = gb_uart_send_escape_sequences(i, next_frame, ret);
				ret = end-next_frame;
			}
		} else {
			gb_uart_send(i, data, ret, GB_UART_TYPE_RECEIVE_DATA, 0);
		}
	}
	return 0;
}

static int tty_write(uint8_t module_id, uint16_t cport_id, void *tbuf, size_t tsize)
{
	int i;
	int ret = 0;
	extern int errno;

	if (!bbb_backend)
		return tsize;

	i = tty_find_port(module_id, cport_id);
	if (i == port_count || up[i].init == false) {
		gbsim_error("UART Module %hhu AP Cport %hu not connected\n",
			     module_id, cport_id);
		return -EINVAL;
	}

	pthread_mutex_lock(&up[i].uart_port);
	ret = write(up[i].fd, tbuf, tsize);
	pthread_mutex_unlock(&up[i].uart_port);

	if (ret < 0)
		gbsim_error("UART write -> %s failed errno=%d\n",
			    up[i].name, errno);

	if (verbose) {
		gbsim_debug("AP -> UART %s length %zu\n", up[i].name, tsize);
		gbsim_dump(tbuf, tsize);
	}
	return ret;
}

static int tty_set_line_coding(int i,
			       struct gb_uart_set_line_coding_request *slc)
{
	struct termios newtios;
	speed_t speed;

	gbsim_debug("UART line coding rate %u format %u parity %u data_bits %u\n",
		     slc->rate, slc->format, slc->parity, slc->data_bits);
	if (bbb_backend)
		tcgetattr(up[i].fd, &newtios);

	newtios.c_cflag &= ~CBAUD;
	switch (slc->rate) {
	case 0:
		speed = B0;
		break;
	case 50:
		speed = B50;
		break;
	case 75:
		speed = B75;
		break;
	case 110:
		speed = B110;
		break;
	case 134:
		speed = B134;
		break;
	case 150:
		speed = B150;
		break;
	case 200:
		speed = B200;
		break;
	case 300:
		speed = B300;
		break;
	case 600:
		speed = B600;
		break;
	case 1200:
		speed = B1200;
		break;
	case 1800:
		speed = B1800;
		break;
	case 2400:
		speed = B2400;
		break;
	case 4800:
		speed = B4800;
		break;
	case 9600:
		speed = B9600;
		break;
	case 19200:
		speed = B19200;
		break;
	case 38400:
		speed = B38400;
		break;
	case 57600:
		speed = B57600;
		break;
	case 115200:
		speed = B115200;
		break;
	case 230400:
		speed = B230400;
		break;
	case 460800:
		speed = B460800;
		break;
	case 500000:
		speed = B500000;
		break;
	case 576000:
		speed = B576000;
		break;
	case 921600:
		speed = B921600;
		break;
	case 1000000:
		speed = B1000000;
		break;
	case 1152000:
		speed = B1152000;
		break;
	case 1500000:
		speed = B1500000;
		break;
	case 2000000:
		speed = B2000000;
		break;
	case 2500000:
		speed = B2500000;
		break;
	case 3000000:
		speed = B3000000;
		break;
	case 3500000:
		speed = B3500000;
		break;
	case 4000000:
		speed = B4000000;
		break;
	default:
		gbsim_error("UART BUAD %hhu invalid\n", slc->rate);
		return -EINVAL;
	}
	cfsetispeed(&newtios, speed);
	cfsetospeed(&newtios, speed);

	newtios.c_cflag &= ~CSIZE;
	switch (slc->data_bits) {
	case 5:
		newtios.c_cflag |= CS5;
		break;
	case 6:
		newtios.c_cflag |= CS6;
		break;
	case 7:
		newtios.c_cflag |= CS7;
		break;
	case 8:
		newtios.c_cflag |= CS8;
		break;
	default:
		gbsim_error("UART data format %hu invalid\n", slc->data_bits);
		return -EINVAL;
	}

	/* Stop bits */
	if (slc->format == GB_SERIAL_2_STOP_BITS)
		newtios.c_cflag |= CSTOPB;
	else
		newtios.c_cflag &= ~CSTOPB;

	/* Parity */
	newtios.c_iflag = 0;
	if (slc->parity) {
		newtios.c_cflag |= PARENB;
		switch (slc->parity) {
		case 1:
			/* odd parity */
			newtios.c_cflag |= PARODD;
			break;
		case 2:
			/* even parity */
			break;
		case 3:
			/* odd parity sticky parity bit */
			newtios.c_cflag |= PARODD;
			newtios.c_cflag |= CMSPAR;
			break;
		case 4:
			/* even parity sticky parity bit */
			newtios.c_cflag |= CMSPAR;
			break;
		default:
			gbsim_error("UART parity %hu invalid\n", slc->parity);
			return -EINVAL;
		}

		/* Enable input parity checking with parity bit strip */
		newtios.c_iflag = PARMRK | INPCK;
	}

	/* set input mode (non-canonical, no echo,...) */
	newtios.c_lflag = 0;
	newtios.c_oflag = 0;

	newtios.c_cc[VTIME] = 0;   /* inter-character timer unused */
	newtios.c_cc[VMIN]  = 1;   /* blocking read until 1 chars received */

	if (bbb_backend) {
		pthread_mutex_lock(&up[i].uart_port);
		tcsetattr(up[i].fd, TCSAFLUSH, &newtios);
		up[i].esc = newtios.c_cflag & PARENB ? true : false;
		pthread_mutex_unlock(&up[i].uart_port);
	}

	return 0;
}

/* Only used when bbb_backend is true */
static int tty_set_control_line_state(int i,
				      struct gb_uart_set_control_line_state_request *sls)
{
	int status, ret;

	gbsim_debug("UART set control line DTR=%d RTS=%d\n",
		    sls->control & GB_UART_CTRL_DTR,
		    sls->control & GB_UART_CTRL_RTS);

	if (!bbb_backend)
		return 0;

	ret = ioctl(up[i].fd, TIOCMGET, &status);
	if (ret)
		goto err;

	status &= ~(TIOCM_DTR | TIOCM_RTS);
	status |= ((sls->control & GB_UART_CTRL_DTR ? TIOCM_DTR : 0)|
		   (sls->control & GB_UART_CTRL_RTS ? TIOCM_RTS : 0));

	pthread_mutex_lock(&up[i].uart_port);
	ret = ioctl(up[i].fd, TIOCMSET, &status);
	pthread_mutex_unlock(&up[i].uart_port);
err:
	return ret;
}

/* Only used when bbb_backend is true */
static int tty_send_break(int i, struct gb_uart_set_break_request *set_break)
{
	int ret;

	if (!bbb_backend)
		return 0;

	pthread_mutex_lock(&up[i].uart_port);
	ret = tcdrain(i);
	if (ret)
		goto err;

	ret = tcsendbreak(up[i].fd, BREAK_DURATION_MS);
err:
	pthread_mutex_unlock(&up[i].uart_port);
	return ret;
}

static int uart_init_port(uint8_t module_id, uint16_t cport_id,
			  uint16_t hd_cport_id, uint8_t id)
{
	int i;

	i = tty_find_port(module_id, cport_id);
	if (i < port_count)
		return i;

	if (port_count >= GB_UART_MAX) {
		gbsim_error("All UARTs used Module %hu CPort %hhu\n",
			    module_id, cport_id);
		return -ENODEV;
	}
	up[port_count].module_id = module_id;
	up[port_count].cport_id = cport_id;
	up[port_count].hd_cport_id = hd_cport_id;
	up[port_count].id = id;
	up[port_count].init = true;
	gbsim_info("UART Module %hu Cport %hhu HDCport %hhu port-index %d\n",
		   module_id, cport_id, hd_cport_id, port_count);
	i = port_count;
	port_count++;
	return i;
}

int uart_handler(struct gbsim_cport *cport, void *rbuf,
		 size_t rsize, void *tbuf, size_t tsize)
{
	struct gb_operation_msg_hdr *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	size_t payload_size = 0;
	uint16_t message_size;
	uint16_t cport_id = cport->id;
	uint16_t hd_cport_id = cport->hd_cport_id;
	uint8_t module_id;
	uint8_t result = PROTOCOL_STATUS_SUCCESS;
	struct gb_uart_set_break_request *set_break;
	struct gb_uart_send_data_request *send_data;
	struct gb_uart_set_line_coding_request *line_coding;
	struct gb_uart_set_control_line_state_request *line_state;
	int i;
	extern int errno;

	module_id = cport_to_module_id(cport_id);

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct gb_operation_msg_hdr *)&op_req->header;

	/* Associate the module_id and cport_id with the device fd */
	i = uart_init_port(module_id, cport_id, hd_cport_id, oph->operation_id);
	if (i < 0)
		return i;

	switch (oph->type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(struct gb_protocol_version_response);
		op_rsp->pv_rsp.major = GREYBUS_VERSION_MAJOR;
		op_rsp->pv_rsp.minor = GREYBUS_VERSION_MINOR;
		break;
	case GB_UART_TYPE_SEND_DATA:
		send_data = &op_req->uart_send_data_req;
		if (tty_write(module_id, cport_id, send_data->data, send_data->size) < send_data->size)
			result = PROTOCOL_STATUS_INVALID;
		gbsim_debug("UART send len %hu\n", send_data->size);
		break;
	case GB_UART_TYPE_SET_LINE_CODING:
		line_coding = &op_req->uart_slc_req;
		if (tty_set_line_coding(i, line_coding))
			result = PROTOCOL_STATUS_INVALID;
		break;
	case GB_UART_TYPE_SET_CONTROL_LINE_STATE:
		line_state = &op_req->uart_sls_req;
		if (tty_set_control_line_state(i, line_state))
			result = PROTOCOL_STATUS_INVALID;
		gbsim_debug("UART dtr=%d rts=%d\n",
			line_state->control&GB_UART_CTRL_DTR,
			line_state->control & GB_UART_CTRL_RTS);
		break;
	case GB_UART_TYPE_SEND_BREAK:
		set_break = &op_req->uart_sb_req;
		if (tty_send_break(i, set_break))
			result = PROTOCOL_STATUS_INVALID;
		break;
	case (OP_RESPONSE | GB_UART_TYPE_RECEIVE_DATA):
	case (OP_RESPONSE | GB_UART_TYPE_SERIAL_STATE):
		gbsim_error("AP -> Module %hhu CPort %hu unsol resp %02x\n",
			    module_id, cport_id, oph->type);
		return 0;
	default:
		return -EINVAL;
	}

	message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
	return send_response(hd_cport_id, op_rsp, message_size,
			oph->operation_id, oph->type, result);
}

/* Only used when bbb_backend is true */
static void *uart_thread(void *param)
{
	fd_set fdset;
	int i, ret;
	int max = uart_sig_pipe[UART_IDX_RX];
	struct timeval tv;
	extern int errno;

	pthread_barrier_wait(&uart_barrier);

	for (i = 0; i < up_count; i++)
		if (max < up[i].fd)
			max = up[i].fd;
	while (!terminate_thread) {
		for (i = 0; i < up_count; i++) {
			if (up[i].init == true)
				tty_poll_modem_state(i);
		}

		FD_ZERO(&fdset);
		FD_SET(uart_sig_pipe[UART_IDX_RX] , &fdset);
		for (i = 0; i < up_count; i++) {
			if (up[i].init == true)
				FD_SET(up[i].fd , &fdset);
		}

		tv.tv_sec = 1;
		tv.tv_usec = 0;
		ret = select(1 + max, &fdset, 0, 0, &tv);
		switch (ret) {
		case -1:
			gbsim_error("%s : select errno=%d\n", __func__, errno);
			terminate_thread = true;
			break;
		case 0:
			break;
		default:
			if (FD_ISSET(uart_sig_pipe[UART_IDX_RX], &fdset)) {
				terminate_thread = true;
				break;
			}
			for (i = 0; i < up_count; i++) {
				if (FD_ISSET(up[i].fd, &fdset)) {
					if (tty_read(i)) {
						terminate_thread = true;
						break;
					}
				}
			}
			break;
		}
	}
	gbsim_info("UART thread exit\n");
	pthread_exit(NULL);
	return NULL;
}

void uart_cleanup(void)
{
	int i;
	char c;
	extern int errno;

	if (thread_started) {
		/* signal termination */
		if (write(uart_sig_pipe[UART_IDX_TX], &c, 1) < 0)
			gbsim_error("Write to signal pipe fail %d\n", errno);

		/* sync */
		pthread_join(uart_pthread, NULL);
		pthread_barrier_destroy(&uart_barrier);
	}

	/* Close serial thread pipes */
	if (uart_sig_pipe[UART_IDX_TX] != -1)
		close(uart_sig_pipe[UART_IDX_TX]);
	if (uart_sig_pipe[UART_IDX_RX] != -1)
		close(uart_sig_pipe[UART_IDX_RX]);

	/* Close fds to serial ports a signal pipes for ports */
	for (i = 0; i < GB_UART_MAX; i++) {
		if (up[i].fd != -1)
			close(up[i].fd);
	}
}

/* Only used when bbb_backend is true */
static int uart_open(int idx)
{
	/* Open fd to serial port */
	snprintf(up[up_count].name, sizeof(up[up_count].name), "/dev/ttyO%d", idx);
	up[up_count].fd = open(up[up_count].name, O_RDWR);
	if (up->fd < 0) {
		fprintf(stderr, "cannot open %s errno=%d\n", up[up_count].name, errno);
		uart_cleanup();
		return EXIT_FAILURE;
	}

	pthread_mutex_init(&up[up_count].uart_port, 0);
	up_count++;
	return 0;
}

char *uart_get_operation(uint8_t type)
{
	switch (type) {
	case GB_REQUEST_TYPE_INVALID:
		return "GB_UART_TYPE_INVALID";
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		return "GB_UART_TYPE_PROTOCOL_VERSION";
	case GB_UART_TYPE_SEND_DATA:
		return "GB_UART_TYPE_SEND_DATA";
	case GB_UART_TYPE_RECEIVE_DATA:
		return "GB_UART_TYPE_RECEIVE_DATA";
	case GB_UART_TYPE_SET_LINE_CODING:
		return "GB_UART_TYPE_SET_LINE_CODING";
	case GB_UART_TYPE_SET_CONTROL_LINE_STATE:
		return "GB_UART_TYPE_SET_CONTROL_LINE_STATE";
	case GB_UART_TYPE_SEND_BREAK:
		return "GB_UART_TYPE_SEND_BREAK";
	case GB_UART_TYPE_SERIAL_STATE:
		return "GB_UART_TYPE_SERIAL_STATE";
	default:
		return "(Unknown operation)";
	}
}

void uart_init(void)
{
	extern int errno;
	int i, ret;

	if (!bbb_backend)
		return;
	/* Loop through the /dev/tty0x entries */
	for (i = 0; i < uart_count; i++)
		if (uart_open(i + uart_portno))
			return;

	/* Create a pipe for kicking the thread's select */
	ret = pipe2(uart_sig_pipe, O_NONBLOCK);
	if (ret < 0) {
		perror("error making pipe!\n");
		uart_cleanup();
		return;
	}

	/* Init fdr thread */
	pthread_barrier_init(&uart_barrier, 0, 2);
	ret = pthread_create(&uart_pthread, NULL, uart_thread, NULL);
	if (ret < 0) {
		perror("can't create uart thread");
		uart_cleanup();
		return;
	}
	thread_started = 1;
	pthread_barrier_wait(&uart_barrier);
}
