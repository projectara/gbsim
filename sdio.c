/*
 * Greybus Simulator
 *
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <fcntl.h>
#include <linux/fs.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "gbsim.h"

/* SD Emulation Code */
/*
 * This try to emulate an sd card only for test propose of the greybus kernel
 * implementation using the mmc_test, and is not fully featured, not all
 * commands are implemented and state transition is not fully validate.  All
 * values/states/registers are taken from the SDIO Simplified Specification
 * Version 3.00 and Physical Layer Simplified Specification Version 4.10.
 */
struct sd_card {
	/* Registers as in SD Memory Card Spec */
	uint32_t	ocr;
	uint32_t	cid[4];
	uint32_t	csd[4];
	uint16_t	rca;
	uint32_t	scr[2];
	uint32_t	card_status;
	uint32_t	sd_status[16];

	uint8_t		state;
#define R1_STATE_IDLE	0
#define R1_STATE_READY	1
#define R1_STATE_IDENT	2
#define R1_STATE_STBY	3
#define R1_STATE_TRAN	4
#define R1_STATE_DATA	5
#define R1_STATE_RCV	6
#define R1_STATE_PRG	7
#define R1_STATE_DIS	8

	uint16_t	max_blk_size;
	uint16_t	max_blk_count;
	uint32_t	vhs;
	uint32_t	caps;
	uint32_t	cmd;
	uint8_t		appcmd;
	uint32_t	rsp[4];
	uint8_t		*xfer;
	uint32_t	xfer_offset;
	uint8_t		*buf;
	uint16_t	blk_len;
	uint16_t	blk_count;
};

static struct sd_card *sd;

#define CLEAR_CONDITION_A	0x02004100 /* According current state */
#define CLEAR_CONDITION_B	0x00c01e00 /* related to previous command */
#define CLEAR_CONDITION_C	0xfd39a028 /* clear by read */

/* Standard MMC commands (4.1)           type  argument     response */
   /* class 1 */
#define MMC_GO_IDLE_STATE         0   /* bc                          */
#define MMC_SEND_OP_COND          1   /* bcr  [31:0] OCR         R3  */
#define MMC_ALL_SEND_CID          2   /* bcr                     R2  */
#define MMC_SET_RELATIVE_ADDR     3   /* ac   [31:16] RCA        R1  */
#define MMC_SET_DSR               4   /* bc   [31:16] RCA            */
#define MMC_SLEEP_AWAKE		  5   /* ac   [31:16] RCA 15:flg R1b */
#define MMC_SWITCH                6   /* ac   [31:0] See below   R1b */
#define MMC_SELECT_CARD           7   /* ac   [31:16] RCA        R1  */
#define MMC_SEND_EXT_CSD          8   /* adtc                    R1  */
#define MMC_SEND_CSD              9   /* ac   [31:16] RCA        R2  */
#define MMC_SEND_CID             10   /* ac   [31:16] RCA        R2  */
#define MMC_READ_DAT_UNTIL_STOP  11   /* adtc [31:0] dadr        R1  */
#define MMC_STOP_TRANSMISSION    12   /* ac                      R1b */
#define MMC_SEND_STATUS          13   /* ac   [31:16] RCA        R1  */
#define MMC_BUS_TEST_R           14   /* adtc                    R1  */
#define MMC_GO_INACTIVE_STATE    15   /* ac   [31:16] RCA            */
#define MMC_BUS_TEST_W           19   /* adtc                    R1  */
#define MMC_SPI_READ_OCR         58   /* spi                  spi_R3 */
#define MMC_SPI_CRC_ON_OFF       59   /* spi  [0:0] flag      spi_R1 */

  /* class 2 */
