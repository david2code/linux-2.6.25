/* Freescale Enhanced Local Bus Controller NAND driver
 *
 * Copyright (c) 2006-2007 Freescale Semiconductor
 *
 * Authors: Nick Spence <nick.spence@freescale.com>,
 *          Scott Wood <scottwood@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>


#define MAX_BANKS 8
#define ERR_BYTE 0xFF /* Value returned for read bytes when read failed */
#define FCM_TIMEOUT_MSECS 500 /* Maximum number of mSecs to wait for FCM */

struct elbc_bank {
	__be32 br;             /**< Base Register  */
#define BR_BA           0xFFFF8000
#define BR_BA_SHIFT             15
#define BR_PS           0x00001800
#define BR_PS_SHIFT             11
#define BR_PS_8         0x00000800  /* Port Size 8 bit */
#define BR_PS_16        0x00001000  /* Port Size 16 bit */
#define BR_PS_32        0x00001800  /* Port Size 32 bit */
#define BR_DECC         0x00000600
#define BR_DECC_SHIFT            9
#define BR_DECC_OFF     0x00000000  /* HW ECC checking and generation off */
#define BR_DECC_CHK     0x00000200  /* HW ECC checking on, generation off */
#define BR_DECC_CHK_GEN 0x00000400  /* HW ECC checking and generation on */
#define BR_WP           0x00000100
#define BR_WP_SHIFT              8
#define BR_MSEL         0x000000E0
#define BR_MSEL_SHIFT            5
#define BR_MS_GPCM      0x00000000  /* GPCM */
#define BR_MS_FCM       0x00000020  /* FCM */
#define BR_MS_SDRAM     0x00000060  /* SDRAM */
#define BR_MS_UPMA      0x00000080  /* UPMA */
#define BR_MS_UPMB      0x000000A0  /* UPMB */
#define BR_MS_UPMC      0x000000C0  /* UPMC */
#define BR_V            0x00000001
#define BR_V_SHIFT               0
#define BR_RES          ~(BR_BA|BR_PS|BR_DECC|BR_WP|BR_MSEL|BR_V)

	__be32 or;             /**< Base Register  */
#define OR0 0x5004
#define OR1 0x500C
#define OR2 0x5014
#define OR3 0x501C
#define OR4 0x5024
#define OR5 0x502C
#define OR6 0x5034
#define OR7 0x503C

#define OR_FCM_AM               0xFFFF8000
#define OR_FCM_AM_SHIFT                 15
#define OR_FCM_BCTLD            0x00001000
#define OR_FCM_BCTLD_SHIFT              12
#define OR_FCM_PGS              0x00000400
#define OR_FCM_PGS_SHIFT                10
#define OR_FCM_CSCT             0x00000200
#define OR_FCM_CSCT_SHIFT                9
#define OR_FCM_CST              0x00000100
#define OR_FCM_CST_SHIFT                 8
#define OR_FCM_CHT              0x00000080
#define OR_FCM_CHT_SHIFT                 7
#define OR_FCM_SCY              0x00000070
#define OR_FCM_SCY_SHIFT                 4
#define OR_FCM_SCY_1            0x00000010
#define OR_FCM_SCY_2            0x00000020
#define OR_FCM_SCY_3            0x00000030
#define OR_FCM_SCY_4            0x00000040
#define OR_FCM_SCY_5            0x00000050
#define OR_FCM_SCY_6            0x00000060
#define OR_FCM_SCY_7            0x00000070
#define OR_FCM_RST              0x00000008
#define OR_FCM_RST_SHIFT                 3
#define OR_FCM_TRLX             0x00000004
#define OR_FCM_TRLX_SHIFT                2
#define OR_FCM_EHTR             0x00000002
#define OR_FCM_EHTR_SHIFT                1
};

