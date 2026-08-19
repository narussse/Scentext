#include "gas_sensor.h"
#include <tk/tkernel.h>

/* μT-Kernel 3.0タスク版。Phase 2a以降、CPU0は全体がμT-Kernelタスクとして
 * 動くようになったため、内部の待ちも tk_dly_tsk() を使う(以前のベアメタル期は
 * R_BSP_SoftwareDelay()だった)。タスク本体(app_main.cのtask_gas)から
 * gas_recognition_update()を呼ぶ形にしており、task_4_mainという名前の関数は
 * ここには置いていない。 */

// Seeed Studio ガスセンサーのレジスタコマンド
#define CMD_GM102B 0x01 // NO2
#define CMD_GM302B 0x03 // C2H5CH (エタノール等)
#define CMD_GM502B 0x05 // VOC
#define CMD_GM702B 0x07 // CO

// I2C通信完了とエラーを待つためのフラグ
volatile bool g_i2c_completed = false;
volatile bool g_i2c_error = false;

// FSPコンフィギュレータで指定するI2Cのコールバック関数
void i2c_master_callback(i2c_master_callback_args_t *p_args) {
    if (p_args->event == I2C_MASTER_EVENT_TX_COMPLETE ||
        p_args->event == I2C_MASTER_EVENT_RX_COMPLETE) {
        g_i2c_completed = true;
    } else {
        // ノイズ等による通信エラー発生時
        g_i2c_error = true;
        g_i2c_completed = true; // ループを抜けるために完了フラグも立てる
    }
}

void gas_sensor_init(void) {
    // I2Cモジュールをオープン
    R_IIC_MASTER_Open(&g_i2c_master0_ctrl, &g_i2c_master0_cfg);
}

// 共通の読み取り処理
static uint32_t read_sensor_value(uint8_t cmd) {
    uint8_t rx_buf[4] = {0}; // センサーは4バイトで数値を返してくる
    fsp_err_t err;
    int timeout;

    // 1. どのガスを読み取るか「コマンド」を送信
    g_i2c_completed = false;
    g_i2c_error = false;
    // true = 通信後にストップコンディションを発行せず、そのままReadに繋げる（Restart条件）
    err = R_IIC_MASTER_Write(&g_i2c_master0_ctrl, &cmd, 1, true);
    if (err != FSP_SUCCESS) {
        goto i2c_error_recovery;
    }

    // タイムアウト付きの完了待ち (約500ms)
    timeout = 500;
    while (!g_i2c_completed && timeout > 0) {
        tk_dly_tsk(1);
        timeout--;
    }

    // タイムアウトした、またはコールバックでエラーが通知された場合
    if (!g_i2c_completed || g_i2c_error) {
        goto i2c_error_recovery;
    }

    // 2. センサーから4バイトのデータを読み取る
    g_i2c_completed = false;
    g_i2c_error = false;
    err = R_IIC_MASTER_Read(&g_i2c_master0_ctrl, rx_buf, 4, false);
    if (err != FSP_SUCCESS) {
        goto i2c_error_recovery;
    }

    // タイムアウト付きの完了待ち (約500ms)
    timeout = 500;
    while (!g_i2c_completed && timeout > 0) {
        tk_dly_tsk(1);
        timeout--;
    }

    if (!g_i2c_completed || g_i2c_error) {
        goto i2c_error_recovery;
    }

    // 3. 4バイトのデータを1つの数値（32bit）に結合して正常終了
    uint32_t val = (uint32_t)(rx_buf[0] | (rx_buf[1] << 8) | (rx_buf[2] << 16) | (rx_buf[3] << 24));
    return val;

i2c_error_recovery:
    // 物理的な接触不良などで通信が失敗した場合はバスをリセットする
    R_IIC_MASTER_Close(&g_i2c_master0_ctrl);
    tk_dly_tsk(10); // バスが解放されるのを待機
    R_IIC_MASTER_Open(&g_i2c_master0_ctrl, &g_i2c_master0_cfg);

    // エラー時は0を返す（Python側のグラフでは0に落ちることでエラー発生を視認できる）
    return 0;
}

uint32_t gas_sensor_read_NO2(void)    { return read_sensor_value(CMD_GM102B); }
uint32_t gas_sensor_read_C2H5CH(void) { return read_sensor_value(CMD_GM302B); }
uint32_t gas_sensor_read_VOC(void)    { return read_sensor_value(CMD_GM502B); }
uint32_t gas_sensor_read_CO(void)     { return read_sensor_value(CMD_GM702B); }