#define MMC_SET_BLOCKLEN         16   /* ac   [31:0] block len   R1  */
#define MMC_READ_SINGLE_BLOCK    17   /* adtc [31:0] data addr   R1  */
#define MMC_READ_MULTIPLE_BLOCK  18   /* adtc [31:0] data addr   R1  */
#define MMC_SEND_TUNING_BLOCK    19   /* adtc                    R1  */
#define MMC_SEND_TUNING_BLOCK_HS200	21	/* adtc R1  */

  /* class 3 */
#define MMC_WRITE_DAT_UNTIL_STOP 20   /* adtc [31:0] data addr   R1  */

  /* class 4 */
#define MMC_SET_BLOCK_COUNT      23   /* adtc [31:0] data addr   R1  */
#define MMC_WRITE_BLOCK          24   /* adtc [31:0] data addr   R1  */
#define MMC_WRITE_MULTIPLE_BLOCK 25   /* adtc                    R1  */
#define MMC_PROGRAM_CID          26   /* adtc                    R1  */
#define MMC_PROGRAM_CSD          27   /* adtc                    R1  */

  /* class 6 */
#define MMC_SET_WRITE_PROT       28   /* ac   [31:0] data addr   R1b */
#define MMC_CLR_WRITE_PROT       29   /* ac   [31:0] data addr   R1b */
#define MMC_SEND_WRITE_PROT      30   /* adtc [31:0] wpdata addr R1  */

  /* class 5 */
#define MMC_ERASE_GROUP_START    35   /* ac   [31:0] data addr   R1  */
#define MMC_ERASE_GROUP_END      36   /* ac   [31:0] data addr   R1  */
#define MMC_ERASE                38   /* ac                      R1b */

  /* class 9 */
#define MMC_FAST_IO              39   /* ac   <Complex>          R4  */
#define MMC_GO_IRQ_STATE         40   /* bcr                     R5  */

  /* class 7 */
#define MMC_LOCK_UNLOCK          42   /* adtc                    R1b */

  /* class 8 */
#define MMC_APP_CMD              55   /* ac   [31:16] RCA        R1  */
#define MMC_GEN_CMD              56   /* adtc [0] RD/WR          R1  */

/* SD commands                           type  argument     response */
  /* class 0 */
/* This is basically the same command as for MMC with some quirks. */
#define SD_SEND_RELATIVE_ADDR     3   /* bcr                     R6  */
#define SD_SEND_IF_COND           8   /* bcr  [11:0] See below   R7  */
#define SD_SWITCH_VOLTAGE         11  /* ac                      R1  */

  /* class 10 */
#define SD_SWITCH                 6   /* adtc [31:0] See below   R1  */

  /* class 5 */
#define SD_ERASE_WR_BLK_START    32   /* ac   [31:0] data addr   R1  */
#define SD_ERASE_WR_BLK_END      33   /* ac   [31:0] data addr   R1  */

  /* Application commands */
#define SD_APP_SET_BUS_WIDTH      6   /* ac   [1:0] bus width    R1  */
#define SD_APP_SD_STATUS         13   /* adtc                    R1  */
#define SD_APP_SEND_NUM_WR_BLKS  22   /* adtc                    R1  */
#define SD_APP_OP_COND           41   /* bcr  [31:0] OCR         R3  */
#define SD_APP_SEND_SCR          51   /* adtc                    R1  */