struct elbc_regs {
	struct elbc_bank bank[8];
	u8 res0[0x28];
	__be32 mar;             /**< UPM Address Register */
	u8 res1[0x4];
	__be32 mamr;            /**< UPMA Mode Register */
	__be32 mbmr;            /**< UPMB Mode Register */
	__be32 mcmr;            /**< UPMC Mode Register */
	u8 res2[0x8];
	__be32 mrtpr;           /**< Memory Refresh Timer Prescaler Register */
	__be32 mdr;             /**< UPM Data Register */
	u8 res3[0x4];
	__be32 lsor;            /**< Special Operation Initiation Register */
	__be32 lsdmr;           /**< SDRAM Mode Register */
	u8 res4[0x8];
	__be32 lurt;            /**< UPM Refresh Timer */
	__be32 lsrt;            /**< SDRAM Refresh Timer */
	u8 res5[0x8];
	__be32 ltesr;           /**< Transfer Error Status Register */
#define LTESR_BM   0x80000000
#define LTESR_FCT  0x40000000
#define LTESR_PAR  0x20000000
#define LTESR_WP   0x04000000
#define LTESR_ATMW 0x00800000
#define LTESR_ATMR 0x00400000
#define LTESR_CS   0x00080000
#define LTESR_CC   0x00000001
#define LTESR_NAND_MASK (LTESR_FCT | LTESR_PAR | LTESR_CC)
	__be32 ltedr;           /**< Transfer Error Disable Register */
	__be32 lteir;           /**< Transfer Error Interrupt Register */
	__be32 lteatr;          /**< Transfer Error Attributes Register */
	__be32 ltear;           /**< Transfer Error Address Register */
	u8 res6[0xC];
	__be32 lbcr;            /**< Configuration Register */
#define LBCR_LDIS  0x80000000
#define LBCR_LDIS_SHIFT    31
#define LBCR_BCTLC 0x00C00000
#define LBCR_BCTLC_SHIFT   22
#define LBCR_AHD   0x00200000
#define LBCR_LPBSE 0x00020000
#define LBCR_LPBSE_SHIFT   17
#define LBCR_EPAR  0x00010000
#define LBCR_EPAR_SHIFT    16
#define LBCR_BMT   0x0000FF00
#define LBCR_BMT_SHIFT      8
#define LBCR_INIT  0x00040000
	__be32 lcrr;            /**< Clock Ratio Register */
#define LCRR_DBYP    0x80000000
#define LCRR_DBYP_SHIFT      31
#define LCRR_BUFCMDC 0x30000000
#define LCRR_BUFCMDC_SHIFT   28
#define LCRR_ECL     0x03000000
#define LCRR_ECL_SHIFT       24
#define LCRR_EADC    0x00030000
#define LCRR_EADC_SHIFT      16
#define LCRR_CLKDIV  0x0000000F
#define LCRR_CLKDIV_SHIFT     0
	u8 res7[0x8];
	__be32 fmr;             /**< Flash Mode Register */
#define FMR_CWTO     0x0000F000
#define FMR_CWTO_SHIFT       12
#define FMR_BOOT     0x00000800
#define FMR_ECCM     0x00000100
#define FMR_AL       0x00000030
#define FMR_AL_SHIFT          4
#define FMR_OP       0x00000003
#define FMR_OP_SHIFT          0
	__be32 fir;             /**< Flash Instruction Register */
#define FIR_OP0      0xF0000000
#define FIR_OP0_SHIFT        28
#define FIR_OP1      0x0F000000
#define FIR_OP1_SHIFT        24
#define FIR_OP2      0x00F00000
#define FIR_OP2_SHIFT        20
#define FIR_OP3      0x000F0000
#define FIR_OP3_SHIFT        16
#define FIR_OP4      0x0000F000
#define FIR_OP4_SHIFT        12
#define FIR_OP5      0x00000F00
#define FIR_OP5_SHIFT         8
#define FIR_OP6      0x000000F0
#define FIR_OP6_SHIFT         4
#define FIR_OP7      0x0000000F
#define FIR_OP7_SHIFT         0
#define FIR_OP_NOP   0x0	/* No operation and end of sequence */
#define FIR_OP_CA    0x1        /* Issue current column address */
#define FIR_OP_PA    0x2        /* Issue current block+page address */
#define FIR_OP_UA    0x3        /* Issue user defined address */
#define FIR_OP_CM0   0x4        /* Issue command from FCR[CMD0] */
#define FIR_OP_CM1   0x5        /* Issue command from FCR[CMD1] */
#define FIR_OP_CM2   0x6        /* Issue command from FCR[CMD2] */
#define FIR_OP_CM3   0x7        /* Issue command from FCR[CMD3] */
#define FIR_OP_WB    0x8        /* Write FBCR bytes from FCM buffer */
#define FIR_OP_WS    0x9        /* Write 1 or 2 bytes from MDR[AS] */
#define FIR_OP_RB    0xA        /* Read FBCR bytes to FCM buffer */
#define FIR_OP_RS    0xB        /* Read 1 or 2 bytes to MDR[AS] */
#define FIR_OP_CW0   0xC        /* Wait then issue FCR[CMD0] */
#define FIR_OP_CW1   0xD        /* Wait then issue FCR[CMD1] */
#define FIR_OP_RBW   0xE        /* Wait then read FBCR bytes */
#define FIR_OP_RSW   0xE        /* Wait then read 1 or 2 bytes */
	__be32 fcr;             /**< Flash Command Register */
#define FCR_CMD0     0xFF000000
#define FCR_CMD0_SHIFT       24
#define FCR_CMD1     0x00FF0000
#define FCR_CMD1_SHIFT       16
#define FCR_CMD2     0x0000FF00
#define FCR_CMD2_SHIFT        8
#define FCR_CMD3     0x000000FF
#define FCR_CMD3_SHIFT        0
	__be32 fbar;            /**< Flash Block Address Register */
#define FBAR_BLK     0x00FFFFFF
	__be32 fpar;            /**< Flash Page Address Register */
#define FPAR_SP_PI   0x00007C00
#define FPAR_SP_PI_SHIFT     10
#define FPAR_SP_MS   0x00000200
#define FPAR_SP_CI   0x000001FF
#define FPAR_SP_CI_SHIFT      0
#define FPAR_LP_PI   0x0003F000
#define FPAR_LP_PI_SHIFT     12
#define FPAR_LP_MS   0x00000800
#define FPAR_LP_CI   0x000007FF
#define FPAR_LP_CI_SHIFT      0
	__be32 fbcr;            /**< Flash Byte Count Register */
#define FBCR_BC      0x00000FFF
	u8 res11[0x8];
	u8 res8[0xF00];
};

struct fsl_elbc_ctrl;

/* mtd information per set */

struct fsl_elbc_mtd {
	struct mtd_info mtd;
	struct nand_chip chip;
	struct fsl_elbc_ctrl *ctrl;

	struct device *dev;
	int bank;               /* Chip select bank number           */
	u8 __iomem *vbase;      /* Chip select base virtual address  */
	int page_size;          /* NAND page size (0=512, 1=2048)    */
	unsigned int fmr;       /* FCM Flash Mode Register value     */
};

/* overview of the fsl elbc controller */

struct fsl_elbc_ctrl {
	struct nand_hw_control controller;
	struct fsl_elbc_mtd *chips[MAX_BANKS];

	/* device info */
	struct device *dev;
	struct elbc_regs __iomem *regs;
	int irq;
	wait_queue_head_t irq_wait;
	unsigned int irq_status; /* status read from LTESR by irq handler */
	u8 __iomem *addr;        /* Address of assigned FCM buffer        */
	unsigned int page;       /* Last page written to / read from      */
	unsigned int read_bytes; /* Number of bytes read during command   */
	unsigned int column;     /* Saved column from SEQIN               */
	unsigned int index;      /* Pointer to next byte to 'read'        */
	unsigned int status;     /* status read from LTESR after last op  */
	unsigned int mdr;        /* UPM/FCM Data Register value           */
	unsigned int use_mdr;    /* Non zero if the MDR is to be set      */
	unsigned int oob;        /* Non zero if operating on OOB data     */
	char *oob_poi;           /* Place to write ECC after read back    */
};

