/*-
 * Copyright (c) 2007 Sepherosa Ziehau.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $DragonFly: src/sys/dev/netif/et/if_etreg.h,v 1.3 2007/10/23 14:28:42 sephe Exp $
 * $FreeBSD$
 */

#ifndef _IF_ETREG_H
#define _IF_ETREG_H

#define	ET_MEM_TXSIZE_EX		182
#define	ET_MEM_RXSIZE_MIN		608
#define	ET_MEM_RXSIZE_DEFAULT		11216
#define	ET_MEM_SIZE			16384
#define	ET_MEM_UNIT			16

/*
 * PCI registers
 *
 * ET_PCIV_ACK_LATENCY_{128,256} are from
 * PCI EXPRESS BASE SPECIFICATION, REV. 1.0a, Table 3-5
 *
 * ET_PCIV_REPLAY_TIMER_{128,256} are from
 * PCI EXPRESS BASE SPECIFICATION, REV. 1.0a, Table 3-4
 */
#define	ET_PCIR_BAR			PCIR_BAR(0)

#define	ET_PCIR_DEVICE_CAPS		0x4C
#define	ET_PCIM_DEVICE_CAPS_MAX_PLSZ	0x7	/* Max playload size */
#define	ET_PCIV_DEVICE_CAPS_PLSZ_128	0x0
#define	ET_PCIV_DEVICE_CAPS_PLSZ_256	0x1

#define	ET_PCIR_DEVICE_CTRL		0x50
#define	ET_PCIM_DEVICE_CTRL_MAX_RRSZ	0x7000	/* Max read request size */
#define	ET_PCIV_DEVICE_CTRL_RRSZ_2K	0x4000

#define	ET_PCIR_MAC_ADDR0		0xA4
#define	ET_PCIR_MAC_ADDR1		0xA8

#define	ET_PCIR_EEPROM_STATUS		0xB2	/* XXX undocumented */
#define	ET_PCIM_EEPROM_STATUS_ERROR	0x4C

#define	ET_PCIR_ACK_LATENCY		0xC0
#define	ET_PCIV_ACK_LATENCY_128		237
#define	ET_PCIV_ACK_LATENCY_256		416

#define	ET_PCIR_REPLAY_TIMER		0xC2
#define	ET_REPLAY_TIMER_RX_L0S_ADJ	250	/* XXX infered from default */
#define	ET_PCIV_REPLAY_TIMER_128	(711 + ET_REPLAY_TIMER_RX_L0S_ADJ)
#define	ET_PCIV_REPLAY_TIMER_256	(1248 + ET_REPLAY_TIMER_RX_L0S_ADJ)

#define	ET_PCIR_L0S_L1_LATENCY		0xCF

/*
 * CSR
 */
#define	ET_TXQUEUE_START		0x0000
#define	ET_TXQUEUE_END			0x0004
#define	ET_RXQUEUE_START		0x0008
#define	ET_RXQUEUE_END			0x000C
#define	ET_QUEUE_ADDR(addr)		(((addr) / ET_MEM_UNIT) - 1)
#define	ET_QUEUE_ADDR_START		0
#define	ET_QUEUE_ADDR_END		ET_QUEUE_ADDR(ET_MEM_SIZE)

#define	ET_PM				0x0010
#define	ET_PM_SYSCLK_GATE		0x00000008
#define	ET_PM_TXCLK_GATE		0x00000010
#define	ET_PM_RXCLK_GATE		0x00000020

#define	ET_INTR_STATUS			0x0018
#define	ET_INTR_MASK			0x001C

#define	ET_SWRST			0x0028
#define	ET_SWRST_TXDMA			0x00000001
#define	ET_SWRST_RXDMA			0x00000002
#define	ET_SWRST_TXMAC			0x00000004
#define	ET_SWRST_RXMAC			0x00000008
#define	ET_SWRST_MAC			0x00000010
#define	ET_SWRST_MAC_STAT		0x00000020
#define	ET_SWRST_MMC			0x00000040
#define	ET_SWRST_SELFCLR_DISABLE	0x80000000