/* R1 Status bits */
#define R1_OUT_OF_RANGE		BIT(31)	/* er, c */
#define R1_ADDRESS_ERROR	BIT(30)	/* erx, c */
#define R1_BLOCK_LEN_ERROR	BIT(29)	/* er, c */
#define R1_ERASE_SEQ_ERROR      BIT(28)	/* er, c */
#define R1_ERASE_PARAM		BIT(27)	/* ex, c */
#define R1_WP_VIOLATION		BIT(26)	/* erx, c */
#define R1_CARD_IS_LOCKED	BIT(25)	/* sx, a */
#define R1_LOCK_UNLOCK_FAILED	BIT(24)	/* erx, c */
#define R1_COM_CRC_ERROR	BIT(23)	/* er, b */
#define R1_ILLEGAL_COMMAND	BIT(22)	/* er, b */
#define R1_CARD_ECC_FAILED	BIT(21)	/* ex, c */
#define R1_CC_ERROR		BIT(20)	/* erx, c */
#define R1_ERROR		BIT(19)	/* erx, c */
#define R1_UNDERRUN		BIT(18)	/* ex, c */
#define R1_OVERRUN		BIT(17)	/* ex, c */
#define R1_CID_CSD_OVERWRITE	BIT(16)	/* erx, c, CID/CSD overwrite */
#define R1_WP_ERASE_SKIP	BIT(15)	/* sx, c */
#define R1_CARD_ECC_DISABLED	BIT(14)	/* sx, a */
#define R1_ERASE_RESET		BIT(13)	/* sr, c */
#define R1_STATUS(x)            (x & 0xFFFFE000)
#define R1_CURRENT_STATE(x)	((x & 0x00001E00) >> 9)	/* sx, b (4 bits) */
#define R1_READY_FOR_DATA	BIT(8)	/* sx, a */
#define R1_SWITCH_ERROR		BIT(7)	/* sx, c */
#define R1_EXCEPTION_EVENT	BIT(6)	/* sr, a */
#define R1_APP_CMD		BIT(5)	/* sr, c */

#define R1_SET_CURRENT_STATE(status, x) ((status & 0xffffe1ff) | (x << 9))

#define MAX_BLK_COUNT	8

/* Initial registers values */
/* cid register */
#define MID	0xee
#define OID	"LI"
#define PNM	"GBSIM"
#define PRV	0x01
#define MDY	2011
#define MDM	10

/* csd register */
#define CARD_SIZE	0x400000	/* 4MB size */
#define SECTOR_SIZE	4		/* 8KB sector */
#define READ_BL_LEN	9		/* 512 bytes */
#define C_SIZE_MULT	7
#define C_SIZE		((CARD_SIZE >> (READ_BL_LEN + (C_SIZE_MULT + 2))) - 1)

/* ocr register */
/* Power-up, Standard Capacity, Allow all Voltages */
#define OCR_RESET	0x80ffff00

/* rca register */
#define RCA_RESET	0x0000

/* scr register */
#define SCR_RESET	0x02250000

/* card status */
#define CARD_STATUS_RESET	0x00000100 /* Ready for data */

#define GB_SDIO_CAPS	(GB_SDIO_CAP_4_BIT_DATA | GB_SDIO_CAP_8_BIT_DATA | \
			 GB_SDIO_CAP_1_8V_DDR)

#define GB_SDIO_OCR	(GB_SDIO_VDD_21_22 | GB_SDIO_VDD_30_31 | \
			 GB_SDIO_VDD_34_35)

#define STUFF_BITS(rsp, value, start, size) \
	sd_rsp_stuff_bits(rsp, (u_int32_t)value, start, size)

static void sd_rsp_stuff_bits(uint32_t *rsp, uint32_t value, int start,
			      int size)
{
	uint32_t mask = (size < 32 ? 1 << size : 0) - 1;
	int offset;
	int shift = start & 31;

	if ((start + size) > (4 * 32))
		return;

	offset = 3 - (start / 32);
	rsp[offset] |= (value & mask) << shift;
	if (size + shift > 32)
		rsp[offset - 1] |= (value & mask) >> ((32 - shift) % 32);
}

static uint8_t sd_mmc_crc7(void *data, size_t size)
{
	int i, bit;
	uint8_t crc = 0x00;
	uint8_t *d = (uint8_t *)data;

	for (i = 0; i < size; i++, d++)
		for (bit = 7; bit >= 0; bit--) {
			crc <<= 1;
			if ((crc >> 7) ^ ((*d >> bit) & 1))
				crc ^= 0x89;
		}

	return crc;
}