/* These map to the positions used by the FCM hardware ECC generator */

/* Small Page FLASH with FMR[ECCM] = 0 */
static struct nand_ecclayout fsl_elbc_oob_sp_eccm0 = {
	.eccbytes = 3,
	.eccpos = {6, 7, 8},
	.oobfree = { {0, 5}, {9, 7} },
	.oobavail = 12,
};

/* Small Page FLASH with FMR[ECCM] = 1 */
static struct nand_ecclayout fsl_elbc_oob_sp_eccm1 = {
	.eccbytes = 3,
	.eccpos = {8, 9, 10},
	.oobfree = { {0, 5}, {6, 2}, {11, 5} },
	.oobavail = 12,
};

/* Large Page FLASH with FMR[ECCM] = 0 */
static struct nand_ecclayout fsl_elbc_oob_lp_eccm0 = {
	.eccbytes = 12,
	.eccpos = {6, 7, 8, 22, 23, 24, 38, 39, 40, 54, 55, 56},
	.oobfree = { {1, 5}, {9, 13}, {25, 13}, {41, 13}, {57, 7} },
	.oobavail = 48,
};

/* Large Page FLASH with FMR[ECCM] = 1 */
static struct nand_ecclayout fsl_elbc_oob_lp_eccm1 = {
	.eccbytes = 12,
	.eccpos = {8, 9, 10, 24, 25, 26, 40, 41, 42, 56, 57, 58},
	.oobfree = { {1, 7}, {11, 13}, {27, 13}, {43, 13}, {59, 5} },
	.oobavail = 48,
};

/*=================================*/

/*
 * Set up the FCM hardware block and page address fields, and the fcm
 * structure addr field to point to the correct FCM buffer in memory
 */
static void set_addr(struct mtd_info *mtd, int column, int page_addr, int oob)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_elbc_mtd *priv = chip->priv;
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;
	struct elbc_regs __iomem *lbc = ctrl->regs;
	int buf_num;

	ctrl->page = page_addr;

	out_be32(&lbc->fbar,
	         page_addr >> (chip->phys_erase_shift - chip->page_shift));

	if (priv->page_size) {
		out_be32(&lbc->fpar,
		         ((page_addr << FPAR_LP_PI_SHIFT) & FPAR_LP_PI) |
		         (oob ? FPAR_LP_MS : 0) | column);
		buf_num = (page_addr & 1) << 2;
	} else {
		out_be32(&lbc->fpar,
		         ((page_addr << FPAR_SP_PI_SHIFT) & FPAR_SP_PI) |
		         (oob ? FPAR_SP_MS : 0) | column);
		buf_num = page_addr & 7;
	}

	ctrl->addr = priv->vbase + buf_num * 1024;
	ctrl->index = column;

	/* for OOB data point to the second half of the buffer */
	if (oob)
		ctrl->index += priv->page_size ? 2048 : 512;

	dev_vdbg(ctrl->dev, "set_addr: bank=%d, ctrl->addr=0x%p (0x%p), "
	                    "index %x, pes %d ps %d\n",
	         buf_num, ctrl->addr, priv->vbase, ctrl->index,
	         chip->phys_erase_shift, chip->page_shift);
}

/*
 * execute FCM command and wait for it to complete
 */
static int fsl_elbc_run_command(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_elbc_mtd *priv = chip->priv;
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;
	struct elbc_regs __iomem *lbc = ctrl->regs;

	/* Setup the FMR[OP] to execute without write protection */
	out_be32(&lbc->fmr, priv->fmr | 3);
	if (ctrl->use_mdr)
		out_be32(&lbc->mdr, ctrl->mdr);

	dev_vdbg(ctrl->dev,
	         "fsl_elbc_run_command: fmr=%08x fir=%08x fcr=%08x\n",
	         in_be32(&lbc->fmr), in_be32(&lbc->fir), in_be32(&lbc->fcr));
	dev_vdbg(ctrl->dev,
	         "fsl_elbc_run_command: fbar=%08x fpar=%08x "
	         "fbcr=%08x bank=%d\n",
	         in_be32(&lbc->fbar), in_be32(&lbc->fpar),
	         in_be32(&lbc->fbcr), priv->bank);

	/* execute special operation */
	out_be32(&lbc->lsor, priv->bank);

	/* wait for FCM complete flag or timeout */
	ctrl->irq_status = 0;
	wait_event_timeout(ctrl->irq_wait, ctrl->irq_status,
	                   FCM_TIMEOUT_MSECS * HZ/1000);
	ctrl->status = ctrl->irq_status;

	/* store mdr value in case it was needed */
	if (ctrl->use_mdr)
		ctrl->mdr = in_be32(&lbc->mdr);

	ctrl->use_mdr = 0;

	dev_vdbg(ctrl->dev,
	         "fsl_elbc_run_command: stat=%08x mdr=%08x fmr=%08x\n",
	         ctrl->status, ctrl->mdr, in_be32(&lbc->fmr));

	/* returns 0 on success otherwise non-zero) */
	return ctrl->status == LTESR_CC ? 0 : -EIO;
}

