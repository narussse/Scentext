#ifndef GAS_SENSOR_H
#define GAS_SENSOR_H

#include <stdbool.h>
#include "hal_data.h"

/* mtk3bsp2_ra8p1_ek_grove_pdm_glcdc の Application/gas_sensor.h から、
 * task_4_main(μT-Kernelタスク本体)の宣言を除いたもの(タスク本体はapp_main.cの
 * task_gasにある)。読み取りロジック自体は同一。 */

// 初期化関数
void gas_sensor_init(void);

// 各ガスデータの読み取り関数
uint32_t gas_sensor_read_NO2(void);
uint32_t gas_sensor_read_C2H5CH(void);
uint32_t gas_sensor_read_VOC(void);
uint32_t gas_sensor_read_CO(void);

#endif /* GAS_SENSOR_H */