static void sd_reset_cid(void)
{
	uint32_t *c = &sd->cid[0];

	STUFF_BITS(c, MID, 120, 8);
	STUFF_BITS(c, OID[0], 112, 8);
	STUFF_BITS(c, OID[1], 104, 8);
	STUFF_BITS(c, PNM[0], 96, 8);
	STUFF_BITS(c, PNM[1], 88, 8);
	STUFF_BITS(c, PNM[2], 80, 8);
	STUFF_BITS(c, PNM[3], 72, 8);
	STUFF_BITS(c, PNM[4], 64, 8);
	STUFF_BITS(c, PRV, 56, 8);
	STUFF_BITS(c, 0xAAEEAAEE, 24, 32);
	STUFF_BITS(c, MDY - 2000, 12, 8);
	STUFF_BITS(c, MDM, 8, 4);
	STUFF_BITS(c, sd_mmc_crc7(c, 15), 1, 7);
	STUFF_BITS(c, 1, 0, 1);
}

static void sd_reset_csd(void)
{
	uint32_t *c = &sd->csd[0];

	STUFF_BITS(c, 0, 126, 2);		/* csd structure */
	STUFF_BITS(c, 9, 112, 8);		/* data read access time */
	STUFF_BITS(c, 1, 104, 8);		/* data read access time 2 */
	STUFF_BITS(c, 0x5A, 96, 8);		/* max data transfer rate */
	STUFF_BITS(c, 0x05B5, 84, 12);		/* card command classes */
	STUFF_BITS(c, READ_BL_LEN, 80, 4);	/* max read block length */
	STUFF_BITS(c, 1, 79, 1);		/* read block partial */
	STUFF_BITS(c, 1, 78, 1);		/* write block misalign */
	STUFF_BITS(c, 1, 77, 1);		/* read block misalign */
	STUFF_BITS(c, 0, 76, 1);		/* dsr implemented */
	STUFF_BITS(c, C_SIZE, 62, 12);		/* device size c_size */
	STUFF_BITS(c, 3, 59, 3);		/* max read current vdd_min */
	STUFF_BITS(c, 3, 56, 3);		/* max read current vdd_max */
	STUFF_BITS(c, 3, 53, 3);		/* max write current vdd_min */
	STUFF_BITS(c, 3, 50, 3);		/* max write current vdd_max */
	STUFF_BITS(c, C_SIZE_MULT, 47, 3);	/* device size multiplier */
	STUFF_BITS(c, 1, 46, 1);		/* erase block enable */
	STUFF_BITS(c, SECTOR_SIZE, 39, 7);	/* sector size */
	STUFF_BITS(c, 0, 32, 7);		/* write protect group size */
	STUFF_BITS(c, 0, 31, 1);		/* write protect group enable */
	STUFF_BITS(c, 0, 26, 3);		/* write speed factor */
	STUFF_BITS(c, READ_BL_LEN, 22, 4);	/* max write block length */
	STUFF_BITS(c, 1, 21, 1);		/* write block partial */
	STUFF_BITS(c, sd_mmc_crc7(c, 15), 1, 7);
	STUFF_BITS(c, 1, 0, 1);
}

static void sd_reset(void)
{
	sd->state = R1_STATE_IDLE;
	sd->rca = 0;
	sd->ocr = OCR_RESET;
	sd->scr[0] = SCR_RESET;
	sd->card_status = CARD_STATUS_RESET;
	sd_reset_cid();
	sd_reset_csd();
	free(sd->buf);
	sd->buf = calloc(1, CARD_SIZE);
}

static void sd_prepare_r1(void)
{
	sd->rsp[0] = sd->card_status;

	sd->card_status &= ~CLEAR_CONDITION_C;
}

static void sd_prepare_r2(void *reg, ssize_t size)
{
	memcpy(&sd->rsp[0], reg, size);
}

static void sd_prepare_r3(void)
{
	sd->rsp[0] = sd->ocr;
}