#define	ET_MSI_CFG			0x0030

#define	ET_LOOPBACK			0x0034

#define	ET_TIMER			0x0038

#define	ET_TXDMA_CTRL			0x1000
#define	ET_TXDMA_CTRL_HALT		0x00000001
#define	ET_TXDMA_CTRL_CACHE_THR_MASK	0x000000F0
#define	ET_TXDMA_CTRL_SINGLE_EPKT	0x00000100	/* ??? */

#define	ET_TX_RING_HI			0x1004
#define	ET_TX_RING_LO			0x1008
#define	ET_TX_RING_CNT			0x100C

#define	ET_TX_STATUS_HI			0x101C
#define	ET_TX_STATUS_LO			0x1020

#define	ET_TX_READY_POS			0x1024
#define	ET_TX_READY_POS_INDEX_MASK	0x000003FF
#define	ET_TX_READY_POS_WRAP		0x00000400

#define	ET_TX_DONE_POS			0x1060
#define	ET_TX_DONE_POS_INDEX_MASK	0x0000003FF
#define	ET_TX_DONE_POS_WRAP		0x000000400

#define	ET_RXDMA_CTRL			0x2000
#define	ET_RXDMA_CTRL_HALT		0x00000001
#define	ET_RXDMA_CTRL_RING0_SIZE_MASK	0x00000300
#define	ET_RXDMA_CTRL_RING0_128		0x00000000	/* 127 */
#define	ET_RXDMA_CTRL_RING0_256		0x00000100	/* 255 */
#define	ET_RXDMA_CTRL_RING0_512		0x00000200	/* 511 */
#define	ET_RXDMA_CTRL_RING0_1024	0x00000300	/* 1023 */
#define	ET_RXDMA_CTRL_RING0_ENABLE	0x00000400
#define	ET_RXDMA_CTRL_RING1_SIZE_MASK	0x00001800
#define	ET_RXDMA_CTRL_RING1_2048	0x00000000	/* 2047 */
#define	ET_RXDMA_CTRL_RING1_4096	0x00000800	/* 4095 */
#define	ET_RXDMA_CTRL_RING1_8192	0x00001000	/* 8191 */
#define	ET_RXDMA_CTRL_RING1_16384	0x00001800	/* 16383 (9022?) */
#define	ET_RXDMA_CTRL_RING1_ENABLE	0x00002000
#define	ET_RXDMA_CTRL_HALTED		0x00020000

#define	ET_RX_STATUS_LO			0x2004
#define	ET_RX_STATUS_HI			0x2008

#define	ET_RX_INTR_NPKTS		0x200C
#define	ET_RX_INTR_DELAY		0x2010

#define	ET_RXSTAT_LO			0x2020
#define	ET_RXSTAT_HI			0x2024
#define	ET_RXSTAT_CNT			0x2028

#define	ET_RXSTAT_POS			0x2030
#define	ET_RXSTAT_POS_INDEX_MASK	0x00000FFF
#define	ET_RXSTAT_POS_WRAP		0x00001000

#define	ET_RXSTAT_MINCNT		0x2038

#define	ET_RX_RING0_LO			0x203C
#define	ET_RX_RING0_HI			0x2040
#define	ET_RX_RING0_CNT			0x2044

#define	ET_RX_RING0_POS			0x204C
#define	ET_RX_RING0_POS_INDEX_MASK	0x000003FF
#define	ET_RX_RING0_POS_WRAP		0x00000400

#define	ET_RX_RING0_MINCNT		0x2054

#define	ET_RX_RING1_LO			0x2058
#define	ET_RX_RING1_HI			0x205C
#define	ET_RX_RING1_CNT			0x2060

#define	ET_RX_RING1_POS			0x2068
#define	ET_RX_RING1_POS_INDEX		0x000003FF
#define	ET_RX_RING1_POS_WRAP		0x00000400

#define	ET_RX_RING1_MINCNT		0x2070

#define	ET_TXMAC_CTRL			0x3000
#define	ET_TXMAC_CTRL_ENABLE		0x00000001
#define	ET_TXMAC_CTRL_FC_DISABLE	0x00000008