static void fsl_elbc_do_read(struct nand_chip *chip, int oob)
{
	struct fsl_elbc_mtd *priv = chip->priv;
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;
	struct elbc_regs __iomem *lbc = ctrl->regs;

	if (priv->page_size) {
		out_be32(&lbc->fir,
		         (FIR_OP_CW0 << FIR_OP0_SHIFT) |
		         (FIR_OP_CA  << FIR_OP1_SHIFT) |
		         (FIR_OP_PA  << FIR_OP2_SHIFT) |
		         (FIR_OP_CW1 << FIR_OP3_SHIFT) |
		         (FIR_OP_RBW << FIR_OP4_SHIFT));

		out_be32(&lbc->fcr, (NAND_CMD_READ0 << FCR_CMD0_SHIFT) |
		                    (NAND_CMD_READSTART << FCR_CMD1_SHIFT));
	} else {
		out_be32(&lbc->fir,
		         (FIR_OP_CW0 << FIR_OP0_SHIFT) |
		         (FIR_OP_CA  << FIR_OP1_SHIFT) |
		         (FIR_OP_PA  << FIR_OP2_SHIFT) |
		         (FIR_OP_RBW << FIR_OP3_SHIFT));

		if (oob)
			out_be32(&lbc->fcr, NAND_CMD_READOOB << FCR_CMD0_SHIFT);
		else
			out_be32(&lbc->fcr, NAND_CMD_READ0 << FCR_CMD0_SHIFT);
	}
}

/* cmdfunc send commands to the FCM */
static void fsl_elbc_cmdfunc(struct mtd_info *mtd, unsigned int command,
                             int column, int page_addr)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_elbc_mtd *priv = chip->priv;
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;
	struct elbc_regs __iomem *lbc = ctrl->regs;

	ctrl->use_mdr = 0;

	/* clear the read buffer */
	ctrl->read_bytes = 0;
	if (command != NAND_CMD_PAGEPROG)
		ctrl->index = 0;

	switch (command) {
	/* READ0 and READ1 read the entire buffer to use hardware ECC. */
	case NAND_CMD_READ1:
		column += 256;

	/* fall-through */
	case NAND_CMD_READ0:
		dev_dbg(ctrl->dev,
		        "fsl_elbc_cmdfunc: NAND_CMD_READ0, page_addr:"
		        " 0x%x, column: 0x%x.\n", page_addr, column);


		out_be32(&lbc->fbcr, 0); /* read entire page to enable ECC */
		set_addr(mtd, 0, page_addr, 0);

		ctrl->read_bytes = mtd->writesize + mtd->oobsize;
		ctrl->index += column;

		fsl_elbc_do_read(chip, 0);
		fsl_elbc_run_command(mtd);
		return;

	/* READOOB reads only the OOB because no ECC is performed. */
	case NAND_CMD_READOOB:
		dev_vdbg(ctrl->dev,
		         "fsl_elbc_cmdfunc: NAND_CMD_READOOB, page_addr:"
			 " 0x%x, column: 0x%x.\n", page_addr, column);

		out_be32(&lbc->fbcr, mtd->oobsize - column);
		set_addr(mtd, column, page_addr, 1);

		ctrl->read_bytes = mtd->writesize + mtd->oobsize;

		fsl_elbc_do_read(chip, 1);
		fsl_elbc_run_command(mtd);
		return;

	/* READID must read all 5 possible bytes while CEB is active */
	case NAND_CMD_READID:
		dev_vdbg(ctrl->dev, "fsl_elbc_cmdfunc: NAND_CMD_READID.\n");

		out_be32(&lbc->fir, (FIR_OP_CW0 << FIR_OP0_SHIFT) |
		                    (FIR_OP_UA  << FIR_OP1_SHIFT) |
		                    (FIR_OP_RBW << FIR_OP2_SHIFT));
		out_be32(&lbc->fcr, NAND_CMD_READID << FCR_CMD0_SHIFT);
		/* 5 bytes for manuf, device and exts */
		out_be32(&lbc->fbcr, 5);
		ctrl->read_bytes = 5;
		ctrl->use_mdr = 1;
		ctrl->mdr = 0;

		set_addr(mtd, 0, 0, 0);
		fsl_elbc_run_command(mtd);
		return;

	/* ERASE1 stores the block and page address */
	case NAND_CMD_ERASE1:
		dev_vdbg(ctrl->dev,
		         "fsl_elbc_cmdfunc: NAND_CMD_ERASE1, "
		         "page_addr: 0x%x.\n", page_addr);
		set_addr(mtd, 0, page_addr, 0);
		return;

	/* ERASE2 uses the block and page address from ERASE1 */
	case NAND_CMD_ERASE2:
		dev_vdbg(ctrl->dev, "fsl_elbc_cmdfunc: NAND_CMD_ERASE2.\n");

		out_be32(&lbc->fir,
		         (FIR_OP_CW0 << FIR_OP0_SHIFT) |
		         (FIR_OP_PA  << FIR_OP1_SHIFT) |
		         (FIR_OP_CM1 << FIR_OP2_SHIFT));

		out_be32(&lbc->fcr,
		         (NAND_CMD_ERASE1 << FCR_CMD0_SHIFT) |
		         (NAND_CMD_ERASE2 << FCR_CMD1_SHIFT));

		out_be32(&lbc->fbcr, 0);
		ctrl->read_bytes = 0;

		fsl_elbc_run_command(mtd);
		return;

	/* SEQIN sets up the addr buffer and all registers except the length */
	case NAND_CMD_SEQIN: {
		__be32 fcr;
		dev_vdbg(ctrl->dev,
		         "fsl_elbc_cmdfunc: NAND_CMD_SEQIN/PAGE_PROG, "
		         "page_addr: 0x%x, column: 0x%x.\n",
		         page_addr, column);

		ctrl->column = column;
		ctrl->oob = 0;

		fcr = (NAND_CMD_PAGEPROG << FCR_CMD1_SHIFT) |
		      (NAND_CMD_SEQIN << FCR_CMD2_SHIFT);

		if (priv->page_size) {
			out_be32(&lbc->fir,
			         (FIR_OP_CW0 << FIR_OP0_SHIFT) |
			         (FIR_OP_CA  << FIR_OP1_SHIFT) |
			         (FIR_OP_PA  << FIR_OP2_SHIFT) |
			         (FIR_OP_WB  << FIR_OP3_SHIFT) |
			         (FIR_OP_CW1 << FIR_OP4_SHIFT));

			fcr |= NAND_CMD_READ0 << FCR_CMD0_SHIFT;
		} else {
			out_be32(&lbc->fir,
			         (FIR_OP_CW0 << FIR_OP0_SHIFT) |
			         (FIR_OP_CM2 << FIR_OP1_SHIFT) |
			         (FIR_OP_CA  << FIR_OP2_SHIFT) |
			         (FIR_OP_PA  << FIR_OP3_SHIFT) |
			         (FIR_OP_WB  << FIR_OP4_SHIFT) |
			         (FIR_OP_CW1 << FIR_OP5_SHIFT));

			if (column >= mtd->writesize) {
				/* OOB area --> READOOB */
				column -= mtd->writesize;
				fcr |= NAND_CMD_READOOB << FCR_CMD0_SHIFT;
				ctrl->oob = 1;
			} else if (column < 256) {
				/* First 256 bytes --> READ0 */
				fcr |= NAND_CMD_READ0 << FCR_CMD0_SHIFT;
			} else {
				/* Second 256 bytes --> READ1 */
				fcr |= NAND_CMD_READ1 << FCR_CMD0_SHIFT;
			}
		}

		out_be32(&lbc->fcr, fcr);
		set_addr(mtd, column, page_addr, ctrl->oob);
		return;
	}

	/* PAGEPROG reuses all of the setup from SEQIN and adds the length */
	case NAND_CMD_PAGEPROG: {
		int full_page;
		dev_vdbg(ctrl->dev,
		         "fsl_elbc_cmdfunc: NAND_CMD_PAGEPROG "
		         "writing %d bytes.\n", ctrl->index);

		/* if the write did not start at 0 or is not a full page
		 * then set the exact length, otherwise use a full page
		 * write so the HW generates the ECC.
		 */
		if (ctrl->oob || ctrl->column != 0 ||
		    ctrl->index != mtd->writesize + mtd->oobsize) {
			out_be32(&lbc->fbcr, ctrl->index);
			full_page = 0;
		} else {
			out_be32(&lbc->fbcr, 0);
			full_page = 1;
		}

		fsl_elbc_run_command(mtd);

		/* Read back the page in order to fill in the ECC for the
		 * caller.  Is this really needed?
		 */
		if (full_page && ctrl->oob_poi) {
			out_be32(&lbc->fbcr, 3);
			set_addr(mtd, 6, page_addr, 1);

			ctrl->read_bytes = mtd->writesize + 9;

			fsl_elbc_do_read(chip, 1);
			fsl_elbc_run_command(mtd);

			memcpy_fromio(ctrl->oob_poi + 6,
			              &ctrl->addr[ctrl->index], 3);
			ctrl->index += 3;
		}

		ctrl->oob_poi = NULL;
		return;
	}

	/* CMD_STATUS must read the status byte while CEB is active */
	/* Note - it does not wait for the ready line */
	case NAND_CMD_STATUS:
		out_be32(&lbc->fir,
		         (FIR_OP_CM0 << FIR_OP0_SHIFT) |
		         (FIR_OP_RBW << FIR_OP1_SHIFT));
		out_be32(&lbc->fcr, NAND_CMD_STATUS << FCR_CMD0_SHIFT);
		out_be32(&lbc->fbcr, 1);
		set_addr(mtd, 0, 0, 0);
		ctrl->read_bytes = 1;

		fsl_elbc_run_command(mtd);

		/* The chip always seems to report that it is
		 * write-protected, even when it is not.
		 */
		setbits8(ctrl->addr, NAND_STATUS_WP);
		return;

	/* RESET without waiting for the ready line */
	case NAND_CMD_RESET:
		dev_dbg(ctrl->dev, "fsl_elbc_cmdfunc: NAND_CMD_RESET.\n");
		out_be32(&lbc->fir, FIR_OP_CM0 << FIR_OP0_SHIFT);
		out_be32(&lbc->fcr, NAND_CMD_RESET << FCR_CMD0_SHIFT);
		fsl_elbc_run_command(mtd);
		return;

	default:
		dev_err(ctrl->dev,
		        "fsl_elbc_cmdfunc: error, unsupported command 0x%x.\n",
		        command);
	}
}