static void sd_prepare_r6(void)
{
	/* [31:16] rca [15:0] status bits: 23, 22, 19, 12:0 */
	sd->rsp[0] |= (sd->rca << 16);
	sd->rsp[0] |= (((sd->card_status & 0x00c00000) >> 8) |
		       ((sd->card_status & 0x00020000) >> 6) |
		       (sd->card_status & 0x1fff));

	sd->card_status &= ~(CLEAR_CONDITION_C & 0xc81fff);
}

static void sd_prepare_r7(void)
{
	sd->rsp[0] = sd->vhs;
}

static void sd_prepare_rsp(uint8_t cmd)
{
	memset(&sd->rsp[0], 0, sizeof(sd->rsp));

	if (sd->card_status & R1_ILLEGAL_COMMAND) {
		sd->card_status &= ~R1_ILLEGAL_COMMAND;
		return;
	}
	sd->card_status = R1_SET_CURRENT_STATE(sd->card_status, sd->state);

	switch (cmd) {
	case MMC_GO_IDLE_STATE:
	case MMC_SET_DSR:
		break;
	case MMC_ALL_SEND_CID:
	case MMC_SEND_CID:
		sd_prepare_r2(sd->cid, sizeof(sd->cid));
		break;
	case MMC_SEND_CSD:
		sd_prepare_r2(sd->csd, sizeof(sd->csd));
		break;
	case MMC_SEND_OP_COND:
	case SD_APP_OP_COND:
		sd_prepare_r3();
		break;
	case MMC_SET_RELATIVE_ADDR:
		sd_prepare_r6();
		break;
	case MMC_SEND_EXT_CSD:
		sd_prepare_r7();
		break;
	default:
		sd_prepare_r1();
		break;
	}
}

static void sd_process_apcmd(uint8_t cmd, uint8_t cmd_flags, uint8_t cmd_type,
			     uint32_t cmd_arg)
{
	sd->appcmd = 0;

	switch (cmd) {
	case SD_APP_SET_BUS_WIDTH:
		sd->sd_status[0] |= (cmd_arg & 0x03) << 30;
		break;
	case SD_APP_OP_COND:
		sd->state = R1_STATE_READY;
		break;
	case SD_APP_SEND_SCR:
	case SD_APP_SD_STATUS:
		sd->state = R1_STATE_DATA;
		break;
	default:
		gbsim_debug("sdio: illegal app command %d\n", cmd);
		sd->card_status |= R1_ILLEGAL_COMMAND;
		break;
	}
	sd->xfer_offset = 0;
}

