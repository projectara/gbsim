/*
 * Greybus Simulator
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "gbsim.h"

#define SPI_BPW_MASK(bits) BIT((bits) - 1)
#define SPIDEV_TYPE	0x00
#define SPINOR_TYPE	0x01

/*
 * this control the number of devices that will be created, for even chipselect
 * spidev for odd spinor
 */
#define SPI_NUM_CS	2

/* use the following spi to emulate */
/* "w25q256",     0xef4019,      0,  64 << 10, 512, ER_4K) }, */
#define SPI_NOR_JEDEC	0xef4019
#define SPI_NOR_SIZE	(32 * 1024 * 1024)

#define SPI_NOR_IDLE	0
#define SPI_NOR_CMD	1
#define SPI_NOR_PP	2
#define SPI_NOR_READ	3

struct gb_spi_dev_config {
	uint16_t	mode;
	uint32_t	bits_per_word;
	uint32_t	max_speed_hz;
	uint8_t		device_type;
	uint8_t		name[32];
};

struct gb_spi_dev {
	uint8_t	cs;
	uint8_t	*buf;
	size_t	buf_size;
	uint8_t	*buf_resp;
	uint8_t	cmd_resp[6];
	size_t	resp_size;
	int	state;
	int	cmd;
	int	(*xfer_req_recv)(struct gb_spi_dev *dev,
				 struct gb_spi_transfer *xfer,
				 uint8_t *xfer_data);
	struct gb_spi_dev_config *conf;
};

struct gb_spi_master {
	uint16_t		mode;
	uint8_t			flags;
	uint32_t		bpwm;
	uint32_t		min_speed_hz;
	uint32_t		max_speed_hz;
	uint8_t			num_chipselect;
	struct gb_spi_dev	*devices;
};

static struct gb_spi_master *master;

static struct gb_spi_dev_config spidev_config = {
	.mode		= GB_SPI_MODE_MODE_3,
	.bits_per_word	= 8,
	.max_speed_hz	= 10000000,
	.name		= "dev",
	.device_type	= GB_SPI_SPI_DEV,
};

static struct gb_spi_dev_config spinor_config = {
	.mode		= GB_SPI_MODE_MODE_3,
	.bits_per_word	= 32,
	.max_speed_hz	= 10000000,
	.name		= "nor",
	.device_type	= GB_SPI_SPI_NOR,
};

/* Flash opcodes. */
#define SPINOR_OP_WREN		0x06	/* Write enable */
#define SPINOR_OP_RDSR		0x05	/* Read status register */
#define SPINOR_OP_WRSR		0x01	/* Write status register 1 byte */
#define SPINOR_OP_READ		0x03	/* Read data bytes (low frequency) */
#define SPINOR_OP_READ_FAST	0x0b	/* Read data bytes (high frequency) */
#define SPINOR_OP_READ_1_1_2	0x3b	/* Read data bytes (Dual SPI) */
#define SPINOR_OP_READ_1_1_4	0x6b	/* Read data bytes (Quad SPI) */
#define SPINOR_OP_PP		0x02	/* Page program (up to 256 bytes) */
#define SPINOR_OP_BE_4K		0x20	/* Erase 4KiB block */
#define SPINOR_OP_BE_4K_PMC	0xd7	/* Erase 4KiB block on PMC chips */
#define SPINOR_OP_BE_32K	0x52	/* Erase 32KiB block */
#define SPINOR_OP_CHIP_ERASE	0xc7	/* Erase whole flash chip */
#define SPINOR_OP_SE		0xd8	/* Sector erase (usually 64KiB) */
#define SPINOR_OP_RDID		0x9f	/* Read JEDEC ID */
#define SPINOR_OP_RDCR		0x35	/* Read configuration register */
#define SPINOR_OP_RDFSR		0x70	/* Read flag status register */

/* 4-byte address opcodes - used on Spansion and some Macronix flashes. */
#define SPINOR_OP_READ4		0x13	/* Read data bytes (low frequency) */
#define SPINOR_OP_READ4_FAST	0x0c	/* Read data bytes (high frequency) */
#define SPINOR_OP_READ4_1_1_2	0x3c	/* Read data bytes (Dual SPI) */
#define SPINOR_OP_READ4_1_1_4	0x6c	/* Read data bytes (Quad SPI) */
#define SPINOR_OP_PP_4B		0x12	/* Page program (up to 256 bytes) */
#define SPINOR_OP_SE_4B		0xdc	/* Sector erase (usually 64KiB) */

/* Used for SST flashes only. */
#define SPINOR_OP_BP		0x02	/* Byte program */
#define SPINOR_OP_WRDI		0x04	/* Write disable */
#define SPINOR_OP_AAI_WP	0xad	/* Auto address increment word program */

/* Used for Macronix and Winbond flashes. */
#define SPINOR_OP_EN4B		0xb7	/* Enter 4-byte mode */
#define SPINOR_OP_EX4B		0xe9	/* Exit 4-byte mode */