#define	ET_TXMAC_FLOWCTRL		0x3010

#define	ET_RXMAC_CTRL			0x4000
#define	ET_RXMAC_CTRL_ENABLE		0x00000001
#define	ET_RXMAC_CTRL_NO_PKTFILT	0x00000004
#define	ET_RXMAC_CTRL_WOL_DISABLE	0x00000008

#define	ET_WOL_CRC			0x4004
#define	ET_WOL_SA_LO			0x4010
#define	ET_WOL_SA_HI			0x4014
#define	ET_WOL_MASK			0x4018

#define	ET_UCAST_FILTADDR1		0x4068
#define	ET_UCAST_FILTADDR2		0x406C
#define	ET_UCAST_FILTADDR3		0x4070

#define	ET_MULTI_HASH			0x4074

#define	ET_PKTFILT			0x4084
#define	ET_PKTFILT_BCAST		0x00000001
#define	ET_PKTFILT_MCAST		0x00000002
#define	ET_PKTFILT_UCAST		0x00000004
#define	ET_PKTFILT_FRAG			0x00000008
#define	ET_PKTFILT_MINLEN_MASK		0x007F0000
#define	ET_PKTFILT_MINLEN_SHIFT		16

#define	ET_RXMAC_MC_SEGSZ		0x4088
#define	ET_RXMAC_MC_SEGSZ_ENABLE	0x00000001
#define	ET_RXMAC_MC_SEGSZ_FC		0x00000002
#define	ET_RXMAC_MC_SEGSZ_MAX_MASK	0x000003FC
#define	ET_RXMAC_SEGSZ(segsz)		((segsz) / ET_MEM_UNIT)
#define	ET_RXMAC_CUT_THRU_FRMLEN	8074

#define	ET_RXMAC_MC_WATERMARK		0x408C
#define	ET_RXMAC_SPACE_AVL		0x4094

#define	ET_RXMAC_MGT			0x4098
#define	ET_RXMAC_MGT_PASS_ECRC		0x00000010
#define	ET_RXMAC_MGT_PASS_ELEN		0x00000020
#define	ET_RXMAC_MGT_PASS_ETRUNC	0x00010000
#define	ET_RXMAC_MGT_CHECK_PKT		0x00020000

#define	ET_MAC_CFG1			0x5000
#define	ET_MAC_CFG1_TXEN		0x00000001
#define	ET_MAC_CFG1_SYNC_TXEN		0x00000002
#define	ET_MAC_CFG1_RXEN		0x00000004
#define	ET_MAC_CFG1_SYNC_RXEN		0x00000008
#define	ET_MAC_CFG1_TXFLOW		0x00000010
#define	ET_MAC_CFG1_RXFLOW		0x00000020
#define	ET_MAC_CFG1_LOOPBACK		0x00000100
#define	ET_MAC_CFG1_RST_TXFUNC		0x00010000
#define	ET_MAC_CFG1_RST_RXFUNC		0x00020000
#define	ET_MAC_CFG1_RST_TXMC		0x00040000
#define	ET_MAC_CFG1_RST_RXMC		0x00080000
#define	ET_MAC_CFG1_SIM_RST		0x40000000
#define	ET_MAC_CFG1_SOFT_RST		0x80000000

#define	ET_MAC_CFG2			0x5004
#define	ET_MAC_CFG2_FDX			0x00000001
#define	ET_MAC_CFG2_CRC			0x00000002
#define	ET_MAC_CFG2_PADCRC		0x00000004
#define	ET_MAC_CFG2_LENCHK		0x00000010
#define	ET_MAC_CFG2_BIGFRM		0x00000020
#define	ET_MAC_CFG2_MODE_MII		0x00000100
#define	ET_MAC_CFG2_MODE_GMII		0x00000200
#define	ET_MAC_CFG2_PREAMBLE_LEN_MASK	0x0000F000
#define	ET_MAC_CFG2_PREAMBLE_LEN_SHIFT	12