static void sd_process_cmd(uint8_t cmd, uint8_t cmd_flags, uint8_t cmd_type,
			   uint32_t cmd_arg)
{
	gbsim_debug(" sdio: cmd:%d, cmd_flags:%d, cmd_type:%d, cmd_arg=0x%08x\n",
		    cmd, cmd_flags, cmd_type, cmd_arg);

	sd->cmd = cmd;

	if (sd->appcmd) {
		sd->appcmd = 0;
		sd_process_apcmd(cmd, cmd_flags, cmd_type, cmd_arg);
		goto prepare_rsp;
	}

	/* Clear the appcmd flag */
	sd->card_status &= ~R1_APP_CMD;

	switch (cmd) {
	case MMC_GO_IDLE_STATE:
		sd_reset();
		break;
	case MMC_SEND_OP_COND:
		sd->state = R1_STATE_TRAN;
		break;
	case MMC_ALL_SEND_CID:
		sd->state = R1_STATE_IDENT;
		break;
	case MMC_SET_RELATIVE_ADDR:
		sd->state = R1_STATE_STBY;
		sd->rca += 1;
		break;
	case MMC_SET_DSR:
		break;
	case MMC_SELECT_CARD:
		if (sd->state == R1_STATE_STBY)
			sd->state = R1_STATE_TRAN;
		else if (sd->state == R1_STATE_TRAN)
			sd->state = R1_STATE_STBY;
		else if (sd->state == R1_STATE_PRG)
			sd->state = R1_STATE_DIS;
		else if (sd->state == R1_STATE_DIS)
			sd->state = R1_STATE_PRG;
		break;
	case MMC_SEND_EXT_CSD:
		sd->vhs = cmd_arg;
		break;
	case MMC_SEND_CSD:
	case MMC_SEND_CID:
		sd->state = R1_STATE_STBY;
		break;
	case MMC_SWITCH:
		sd->state = R1_STATE_READY;
		break;
	case MMC_STOP_TRANSMISSION:
		sd->state = R1_STATE_TRAN;
		sd->xfer_offset = 0;
		break;
	case MMC_SEND_STATUS:
		break;
	case MMC_GO_INACTIVE_STATE:
		sd->state = R1_STATE_IDLE;
		break;
	case MMC_SET_BLOCKLEN:
		if (cmd_arg > (1 << READ_BL_LEN))
			sd->card_status |= R1_BLOCK_LEN_ERROR;
		else
			sd->blk_len = cmd_arg;
		break;
	case MMC_READ_SINGLE_BLOCK:
	case MMC_READ_MULTIPLE_BLOCK:
		sd->state = R1_STATE_DATA;
		if (cmd_arg + sd->blk_len > CARD_SIZE)
			sd->card_status |= R1_ADDRESS_ERROR;

		sd->xfer_offset = cmd_arg;
		break;
	case MMC_SET_BLOCK_COUNT:
		sd->blk_count = cmd_arg;
		break;
	case MMC_WRITE_BLOCK:
	case MMC_WRITE_MULTIPLE_BLOCK:
		sd->state = R1_STATE_RCV;
		if (cmd_arg + sd->blk_len > CARD_SIZE)
			sd->card_status |= R1_ADDRESS_ERROR;

		sd->xfer_offset = cmd_arg;
		break;
	case MMC_APP_CMD:
		sd->card_status |= R1_APP_CMD;
		sd->appcmd = 1;
		break;
	default:
		gbsim_debug("sdio: illegal command %d\n", cmd);
		sd->card_status |= R1_ILLEGAL_COMMAND;
		break;
	}

prepare_rsp:
	sd_prepare_rsp(cmd);
}

static void sd_transfer_read(uint16_t blocks, uint16_t blksz)
{
	if (sd->state != R1_STATE_DATA) {
		sd->card_status |= R1_ILLEGAL_COMMAND;
		return;
	}

	switch (sd->cmd) {
	case SD_APP_SEND_SCR:
		sd->xfer = (uint8_t *)&sd->scr[0];
		break;
	case SD_APP_SD_STATUS:
		sd->xfer = (uint8_t *)&sd->sd_status[0];
		break;
	case MMC_READ_SINGLE_BLOCK:
	case MMC_READ_MULTIPLE_BLOCK:
		sd->xfer = sd->buf;
		sd->xfer += sd->xfer_offset;
		break;
	default:
		sd->card_status |= R1_ILLEGAL_COMMAND;
		gbsim_debug("sdio: transfer read illegal command %d\n",
			    sd->cmd);
		break;
	}
	sd->xfer_offset += blocks * blksz;
}

static void sd_transfer_write(uint16_t blocks, uint16_t blksz)
{
	if (sd->state != R1_STATE_RCV) {
		sd->card_status |= R1_ILLEGAL_COMMAND;
		return;
	}
	switch (sd->cmd) {
	case MMC_WRITE_BLOCK:
	case MMC_WRITE_MULTIPLE_BLOCK:
		sd->xfer = sd->buf;
		sd->xfer += sd->xfer_offset;
		break;
	default:
		sd->card_status |= R1_ILLEGAL_COMMAND;
		gbsim_debug("sdio: transfer write illegal command %d\n",
			    sd->cmd);
		break;
	}
	sd->xfer_offset += blocks * blksz;
}

