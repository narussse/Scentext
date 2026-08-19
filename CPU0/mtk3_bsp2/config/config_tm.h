/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2023-2024 by Ken Sakamura.
 *    This software is distributed under the T-License 2.1.
 *----------------------------------------------------------------------
 *
 *    Released by TRON Forum(http://www.tron.org) at 2024/08.
 *
 *----------------------------------------------------------------------
 */

/*
 *	config_tm.h
 *	T-Monitor Configuration Definition
 */

#ifndef _MTKBSP_TM_CONFIG_H_
#define _MTKBSP_TM_CONFIG_H_

/*---------------------------------------------------------------------- */
/* Select a communication port
 *      Select the communication port used by T-Monitor.
 *         1: Valid  0: Invalid  (Only one of them is valid)
 */
/* 無効化(0)にする。ek_ra8p1向けtm_com.cはSCI8のレジスタを直接叩く実装だが、
 * SCI8はFSPのr_sci_b_uart経由でガスコンソール出力(task_gas_console)に
 * 使っている同一の物理UARTペリフェラルのため、両方が同時に触ると衝突する。
 * tm_putstring/tm_printfはno_device構成にして無害化し、実際のUART出力は
 * FSPドライバ経由のconsole_write()だけに一本化する。 */
#define	TM_COM_SERIAL_DEV	(0)	/* Use serial communication device */
#define	TM_COM_NO_DEV		(1)	/* Do not use communication port */

/*---------------------------------------------------------------------- */
/* tm_printf() call setting
 *         1: Valid  0: Invalid
 */
#define	USE_TM_PRINTF		(1)	/* Use tm_printf() & tm_sprintf() calls */
#define	TM_OUTBUF_SZ		(0)	/* Output Buffer size in stack */

#endif /* _MTKBSP_TM_CONFIG_H_ */
