/*
 * Copyright (C) 2010 Marek Vasut <marek.vasut@gmail.com>
 *
 * Loosely based on the old code and Linux's PXA MMC driver
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <config.h>
#include <common.h>
#include <malloc.h>

#include <mmc.h>
#include <asm/errno.h>
#include <asm/arch/hardware.h>

#include "pxa_mmc.h"

/* 1000uS (in wait cycles below it's 100 x 10uS waits) */
#define	PXA_MMC_TIMEOUT	100

static int pxa_mmc_wait(int mask)
{
	int timeout = PXA_MMC_TIMEOUT;

	/* Wait until the clock are off */
	while (!(MMC_STAT & mask) && --timeout)
		udelay(10);

	/* The clock refused to stop, scream and die a painful death */
	if (!timeout)
		return -ETIMEDOUT;

	return 0;
}

static int pxa_mmc_stop_clock(void)
{
	int timeout = PXA_MMC_TIMEOUT;

	/* If the clock aren't running, exit */
	if (!(MMC_STAT & MMC_STAT_CLK_EN))
		return 0;

	/* Tell the controller to turn off the clock */
	MMC_STRPCL = MMC_STRPCL_STOP_CLK;

	/* Wait until the clock are off */
	while ((MMC_STAT & MMC_STAT_CLK_EN) && --timeout)
		udelay(10);

	/* The clock refused to stop, scream and die a painful death */
	if (!timeout)
		return -ETIMEDOUT;

	/* The clock stopped correctly */
	return 0;
}

static int pxa_mmc_start_cmd(struct mmc_cmd *cmd, unsigned long cmdat)
{
	int ret;

	/* The card can send a "busy" response */
	if (cmd->flags & MMC_RSP_BUSY)
		cmdat |= MMC_CMDAT_BUSY;

	/* Inform the controller about response type */
	switch (cmd->resp_type) {
		case MMC_RSP_R1:
		case MMC_RSP_R1b:
			cmdat |= MMC_CMDAT_R1;
			break;
		case MMC_RSP_R2:
			cmdat |= MMC_CMDAT_R2;
			break;
		case MMC_RSP_R3:
			cmdat |= MMC_CMDAT_R3;
			break;
		default:
			break;
	}

	/* Load command and it's arguments into the controller */
	MMC_CMD = cmd->cmdidx;
	MMC_ARGH = cmd->cmdarg >> 16;
	MMC_ARGL = cmd->cmdarg & 0xffff;
	MMC_CMDAT = cmdat;

	/* Start the controller clock and wait until they are started */
	MMC_STRPCL = MMC_STRPCL_START_CLK;

	ret = pxa_mmc_wait(MMC_STAT_CLK_EN);
	if (ret)
		return ret;

	/* Correct and happy end */
	return 0;
}

static int pxa_mmc_cmd_done(struct mmc_cmd *cmd)
{
	unsigned long a, b, c;
	int i;
	int stat;

	/* Read the controller status */
	stat = MMC_STAT;

	/*
	 * Linux says:
	 * Did I mention this is Sick.  We always need to
	 * discard the upper 8 bits of the first 16-bit word.
	 */
	a = MMC_RES & 0xffff;
	for (i = 0; i < 4; i++) {
		b = MMC_RES & 0xffff;
		c = MMC_RES & 0xffff;
		cmd->response[i] = (a << 24) | (b << 8) | (c >> 8);
		a = c;
	}

	/* The command response didn't arrive */
	if (stat & MMC_STAT_TIME_OUT_RESPONSE)
		return -ETIMEDOUT;
	else if (stat & MMC_STAT_RES_CRC_ERROR && cmd->flags & MMC_RSP_CRC) {
#ifdef	PXAMMC_CRC_SKIP
		if (cmd->flags & MMC_RSP_136 && cmd->response[0] & 0x80000000)
			printf("Ignoring CRC, this may be dangerous!\n");
		else
#endif
		return -EILSEQ;
	}

	/* The command response was successfully read */
	return 0;
}

static int pxa_mmc_do_read_xfer(struct mmc_data *data)
{
	unsigned long len;
	int i;
	int ret;

	len = data->blocks * data->blocksize;

	while (len) {
		/* The controller has data ready */
		if (MMC_I_REG & MMC_I_REG_RXFIFO_RD_REQ) {
			i = min(len, PXAMMC_FIFO_SIZE);

			while (i--) {
				*data->dest++ = *((volatile uchar *)&MMC_RXFIFO);
				len--;
			}
		}

		if (MMC_STAT & MMC_STAT_ERRORS)
			return -EIO;
	}

	/* Wait for the transmission-done interrupt */
	ret = pxa_mmc_wait(MMC_STAT_DATA_TRAN_DONE);
	if (ret)
		return ret;

	return 0;
}