/* Used for Spansion flashes only. */
#define SPINOR_OP_BRWR		0x17	/* Bank register write */

/* Used for Micron flashes only. */
#define SPINOR_OP_RD_EVCR      0x65    /* Read EVCR register */
#define SPINOR_OP_WD_EVCR      0x61    /* Write EVCR register */

/* Status Register bits. */
#define SR_WIP			1	/* Write in progress */
#define SR_WEL			2	/* Write enable latch */
/* meaning of other SR_* bits may differ between vendors */
#define SR_BP0			4	/* Block protect 0 */
#define SR_BP1			8	/* Block protect 1 */
#define SR_BP2			0x10	/* Block protect 2 */
#define SR_SRWD			0x80	/* SR write protect */

#define SR_QUAD_EN_MX		0x40	/* Macronix Quad I/O */

/* Enhanced Volatile Configuration Register bits */
#define EVCR_QUAD_EN_MICRON    0x80    /* Micron Quad I/O */

/* Flag Status Register bits */
#define FSR_READY		0x80

/* Configuration Register bits. */
#define CR_QUAD_EN_SPAN		0x2	/* Spansion Quad I/O */

enum read_mode {
	SPI_NOR_NORMAL = 0,
	SPI_NOR_FAST,
	SPI_NOR_DUAL,
	SPI_NOR_QUAD,
};

static int spidev_xfer_req_recv(struct gb_spi_dev *dev,
				struct gb_spi_transfer *xfer,
				uint8_t *xfer_data)
{
	int i;
	int fd;
	int ret;

	/* if it is only a write transfer write it to a file in tmp */
	if (xfer->rdwr & ~GB_SPI_XFER_READ) {
		fd = open("/tmp/spi_file", O_WRONLY | O_CREAT | O_APPEND, 0);
		lseek(fd, SEEK_END, 0);
		ret = write(fd, xfer_data, xfer->len);
		if (ret < xfer->len)
			gbsim_debug("%s: Failed to write %u bytes: %d\n",
				    __func__, xfer->len, ret);
		close(fd);
		return 0;
	}

	/* if it is read/write, e.g., spidev_test, just return the complement */
	for (i = 0; i < xfer->len; i++, xfer_data++, dev->buf_resp++)
		*dev->buf_resp = ~(*xfer_data);

	dev->resp_size = xfer->len;

	return 0;
}


static void spinor_handle_cmd(struct gb_spi_dev *dev, uint8_t cmd_op)
{
	dev->cmd = cmd_op;

	/*
	 * basic operations, for discovery over jedec, need to be extend for
	 * other operations
	 */
	switch (cmd_op) {
	case SPINOR_OP_RDID:
		dev->resp_size = 3;
		dev->buf_resp[0] = (SPI_NOR_JEDEC >> 16) & 0xff;
		dev->buf_resp[1] = (SPI_NOR_JEDEC >> 8) & 0xff;
		dev->buf_resp[2] = SPI_NOR_JEDEC & 0xff;
		break;
	case SPINOR_OP_EN4B:
		dev->resp_size = 0;
		break;
	default:
		break;
	}

}

static int spinor_xfer_req_recv(struct gb_spi_dev *dev,
				struct gb_spi_transfer *xfer,
				uint8_t *xfer_data)
{
	switch (dev->state) {
	case SPI_NOR_IDLE:
		spinor_handle_cmd(dev, *xfer_data);
		dev->state = SPI_NOR_CMD;
		break;
	case SPI_NOR_CMD:
		/* just force the resp_size to the expected length */
		if (dev->cmd)
			dev->resp_size = xfer->len;
		dev->state = SPI_NOR_IDLE;
		break;
	default:
		break;
	}


	return 0;
}

static int spi_set_device(uint8_t cs, int spi_type)
{
	struct gb_spi_dev *spi_dev = &master->devices[cs];

	switch (spi_type) {
	case SPINOR_TYPE:
		spi_dev->xfer_req_recv = spinor_xfer_req_recv;
		spi_dev->buf_size = SPI_NOR_SIZE;
		spi_dev->conf = &spinor_config;
		break;
	default:
	case SPIDEV_TYPE:
		spi_dev->xfer_req_recv = spidev_xfer_req_recv;
		spi_dev->buf_size = 0;
		spi_dev->conf = &spidev_config;
		break;
	}

	spi_dev->state = SPI_NOR_IDLE;
	spi_dev->cs = cs;

	if (!spi_dev->buf_size)
		return 0;

	spi_dev->buf = calloc(1, spi_dev->buf_size);
	if (!spi_dev->buf) {
		free(spi_dev);
		return -ENOMEM;
	}

	return 0;
}

