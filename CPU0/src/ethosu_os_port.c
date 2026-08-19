/*
 * ethosu_os_port.c
 *
 *  Created on: 2026/08/03
 *      Author: narus
 */

#include <tk/tkernel.h>
#include <stdint.h>
#include "ethosu_driver.h"

/*
 * This project builds with BSP_CFG_HEAP_BYTES == 0 (no newlib heap), so the
 * default malloc-based weak semaphore implementation in ethosu_driver.c can
 * never succeed. Override it with real microT-Kernel semaphore objects.
 */

void *ethosu_semaphore_create(void)
{
    T_CSEM csem = {
        .exinf   = NULL,
        .sematr  = TA_TFIFO,
        .isemcnt = 0,
        .maxsem  = 255,
    };

    ID semid = tk_cre_sem(&csem);
    if (semid <= 0)
    {
        return NULL;
    }

    return (void *)(intptr_t) semid;
}

void ethosu_semaphore_destroy(void *sem)
{
    tk_del_sem((ID)(intptr_t) sem);
}

int ethosu_semaphore_take(void *sem, uint64_t timeout)
{
    TMO tmout = (timeout == ETHOSU_SEMAPHORE_WAIT_FOREVER) ? TMO_FEVR : (TMO) timeout;

    return (tk_wai_sem((ID)(intptr_t) sem, 1, tmout) == E_OK) ? 0 : -1;
}

int ethosu_semaphore_give(void *sem)
{
    return (tk_sig_sem((ID)(intptr_t) sem, 1) == E_OK) ? 0 : -1;
}