static void fsl_elbc_select_chip(struct mtd_info *mtd, int chip)
{
	/* The hardware does not seem to support multiple
	 * chips per bank.
	 */
}

/*
 * Write buf to the FCM Controller Data Buffer
 */
static void fsl_elbc_write_buf(struct mtd_info *mtd, const u8 *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_elbc_mtd *priv = chip->priv;
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;
	unsigned int bufsize = mtd->writesize + mtd->oobsize;

	if (len < 0) {
		dev_err(ctrl->dev, "write_buf of %d bytes", len);
		ctrl->status = 0;
		return;
	}

	if ((unsigned int)len > bufsize - ctrl->index) {
		dev_err(ctrl->dev,
		        "write_buf beyond end of buffer "
		        "(%d requested, %u available)\n",
		        len, bufsize - ctrl->index);
		len = bufsize - ctrl->index;
	}

	memcpy_toio(&ctrl->addr[ctrl->index], buf, len);
	ctrl->index += len;
}

/*
 * read a byte from either the FCM hardware buffer if it has any data left
 * otherwise issue a command to read a single byte.
 */
static u8 fsl_elbc_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_elbc_mtd *priv = chip->priv;
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;

	/* If there are still bytes in the FCM, then use the next byte. */
	if (ctrl->index < ctrl->read_bytes)
		return in_8(&ctrl->addr[ctrl->index++]);

	dev_err(ctrl->dev, "read_byte beyond end of buffer\n");
	return ERR_BYTE;
}

