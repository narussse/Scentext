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
 * デュアルコア構成でCPU1側(RAM 0x220ea000-0x221d4000)がこの誤ったRAM終端
 * より上位に配置されるため、カーネルの空きメモリプールが負のサイズになり
 * 即座にE_NOMEM(-33)でInitial Task作成に失敗する原因になっていた。 */
#include <sys/sysdepend/ra_fsp/cpu/ra8p1/sysdef.h>


/* ------------------------------------------------------------------------ */
/* Clock frequency
 *
 * [修正] このファイルはCPU0(Cortex-M85)向けの値(CPUCLK_MHz=1000, PLL1P/1)を
 * 元にしていたが、CPU1(Cortex-M33)は同じPLL1Pから /4 分周された
 * CPUCLK1(=250MHz)で動作している(dual_core_CPU1/ra_gen/bsp_clock_cfg.hの
 * BSP_CFG_CPUCLK1_DIV参照)。CPUCLK_MHzはsys_timer.hのSysTick周期計算に
 * そのまま使われるため、1000MHzのままだとCPU1側で実際の4倍の時間
 * (tk_dly_tsk(300)が実時間で約1.2秒)かかってしまっていた。
 * ICLK/PCLKx(バス系クロック)は両コアで共通なので変更不要。 */
#define CPUCLK_MHz	(250)
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
