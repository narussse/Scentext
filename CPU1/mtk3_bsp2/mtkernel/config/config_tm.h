/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.00.04
 *
 *    Copyright (C) 2006-2021 by Ken Sakamura.
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 *
 *    Released by TRON Forum(http://www.tron.org) at 2021/05/17.
 *
 *----------------------------------------------------------------------
 */

/*
 *	config_tm.h
 *	T-Monitor Configuration Definition
 */

#ifndef __TM_CONFIG_H__
#define __TM_CONFIG_H__

/*---------------------------------------------------------------------- */
/* Select a communication port
 *      Select the communication port used by T-Monitor.
 *         1: Valid  0: Invalid  (Only one of them is valid)
 */
/* CPU1では無効化(0)。SCI8はCPU0がガスコンソール出力に使う物理UARTと同一のため、
 * CPU1側で初期化すると衝突する(mtk3_bsp2/config/config_tm.hと同じ理由)。 */
#define	TM_COM_SERIAL_DEV	(0)	/* Use serial communication device */
#define	TM_COM_NO_DEV		(1)	/* Do not use communication port */

/*---------------------------------------------------------------------- */
/* tm_printf() call setting
 *         1: Valid  0: Invalid
 */
#define	USE_TM_PRINTF		(1)	/* Use tm_printf() & tm_sprintf() calls */
#define	TM_OUTBUF_SZ		(0)	/* Output Buffer size in stack */

#endif /* __TM_CONFIG_H__ */