/*
 * Read from the FCM Controller Data Buffer
 */
static void fsl_elbc_read_buf(struct mtd_info *mtd, u8 *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_elbc_mtd *priv = chip->priv;
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;
	int avail;

	if (len < 0)
		return;

	avail = min((unsigned int)len, ctrl->read_bytes - ctrl->index);
	memcpy_fromio(buf, &ctrl->addr[ctrl->index], avail);
	ctrl->index += avail;

	if (len > avail)
		dev_err(ctrl->dev,
		        "read_buf beyond end of buffer "
		        "(%d requested, %d available)\n",
		        len, avail);
}

/*
 * Verify buffer against the FCM Controller Data Buffer
 */
static int fsl_elbc_verify_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_elbc_mtd *priv = chip->priv;
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;
	int i;

	if (len < 0) {
		dev_err(ctrl->dev, "write_buf of %d bytes", len);
		return -EINVAL;
	}

	if ((unsigned int)len > ctrl->read_bytes - ctrl->index) {
		dev_err(ctrl->dev,
		        "verify_buf beyond end of buffer "
		        "(%d requested, %u available)\n",
		        len, ctrl->read_bytes - ctrl->index);

		ctrl->index = ctrl->read_bytes;
		return -EINVAL;
	}

	for (i = 0; i < len; i++)
		if (in_8(&ctrl->addr[ctrl->index + i]) != buf[i])
			break;

	ctrl->index += len;
	return i == len && ctrl->status == LTESR_CC ? 0 : -EIO;
}

/* This function is called after Program and Erase Operations to
 * check for success or failure.
 */
static int fsl_elbc_wait(struct mtd_info *mtd, struct nand_chip *chip)
{
	struct fsl_elbc_mtd *priv = chip->priv;
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;
	struct elbc_regs __iomem *lbc = ctrl->regs;

	if (ctrl->status != LTESR_CC)
		return NAND_STATUS_FAIL;

	/* Use READ_STATUS command, but wait for the device to be ready */
	ctrl->use_mdr = 0;
	out_be32(&lbc->fir,
	         (FIR_OP_CW0 << FIR_OP0_SHIFT) |
	         (FIR_OP_RBW << FIR_OP1_SHIFT));
	out_be32(&lbc->fcr, NAND_CMD_STATUS << FCR_CMD0_SHIFT);
	out_be32(&lbc->fbcr, 1);
	set_addr(mtd, 0, 0, 0);
	ctrl->read_bytes = 1;

	fsl_elbc_run_command(mtd);

	if (ctrl->status != LTESR_CC)
		return NAND_STATUS_FAIL;

	/* The chip always seems to report that it is
	 * write-protected, even when it is not.
	 */
	setbits8(ctrl->addr, NAND_STATUS_WP);
	return fsl_elbc_read_byte(mtd);
}

static int fsl_elbc_chip_init_tail(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct fsl_elbc_mtd *priv = chip->priv;
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;
	struct elbc_regs __iomem *lbc = ctrl->regs;
	unsigned int al;

	/* calculate FMR Address Length field */
	al = 0;
	if (chip->pagemask & 0xffff0000)
		al++;
	if (chip->pagemask & 0xff000000)
		al++;

	/* add to ECCM mode set in fsl_elbc_init */
	priv->fmr |= (12 << FMR_CWTO_SHIFT) |  /* Timeout > 12 ms */
	             (al << FMR_AL_SHIFT);

	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->numchips = %d\n",
	        chip->numchips);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->chipsize = %ld\n",
	        chip->chipsize);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->pagemask = %8x\n",
	        chip->pagemask);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->chip_delay = %d\n",
	        chip->chip_delay);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->badblockpos = %d\n",
	        chip->badblockpos);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->chip_shift = %d\n",
	        chip->chip_shift);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->page_shift = %d\n",
	        chip->page_shift);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->phys_erase_shift = %d\n",
	        chip->phys_erase_shift);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->ecclayout = %p\n",
	        chip->ecclayout);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->ecc.mode = %d\n",
	        chip->ecc.mode);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->ecc.steps = %d\n",
	        chip->ecc.steps);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->ecc.bytes = %d\n",
	        chip->ecc.bytes);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->ecc.total = %d\n",
	        chip->ecc.total);
	dev_dbg(ctrl->dev, "fsl_elbc_init: nand->ecc.layout = %p\n",
	        chip->ecc.layout);
	dev_dbg(ctrl->dev, "fsl_elbc_init: mtd->flags = %08x\n", mtd->flags);
	dev_dbg(ctrl->dev, "fsl_elbc_init: mtd->size = %d\n", mtd->size);
	dev_dbg(ctrl->dev, "fsl_elbc_init: mtd->erasesize = %d\n",
	        mtd->erasesize);
	dev_dbg(ctrl->dev, "fsl_elbc_init: mtd->writesize = %d\n",
	        mtd->writesize);
	dev_dbg(ctrl->dev, "fsl_elbc_init: mtd->oobsize = %d\n",
	        mtd->oobsize);

	/* adjust Option Register and ECC to match Flash page size */
	if (mtd->writesize == 512) {
		priv->page_size = 0;
		clrbits32(&lbc->bank[priv->bank].or, ~OR_FCM_PGS);
	} else if (mtd->writesize == 2048) {
		priv->page_size = 1;
		setbits32(&lbc->bank[priv->bank].or, OR_FCM_PGS);
		/* adjust ecc setup if needed */
		if ((in_be32(&lbc->bank[priv->bank].br) & BR_DECC) ==
		    BR_DECC_CHK_GEN) {
			chip->ecc.size = 512;
			chip->ecc.layout = (priv->fmr & FMR_ECCM) ?
			                   &fsl_elbc_oob_lp_eccm1 :
			                   &fsl_elbc_oob_lp_eccm0;
			mtd->ecclayout = chip->ecc.layout;
			mtd->oobavail = chip->ecc.layout->oobavail;
		}
	} else {
		dev_err(ctrl->dev,
		        "fsl_elbc_init: page size %d is not supported\n",
		        mtd->writesize);
		return -1;
	}

	/* The default u-boot configuration on MPC8313ERDB causes errors;
	 * more delay is needed.  This should be safe for other boards
	 * as well.
	 */
	setbits32(&lbc->bank[priv->bank].or, 0x70);
	return 0;
}