static int pxa_mmc_do_write_xfer(struct mmc_data *data)
{
	unsigned long len;
	int i, bytes;
	int ret;

	len = data->blocks * data->blocksize;

	while (len) {
		/* The controller is ready to receive data */
		if (MMC_I_REG & MMC_I_REG_TXFIFO_WR_REQ) {
			bytes = min(len, PXAMMC_FIFO_SIZE);

			for (i = 0; i < bytes; i++)
				MMC_TXFIFO = *data->src++;

			if (bytes < 32)
				MMC_PRTBUF = MMC_PRTBUF_BUF_PART_FULL;

			len -= bytes;
		}

		if (MMC_STAT & MMC_STAT_ERRORS)
			return -EIO;
	}

	/* Wait for the transmission-done interrupt */
	ret = pxa_mmc_wait(MMC_STAT_DATA_TRAN_DONE);
	if (ret)
		return ret;

	/* Wait until the data are really written to the card */
	ret = pxa_mmc_wait(MMC_STAT_PRG_DONE);
	if (ret)
		return ret;

	return 0;
}

static int pxa_mmc_request(struct mmc *mmc, struct mmc_cmd *cmd,
				struct mmc_data *data)
{
	unsigned long cmdat = 0;
	int ret;

	/* Stop the controller */
	ret = pxa_mmc_stop_clock();
	if (ret)
		return ret;

	/* If we're doing data transfer, configure the controller accordingly */
	if (data) {
		MMC_NOB = data->blocks;
		MMC_BLKLEN = data->blocksize;
		/* This delay can be optimized, but stick with max value */
		MMC_RDTO = 0xffff;
		cmdat |= MMC_CMDAT_DATA_EN;
		if (data->flags & MMC_DATA_WRITE)
			cmdat |= MMC_CMDAT_WRITE;
	}

	/* Run in 4bit mode if the card can do it */
	if (mmc->bus_width == 4)
		cmdat |= MMC_CMDAT_SD_4DAT;

	/* Execute the command */
	ret = pxa_mmc_start_cmd(cmd, cmdat);
	if (ret)
		return ret;

	/* Wait until the command completes */
	ret = pxa_mmc_wait(MMC_STAT_END_CMD_RES);
	if (ret)
		return ret;

	/* Read back the result */
	ret = pxa_mmc_cmd_done(cmd);
	if (ret)
		return ret;

	/* In case there was a data transfer scheduled, do it */
	if (data) {
		if (data->flags & MMC_DATA_WRITE)
			pxa_mmc_do_write_xfer(data);
		else
			pxa_mmc_do_read_xfer(data);
	}

	return 0;
}

static void pxa_mmc_set_ios(struct mmc *mmc)
{
	unsigned long tmp;
	unsigned long pxa_mmc_clock;

	/* Set clock to the card */
	if (mmc->clock) {
		/* PXA3xx can do 26MHz with special settings */
		if (mmc->clock == 26000000)
			MMC_CLKRT = 0x7;
		else {
			pxa_mmc_clock = 0;
			tmp = mmc->f_max / mmc->clock;
			tmp += tmp % 2;
			while (tmp > 1) {
				pxa_mmc_clock++;
				tmp >>= 1;
			}
			MMC_CLKRT = pxa_mmc_clock;
		}
	} else
		pxa_mmc_stop_clock();
}

static int pxa_mmc_setup(struct mmc *mmc)
{
	/* Make sure the clock are stopped */
	pxa_mmc_stop_clock();
	/* Turn off SPI mode */
	MMC_SPI = MMC_SPI_DISABLE;
	/* Set up maximum timeout to wait for command response */
	MMC_RESTO = MMC_RES_TO_MAX;
	/* Mask all interrupts */
	MMC_I_MASK = ~(MMC_I_MASK_TXFIFO_WR_REQ | MMC_I_MASK_RXFIFO_RD_REQ);
	return 0;
}

int pxa_mmc_init(bd_t *bis)
{
	struct mmc *mmc;

	mmc = malloc(sizeof(struct mmc));

	if (!mmc)
		return -ENOMEM;
	sprintf(mmc->name, "PXA MMC");
	mmc->send_cmd	= pxa_mmc_request;
	mmc->set_ios	= pxa_mmc_set_ios;
	mmc->init	= pxa_mmc_setup;

	mmc->voltages	= MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->f_max	= PXAMMC_MAX_SPEED;
	mmc->f_min	= PXAMMC_MIN_SPEED;
	mmc->host_caps	= PXAMMC_HOST_CAPS;
	mmc_register(mmc);

	return 0;
}

int cpu_mmc_init(bd_t *bis)
{
	return pxa_mmc_init(bis);
}
