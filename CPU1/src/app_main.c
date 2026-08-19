/*
 * app_main.c
 *
 * μT-Kernel 3.0 Phase 2-B: GLCDC表示(glcdc_display_init()+描画ループ、
 * hal_entry.cで定義)と、ESP-01S経由のntfy.sh通知(task_net、同じく
 * hal_entry.cで定義)を、それぞれμT-Kernelタスクとして起動する。
 * task_netはベストエフォートのネットワークI/Oなので、task_displayより
 * 優先度を低くしてある(表示の描画タイミングに影響させないため)。
 * CPU1はTM_COM_NO_DEV構成(config_tm.h参照)のため、tm_putstring等の出力は
 * どこにも送られない(無害な no-op)。
 */
#include <tk/tkernel.h>

extern void display_task_start(void);
extern void net_task_start(void);

EXPORT INT usermain(void)
{
    display_task_start();
    net_task_start();

    tk_exd_tsk();
    return 0;
}