static int fsl_elbc_read_page(struct mtd_info *mtd,
                              struct nand_chip *chip,
                              uint8_t *buf)
{
	fsl_elbc_read_buf(mtd, buf, mtd->writesize);
	fsl_elbc_read_buf(mtd, chip->oob_poi, mtd->oobsize);

	if (fsl_elbc_wait(mtd, chip) & NAND_STATUS_FAIL)
		mtd->ecc_stats.failed++;

	return 0;
}

/* ECC will be calculated automatically, and errors will be detected in
 * waitfunc.
 */
static void fsl_elbc_write_page(struct mtd_info *mtd,
                                struct nand_chip *chip,
                                const uint8_t *buf)
{
	struct fsl_elbc_mtd *priv = chip->priv;
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;

	fsl_elbc_write_buf(mtd, buf, mtd->writesize);
	fsl_elbc_write_buf(mtd, chip->oob_poi, mtd->oobsize);

	ctrl->oob_poi = chip->oob_poi;
}

static int fsl_elbc_chip_init(struct fsl_elbc_mtd *priv)
{
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;
	struct elbc_regs __iomem *lbc = ctrl->regs;
	struct nand_chip *chip = &priv->chip;

	dev_dbg(priv->dev, "eLBC Set Information for bank %d\n", priv->bank);

	/* Fill in fsl_elbc_mtd structure */
	priv->mtd.priv = chip;
	priv->mtd.owner = THIS_MODULE;
	priv->fmr = 0; /* rest filled in later */

	/* fill in nand_chip structure */
	/* set up function call table */
	chip->read_byte = fsl_elbc_read_byte;
	chip->write_buf = fsl_elbc_write_buf;
	chip->read_buf = fsl_elbc_read_buf;
	chip->verify_buf = fsl_elbc_verify_buf;
	chip->select_chip = fsl_elbc_select_chip;
	chip->cmdfunc = fsl_elbc_cmdfunc;
	chip->waitfunc = fsl_elbc_wait;

	/* set up nand options */
	chip->options = NAND_NO_READRDY | NAND_NO_AUTOINCR;

	chip->controller = &ctrl->controller;
	chip->priv = priv;

	chip->ecc.read_page = fsl_elbc_read_page;
	chip->ecc.write_page = fsl_elbc_write_page;

	/* If CS Base Register selects full hardware ECC then use it */
	if ((in_be32(&lbc->bank[priv->bank].br) & BR_DECC) ==
	    BR_DECC_CHK_GEN) {
		chip->ecc.mode = NAND_ECC_HW;
		/* put in small page settings and adjust later if needed */
		chip->ecc.layout = (priv->fmr & FMR_ECCM) ?
				&fsl_elbc_oob_sp_eccm1 : &fsl_elbc_oob_sp_eccm0;
		chip->ecc.size = 512;
		chip->ecc.bytes = 3;
	} else {
		/* otherwise fall back to default software ECC */
		chip->ecc.mode = NAND_ECC_SOFT;
	}

	return 0;
}

static int fsl_elbc_chip_remove(struct fsl_elbc_mtd *priv)
{
	struct fsl_elbc_ctrl *ctrl = priv->ctrl;

	nand_release(&priv->mtd);

	if (priv->vbase)
		iounmap(priv->vbase);

	ctrl->chips[priv->bank] = NULL;
	kfree(priv);

	return 0;
}

static int fsl_elbc_chip_probe(struct fsl_elbc_ctrl *ctrl,
                               struct device_node *node)
{
	struct elbc_regs __iomem *lbc = ctrl->regs;
	struct fsl_elbc_mtd *priv;
	struct resource res;
#ifdef CONFIG_MTD_PARTITIONS
	static const char *part_probe_types[]
		= { "cmdlinepart", "RedBoot", NULL };
	struct mtd_partition *parts;
#endif
	int ret;
	int bank;