static int spi_master_setup(void)
{
	int i;

	master = calloc(1, sizeof(struct gb_spi_master));
	if (!master)
		return -ENOMEM;

	master->mode = GB_SPI_MODE_MODE_3;
	master->flags = 0;
	master->bpwm = SPI_BPW_MASK(8) | SPI_BPW_MASK(16) | SPI_BPW_MASK(32);
	master->min_speed_hz = 400000;
	master->max_speed_hz = 48000000;
	master->num_chipselect = SPI_NUM_CS;

	master->devices = calloc(master->num_chipselect,
				 sizeof(struct gb_spi_dev));
	if (!master->devices)
		return -ENOMEM;

	/* for even set spidev for odd set spinor */
	for (i = 0; i < SPI_NUM_CS; i++)
		spi_set_device(i, (i % 2 ? SPINOR_TYPE : SPIDEV_TYPE));

	return 0;
}

int spi_handler(struct gbsim_connection *connection, void *rbuf,
		   size_t rsize, void *tbuf, size_t tsize)
{
	struct gb_operation_msg_hdr *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	size_t payload_size = 0;
	uint16_t message_size;
	uint16_t hd_cport_id = connection->hd_cport_id;
	struct gb_spi_transfer *xfer;
	struct gb_spi_dev *spi_dev;
	struct gb_spi_dev_config *conf;
	void *xfer_data;
	int xfer_cs, cs;
	int xfer_count;
	int xfer_rx = 0;
	int ret;
	int i;

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct gb_operation_msg_hdr *)&op_req->header;

	switch (oph->type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(struct gb_protocol_version_response);
		op_rsp->pv_rsp.major = GB_SPI_VERSION_MAJOR;
		op_rsp->pv_rsp.minor = GB_SPI_VERSION_MINOR;
		break;
	case GB_SPI_TYPE_MASTER_CONFIG:
		spi_master_setup();
		payload_size = sizeof(struct gb_spi_master_config_response);

		op_rsp->spi_mc_rsp.mode = htole16(master->mode);
		op_rsp->spi_mc_rsp.flags = htole16(master->flags);
		op_rsp->spi_mc_rsp.bits_per_word_mask = htole32(master->bpwm);
		op_rsp->spi_mc_rsp.num_chipselect = htole16(master->num_chipselect);
		op_rsp->spi_mc_rsp.min_speed_hz = htole32(master->min_speed_hz);
		op_rsp->spi_mc_rsp.max_speed_hz = htole32(master->max_speed_hz);
		break;
	case GB_SPI_TYPE_DEVICE_CONFIG:
		payload_size = sizeof(struct gb_spi_device_config_response);

		cs = op_req->spi_dc_req.chip_select;
		spi_dev = &master->devices[cs];
		conf = spi_dev->conf;

		op_rsp->spi_dc_rsp.mode = htole16(conf->mode);
		op_rsp->spi_dc_rsp.bits_per_word = conf->bits_per_word;
		op_rsp->spi_dc_rsp.max_speed_hz = htole32(conf->max_speed_hz);
		op_rsp->spi_dc_rsp.device_type = conf->device_type;
	        memcpy(op_rsp->spi_dc_rsp.name, conf->name, sizeof(conf->name));
		break;
	case GB_SPI_TYPE_TRANSFER:
		xfer_cs = op_req->spi_xfer_req.chip_select;
		xfer_count = op_req->spi_xfer_req.count;

		xfer = &op_req->spi_xfer_req.transfers[0];
		xfer_data = xfer + xfer_count;

		spi_dev = &master->devices[xfer_cs];

		spi_dev->buf_resp = op_rsp->spi_xfer_rsp.data;

		for (i = 0; i < xfer_count; i++, xfer++) {
			spi_dev->xfer_req_recv(spi_dev, xfer, xfer_data);
			/* we only increment if transfer is write */
			if (xfer->rdwr & GB_SPI_XFER_WRITE)
				xfer_data += xfer->len;
			if (xfer->rdwr & GB_SPI_XFER_READ)
				xfer_rx += xfer->len;
		}

		payload_size = sizeof(struct gb_spi_transfer_response) + xfer_rx;
		break;
	default:
		return -EINVAL;
	}

	message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
	ret = send_response(hd_cport_id, op_rsp, message_size,
			    oph->operation_id, oph->type,
			    PROTOCOL_STATUS_SUCCESS);
	return ret;
}

char *spi_get_operation(uint8_t type)
{
	switch (type) {
	case GB_REQUEST_TYPE_INVALID:
		return "GB_SPI_TYPE_INVALID";
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		return "GB_SPI_TYPE_PROTOCOL_VERSION";
	case GB_SPI_TYPE_MASTER_CONFIG:
		return "GB_SPI_TYPE_MASTER_CONFIG";
	case GB_SPI_TYPE_DEVICE_CONFIG:
		return "GB_SPI_TYPE_DEVICE_CONFIG";
	case GB_SPI_TYPE_TRANSFER:
		return "GB_SPI_TYPE_TRANSFER";
	default:
		return "(Unknown operation)";
	}
}