static void sd_init(void)
{
	sd = calloc(1, sizeof(*sd));

	sd->max_blk_size = READ_BL_LEN;
	sd->max_blk_count = MAX_BLK_COUNT;

	sd_reset();
}

/* Greybus Specific Code */
static ssize_t sdio_send_card_event(struct op_msg *op_req, uint16_t hd_cport_id,
				    uint8_t event)
{
	uint16_t message_size = sizeof(struct gb_operation_msg_hdr) +
				sizeof(struct gb_sdio_event_request);

	op_req->sdio_event_req.event = event;

	return send_request(hd_cport_id, op_req, message_size, 0,
			GB_SDIO_TYPE_EVENT);
}

static ssize_t sdio_transfer_rsp(struct op_msg *op_rsp, uint16_t hd_cport_id,
				 struct gb_operation_msg_hdr *oph, uint16_t data_blocks,
				 uint16_t data_blksz, uint8_t *data)
{
	size_t payload_size;
	uint16_t message_size;
	uint32_t len;

	len = data_blocks * data_blksz;

	if (sd->state == R1_STATE_RCV)
		payload_size = sizeof(struct gb_sdio_transfer_response);
	else
		payload_size = sizeof(struct gb_sdio_transfer_response) + len;

	if (!sd->xfer || sd->card_status & R1_ILLEGAL_COMMAND) {
		sd->card_status &= ~R1_ILLEGAL_COMMAND;
		sd->state = R1_STATE_TRAN;
		op_rsp->sdio_xfer_rsp.data_blocks = 0;
		op_rsp->sdio_xfer_rsp.data_blksz = 0;
		goto send;
	} else {
		op_rsp->sdio_xfer_rsp.data_blocks = data_blocks;
		op_rsp->sdio_xfer_rsp.data_blksz = data_blksz;
	}

	if (sd->state == R1_STATE_RCV)
		memcpy(sd->xfer, data, len);
	else
		memcpy(&op_rsp->sdio_xfer_rsp.data[0], sd->xfer, len);

send:
	message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
	return send_response(hd_cport_id, op_rsp, message_size,
				oph->operation_id, oph->type,
				PROTOCOL_STATUS_SUCCESS);
}

static ssize_t sdio_command_rsp(struct op_msg *op_rsp, uint16_t hd_cport_id,
				struct gb_operation_msg_hdr *oph)
{
	uint16_t message_size = sizeof(struct gb_sdio_command_response) +
				sizeof(struct gb_operation_msg_hdr);
	int i;

	for (i = 0; i < 4; i++)
		op_rsp->sdio_cmd_rsp.resp[i] = htole32(sd->rsp[i]);

	return send_response(hd_cport_id, op_rsp, message_size,
				oph->operation_id, oph->type,
				PROTOCOL_STATUS_SUCCESS);
}