#define	ET_IPG				0x5008
#define	ET_IPG_B2B_MASK			0x0000007F
#define	ET_IPG_MINIFG_MASK		0x0000FF00
#define	ET_IPG_NONB2B_2_MASK		0x007F0000
#define	ET_IPG_NONB2B_1_MASK		0x7F000000
#define	ET_IPG_B2B_SHIFT		0
#define	ET_IPG_MINIFG_SHIFT		8
#define	ET_IPG_NONB2B_2_SHIFT		16
#define	ET_IPG_NONB2B_1_SHIFT		24

#define	ET_MAC_HDX			0x500C
#define	ET_MAC_HDX_COLLWIN_MASK		0x000003FF
#define	ET_MAC_HDX_REXMIT_MAX_MASK	0x0000F000
#define	ET_MAC_HDX_EXC_DEFER		0x00010000
#define	ET_MAC_HDX_NOBACKOFF		0x00020000
#define	ET_MAC_HDX_BP_NOBACKOFF		0x00040000
#define	ET_MAC_HDX_ALT_BEB		0x00080000
#define	ET_MAC_HDX_ALT_BEB_TRUNC_MASK	0x00F00000
#define	ET_MAC_HDX_COLLWIN_SHIFT	0
#define	ET_MAC_HDX_REXMIT_MAX_SHIFT	12
#define	ET_MAC_HDX_ALT_BEB_TRUNC_SHIFT	20

#define	ET_MAX_FRMLEN			0x5010

#define	ET_MII_CFG			0x5020
#define	ET_MII_CFG_CLKRST		0x00000007
#define	ET_MII_CFG_PREAMBLE_SUP		0x00000010
#define	ET_MII_CFG_SCAN_AUTOINC		0x00000020
#define	ET_MII_CFG_RST			0x80000000

#define	ET_MII_CMD			0x5024
#define	ET_MII_CMD_READ			0x00000001

#define	ET_MII_ADDR			0x5028
#define	ET_MII_ADDR_REG_MASK		0x0000001F
#define	ET_MII_ADDR_PHY_MASK		0x00001F00
#define	ET_MII_ADDR_REG_SHIFT		0
#define	ET_MII_ADDR_PHY_SHIFT		8

#define	ET_MII_CTRL			0x502C
#define	ET_MII_CTRL_VALUE_MASK		0x0000FFFF
#define	ET_MII_CTRL_VALUE_SHIFT		0

#define	ET_MII_STAT			0x5030
#define	ET_MII_STAT_VALUE_MASK		0x0000FFFF

#define	ET_MII_IND			0x5034
#define	ET_MII_IND_BUSY			0x00000001
#define	ET_MII_IND_INVALID		0x00000004

#define	ET_MAC_CTRL			0x5038
#define	ET_MAC_CTRL_MODE_MII		0x01000000
#define	ET_MAC_CTRL_LHDX		0x02000000
#define	ET_MAC_CTRL_GHDX		0x04000000

#define	ET_MAC_ADDR1			0x5040
#define	ET_MAC_ADDR2			0x5044