	/* get, allocate and map the memory resource */
	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		dev_err(ctrl->dev, "failed to get resource\n");
		return ret;
	}

	/* find which chip select it is connected to */
	for (bank = 0; bank < MAX_BANKS; bank++)
		if ((in_be32(&lbc->bank[bank].br) & BR_V) &&
		    (in_be32(&lbc->bank[bank].br) & BR_MSEL) == BR_MS_FCM &&
		    (in_be32(&lbc->bank[bank].br) &
		     in_be32(&lbc->bank[bank].or) & BR_BA)
		     == res.start)
			break;

	if (bank >= MAX_BANKS) {
		dev_err(ctrl->dev, "address did not match any chip selects\n");
		return -ENODEV;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ctrl->chips[bank] = priv;
	priv->bank = bank;
	priv->ctrl = ctrl;
	priv->dev = ctrl->dev;

	priv->vbase = ioremap(res.start, res.end - res.start + 1);
	if (!priv->vbase) {
		dev_err(ctrl->dev, "failed to map chip region\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = fsl_elbc_chip_init(priv);
	if (ret)
		goto err;

	ret = nand_scan_ident(&priv->mtd, 1);
	if (ret)
		goto err;

	ret = fsl_elbc_chip_init_tail(&priv->mtd);
	if (ret)
		goto err;

	ret = nand_scan_tail(&priv->mtd);
	if (ret)
		goto err;

#ifdef CONFIG_MTD_PARTITIONS
	/* First look for RedBoot table or partitions on the command
	 * line, these take precedence over device tree information */
	ret = parse_mtd_partitions(&priv->mtd, part_probe_types, &parts, 0);
	if (ret < 0)
		goto err;

#ifdef CONFIG_MTD_OF_PARTS
	if (ret == 0) {
		ret = of_mtd_parse_partitions(priv->dev, &priv->mtd,
		                              node, &parts);
		if (ret < 0)
			goto err;
	}
#endif

	if (ret > 0)
		add_mtd_partitions(&priv->mtd, parts, ret);
	else
#endif
		add_mtd_device(&priv->mtd);

	printk(KERN_INFO "eLBC NAND device at 0x%zx, bank %d\n",
	       res.start, priv->bank);
	return 0;

err:
	fsl_elbc_chip_remove(priv);
	return ret;
}

static int __devinit fsl_elbc_ctrl_init(struct fsl_elbc_ctrl *ctrl)
{
	struct elbc_regs __iomem *lbc = ctrl->regs;

	/* clear event registers */
	setbits32(&lbc->ltesr, LTESR_NAND_MASK);
	out_be32(&lbc->lteatr, 0);

	/* Enable interrupts for any detected events */
	out_be32(&lbc->lteir, LTESR_NAND_MASK);

	ctrl->read_bytes = 0;
	ctrl->index = 0;
	ctrl->addr = NULL;

	return 0;
}

static int __devexit fsl_elbc_ctrl_remove(struct of_device *ofdev)
{
	struct fsl_elbc_ctrl *ctrl = dev_get_drvdata(&ofdev->dev);
	int i;

	for (i = 0; i < MAX_BANKS; i++)
		if (ctrl->chips[i])
			fsl_elbc_chip_remove(ctrl->chips[i]);

	if (ctrl->irq)
		free_irq(ctrl->irq, ctrl);

	if (ctrl->regs)
		iounmap(ctrl->regs);

	dev_set_drvdata(&ofdev->dev, NULL);
	kfree(ctrl);
	return 0;
}

/* NOTE: This interrupt is also used to report other localbus events,
 * such as transaction errors on other chipselects.  If we want to
 * capture those, we'll need to move the IRQ code into a shared
 * LBC driver.
 */

static irqreturn_t fsl_elbc_ctrl_irq(int irqno, void *data)
{
	struct fsl_elbc_ctrl *ctrl = data;
	struct elbc_regs __iomem *lbc = ctrl->regs;
	__be32 status = in_be32(&lbc->ltesr) & LTESR_NAND_MASK;

	if (status) {
		out_be32(&lbc->ltesr, status);
		out_be32(&lbc->lteatr, 0);

		ctrl->irq_status = status;
		smp_wmb();
		wake_up(&ctrl->irq_wait);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

/* fsl_elbc_ctrl_probe
 *
 * called by device layer when it finds a device matching
 * one our driver can handled. This code allocates all of
 * the resources needed for the controller only.  The
 * resources for the NAND banks themselves are allocated
 * in the chip probe function.
*/

static int __devinit fsl_elbc_ctrl_probe(struct of_device *ofdev,
                                         const struct of_device_id *match)
{
	struct device_node *child;
	struct fsl_elbc_ctrl *ctrl;
	int ret;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	dev_set_drvdata(&ofdev->dev, ctrl);

	spin_lock_init(&ctrl->controller.lock);
	init_waitqueue_head(&ctrl->controller.wq);
	init_waitqueue_head(&ctrl->irq_wait);

	ctrl->regs = of_iomap(ofdev->node, 0);
	if (!ctrl->regs) {
		dev_err(&ofdev->dev, "failed to get memory region\n");
		ret = -ENODEV;
		goto err;
	}

	ctrl->irq = of_irq_to_resource(ofdev->node, 0, NULL);
	if (ctrl->irq == NO_IRQ) {
		dev_err(&ofdev->dev, "failed to get irq resource\n");
		ret = -ENODEV;
		goto err;
	}

	ctrl->dev = &ofdev->dev;

	ret = fsl_elbc_ctrl_init(ctrl);
	if (ret < 0)
		goto err;

	ret = request_irq(ctrl->irq, fsl_elbc_ctrl_irq, 0, "fsl-elbc", ctrl);
	if (ret != 0) {
		dev_err(&ofdev->dev, "failed to install irq (%d)\n",
		        ctrl->irq);
		ret = ctrl->irq;
		goto err;
	}

	for_each_child_of_node(ofdev->node, child)
		if (of_device_is_compatible(child, "fsl,elbc-fcm-nand"))
			fsl_elbc_chip_probe(ctrl, child);

	return 0;

err:
	fsl_elbc_ctrl_remove(ofdev);
	return ret;
}

static const struct of_device_id fsl_elbc_match[] = {
	{
		.compatible = "fsl,elbc",
	},
	{}
};

static struct of_platform_driver fsl_elbc_ctrl_driver = {
	.driver = {
		.name	= "fsl-elbc",
	},
	.match_table = fsl_elbc_match,
	.probe = fsl_elbc_ctrl_probe,
	.remove = __devexit_p(fsl_elbc_ctrl_remove),
};

static int __init fsl_elbc_init(void)
{
	return of_register_platform_driver(&fsl_elbc_ctrl_driver);
}

static void __exit fsl_elbc_exit(void)
{
	of_unregister_platform_driver(&fsl_elbc_ctrl_driver);
}

module_init(fsl_elbc_init);
module_exit(fsl_elbc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Freescale");
MODULE_DESCRIPTION("Freescale Enhanced Local Bus Controller MTD NAND driver");