int sdio_handler(struct gbsim_cport *cport, void *rbuf,
		 size_t rsize, void *tbuf, size_t tsize)
{
	struct gb_operation_msg_hdr *oph;
	struct op_msg *op_req = rbuf;
	struct op_msg *op_rsp;
	size_t payload_size = 0;
	uint16_t message_size;
	uint16_t cport_id = cport->id;
	uint16_t hd_cport_id = cport->hd_cport_id;
	uint16_t data_blocks;
	uint16_t data_blksz;
	uint8_t *data;
	uint8_t module_id;

	uint8_t result = PROTOCOL_STATUS_SUCCESS;

	module_id = cport_to_module_id(cport_id);

	op_rsp = (struct op_msg *)tbuf;
	oph = (struct gb_operation_msg_hdr *)&op_req->header;

	switch (oph->type) {
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		payload_size = sizeof(struct gb_protocol_version_response);
		op_rsp->pv_rsp.major = GB_SDIO_VERSION_MAJOR;
		op_rsp->pv_rsp.minor = GB_SDIO_VERSION_MINOR;
		gbsim_debug("Module %hhu -> AP CPort %hu SDIO protocol version response\n  ",
			    module_id, cport_id);
		break;
	case GB_SDIO_TYPE_GET_CAPABILITIES:
		payload_size = sizeof(struct gb_sdio_get_caps_response);
		op_rsp->sdio_caps_rsp.caps = htole32(GB_SDIO_CAPS);
		op_rsp->sdio_caps_rsp.ocr = htole32(GB_SDIO_OCR);
		op_rsp->sdio_caps_rsp.f_min = htole32(400000);
		op_rsp->sdio_caps_rsp.f_max = htole32(25000000);
		op_rsp->sdio_caps_rsp.max_blk_count = htole16(1024);
		op_rsp->sdio_caps_rsp.max_blk_size = htole16(1024);
		gbsim_debug("Module %hhu -> AP CPort %hu SDIO protocol capabilities response\n  ",
			    module_id, cport_id);
		break;
	case GB_SDIO_TYPE_SET_IOS:
		gbsim_debug("Module %hhu -> AP CPort %hu SDIO protocol set ios response\n  ",
			    module_id, cport_id);
		break;
	case GB_SDIO_TYPE_COMMAND:
		sd_process_cmd(op_req->sdio_cmd_req.cmd,
			       op_req->sdio_cmd_req.cmd_flags,
			       op_req->sdio_cmd_req.cmd_type,
			       le32toh(op_req->sdio_cmd_req.cmd_arg));

		sdio_command_rsp(op_rsp, hd_cport_id, oph);
		return 0;
	case GB_SDIO_TYPE_TRANSFER:
		data_blocks = le16toh(op_req->sdio_xfer_req.data_blocks);
		data_blksz = le16toh(op_req->sdio_xfer_req.data_blksz);
		data = &op_req->sdio_xfer_req.data[0];
		if (op_req->sdio_xfer_req.data_flags & GB_SDIO_DATA_READ)
			sd_transfer_read(data_blocks, data_blksz);
		else
			sd_transfer_write(data_blocks, data_blksz);

		sdio_transfer_rsp(op_rsp, hd_cport_id, oph, data_blocks,
				  data_blksz, data);
		return 0;
	default:
		gbsim_error("sdio operation type %02x not supported\n",
			    oph->type);
		return -EINVAL;
	}

	message_size = sizeof(struct gb_operation_msg_hdr) + payload_size;
	send_response(hd_cport_id, op_rsp, message_size,
				oph->operation_id, oph->type, result);

	/* Simulate a card insert after sending capabilities */
	if (oph->type == GB_SDIO_TYPE_GET_CAPABILITIES)
		sdio_send_card_event(op_req, hd_cport_id,
				     GB_SDIO_CARD_INSERTED);
	return 0;
}

char *sdio_get_operation(uint8_t type)
{
	switch (type) {
	case GB_REQUEST_TYPE_INVALID:
		return "GB_SDIO_TYPE_INVALID";
	case GB_REQUEST_TYPE_PROTOCOL_VERSION:
		return "GB_SDIO_TYPE_PROTOCOL_VERSION";
	case GB_SDIO_TYPE_GET_CAPABILITIES:
		return "GB_SDIO_TYPE_GET_CAPABILITIES";
	case GB_SDIO_TYPE_SET_IOS:
		return "GB_SDIO_TYPE_SET_IOS";
	case GB_SDIO_TYPE_COMMAND:
		return "GB_SDIO_TYPE_COMMAND";
	case GB_SDIO_TYPE_TRANSFER:
		return "GB_SDIO_TYPE_TRANSFER";
	case GB_SDIO_TYPE_EVENT:
		return "GB_SDIO_TYPE_EVENT";
	default:
		return "(Unknown operation)";
	}
}

void sdio_init(void)
{
	sd_init();
}