/* MAC statistics counters. */
#define	ET_STAT_PKTS_64			0x6080
#define	ET_STAT_PKTS_65_127		0x6084
#define	ET_STAT_PKTS_128_255		0x6088
#define	ET_STAT_PKTS_256_511		0x608C
#define	ET_STAT_PKTS_512_1023		0x6090
#define	ET_STAT_PKTS_1024_1518		0x6094
#define	ET_STAT_PKTS_1519_1522		0x6098
#define	ET_STAT_RX_BYTES		0x609C
#define	ET_STAT_RX_FRAMES		0x60A0
#define	ET_STAT_RX_CRC_ERR		0x60A4
#define	ET_STAT_RX_MCAST		0x60A8
#define	ET_STAT_RX_BCAST		0x60AC
#define	ET_STAT_RX_CTL			0x60B0
#define	ET_STAT_RX_PAUSE		0x60B4
#define	ET_STAT_RX_UNKNOWN_CTL		0x60B8
#define	ET_STAT_RX_ALIGN_ERR		0x60BC
#define	ET_STAT_RX_LEN_ERR		0x60C0
#define	ET_STAT_RX_CODE_ERR		0x60C4
#define	ET_STAT_RX_CS_ERR		0x60C8
#define	ET_STAT_RX_RUNT			0x60CC
#define	ET_STAT_RX_OVERSIZE		0x60D0
#define	ET_STAT_RX_FRAG			0x60D4
#define	ET_STAT_RX_JABBER		0x60D8
#define	ET_STAT_RX_DROP			0x60DC
#define	ET_STAT_TX_BYTES		0x60E0
#define	ET_STAT_TX_FRAMES		0x60E4
#define	ET_STAT_TX_MCAST		0x60E8
#define	ET_STAT_TX_BCAST		0x60EC
#define	ET_STAT_TX_PAUSE		0x60F0
#define	ET_STAT_TX_DEFER		0x60F4
#define	ET_STAT_TX_EXCESS_DEFER		0x60F8
#define	ET_STAT_TX_SINGLE_COL		0x60FC
#define	ET_STAT_TX_MULTI_COL		0x6100
#define	ET_STAT_TX_LATE_COL		0x6104
#define	ET_STAT_TX_EXCESS_COL		0x6108
#define	ET_STAT_TX_TOTAL_COL		0x610C
#define	ET_STAT_TX_PAUSE_HONOR		0x6110
#define	ET_STAT_TX_DROP			0x6114
#define	ET_STAT_TX_JABBER		0x6118
#define	ET_STAT_TX_CRC_ERR		0x611C
#define	ET_STAT_TX_CTL			0x6120
#define	ET_STAT_TX_OVERSIZE		0x6124
#define	ET_STAT_TX_UNDERSIZE		0x6128
#define	ET_STAT_TX_FRAG			0x612C

#define	ET_MMC_CTRL			0x7000
#define	ET_MMC_CTRL_ENABLE		0x00000001
#define	ET_MMC_CTRL_ARB_DISABLE		0x00000002
#define	ET_MMC_CTRL_RXMAC_DISABLE	0x00000004
#define	ET_MMC_CTRL_TXMAC_DISABLE	0x00000008
#define	ET_MMC_CTRL_TXDMA_DISABLE	0x00000010
#define	ET_MMC_CTRL_RXDMA_DISABLE	0x00000020
#define	ET_MMC_CTRL_FORCE_CE		0x00000040

/*
 * Interrupts
 */
#define	ET_INTR_TXEOF			0x00000008
#define	ET_INTR_TXDMA_ERROR		0x00000010
#define	ET_INTR_RXEOF			0x00000020
#define	ET_INTR_RXRING0_LOW		0x00000040
#define	ET_INTR_RXRING1_LOW		0x00000080
#define	ET_INTR_RXSTAT_LOW		0x00000100
#define	ET_INTR_RXDMA_ERROR		0x00000200
#define	ET_INTR_TIMER			0x00004000
#define	ET_INTR_WOL			0x00008000
#define	ET_INTR_PHY			0x00010000
#define	ET_INTR_TXMAC			0x00020000
#define	ET_INTR_RXMAC			0x00040000
#define	ET_INTR_MAC_STATS		0x00080000
#define	ET_INTR_SLAVE_TO		0x00100000

#define	ET_INTRS			(ET_INTR_TXEOF | \
					 ET_INTR_RXEOF | \
					 ET_INTR_TIMER)

/*
 * RX ring position uses same layout
 */
#define	ET_RX_RING_POS_INDEX_MASK	0x000003FF
#define	ET_RX_RING_POS_WRAP		0x00000400

/*
 * PCI IDs
 */
#define	PCI_VENDOR_LUCENT		0x11C1
#define	PCI_PRODUCT_LUCENT_ET1310	0xED00		/* ET1310 10/100/1000M Ethernet */
#define	PCI_PRODUCT_LUCENT_ET1310_FAST	0xED01		/* ET1310 10/100M Ethernet */

#endif	/* !_IF_ETREG_H */
