/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2023-2026 by Ken Sakamura.
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 *
 *    Released by TRON Forum(http://www.tron.org) at 2026/04.
 *
 *----------------------------------------------------------------------
 */

/*
 *	sysdef.h
 *
 *	System dependencies definition (EK-RA8P1)
 *	Included also from assembler program.
 */

#ifndef _MTKBSP_SYS_SYSDEF_DEPEND_H_
#define _MTKBSP_SYS_SYSDEF_DEPEND_H_

/* CPU-dependent definition */
/* [修正] 元のBSPパッケージでは誤って ra8m1(RAM終端0x220E0000)を
 * includeしていたが、これはRA8P1(RAM終端0x221D4000)ではない。
 * CPU0側では実害は出ていなかったが(自分のRAM範囲0x220EA000より
 * この誤った終端の方がわずかに大きく収まっていたため)、CPU1側の
 * 同じバグでE_NOMEMが発生したのを機に、両方修正して揃える。 */
#include <sys/sysdepend/ra_fsp/cpu/ra8p1/sysdef.h>

/* [デュアルコア対応] cpu/ra8p1/sysdef.hのINTERNAL_RAM_SIZEはチップ全体
 * (0x22000000-0x221D4000、シングルコア前提)の値。デュアルコア構成では
 * CPU0が実際にリンクされているRAMは0x22000000-0x220EA000だけで、
 * その先(0x220EA000-0x221D4000)はCPU1の領域なので、カーネルの
 * 動的メモリプールがそこまで食い込まないよう、CPU0自身の実サイズに
 * 上書きする(dual_core_CPU0/Debug/memory_regions.lldのRAM_LENGTH=0xea000と一致)。 */
#undef INTERNAL_RAM_SIZE
#define INTERNAL_RAM_SIZE	0x000EA000

/* ------------------------------------------------------------------------ */
/* Clock frequency
 */
#define CPUCLK_MHz	(1000)
#define ICLK_MHz	(250)
#define PCLKA_MHz	(125)
#define PCLKB_MHz	(62)
#define PCLKC_MHz	(125)
#define PCLKD_MHz	(250)
#define PCLKE_MHz	(250)

#define	SYSCLK		(CPUCLK_MHz*1000*1000)	// System clock (Hz)
#define TMCLK_KHz	(CPUCLK_MHz*1000)	// System timer clock input (kHz)
#define TMCLK		(CPUCLK_MHz)		// System timer clock input (MHz)

#endif /* _MTKBSP_TK_SYSDEF_DEPEND_H_ */
