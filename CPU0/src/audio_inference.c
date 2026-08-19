/*
 * audio_inference.c
 *
 *  Created on: 2026/08/02
 *      Author: narus
 */

#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>
#include "audio_inference.h"
#include "hal_data.h"
#include "model.h"

// 初期化処理（NPUの起動）
void audio_inference_init(void) {
    tm_printf((UB*)"[AI Interface] Initializing RUHMI Optimized Model / NPU...\n");

    fsp_err_t status = RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &g_rm_ethosu0_cfg);

    if (status != FSP_SUCCESS) {
        tm_printf((UB*)"[AI Error] Failed to start NPU! status=%d\n", status);
        return;
    }

    tm_printf((UB*)"[AI Interface] Model initialized successfully via RUHMI!\n");
}

// 推論の実行関数
float audio_run_inference(float (*mfcc_matrix)[311]) {
    tm_printf((UB*)"[AI] Running inference with RUHMI / Ethos-U55...\n");

    // 1. MFCC行列（10 × 311）を、RUHMI側の入力バッファにコピー
    float* model_input = (float*)GetModelInputPtr_serving_default_input_layer_0();
    int input_idx = 0;
    for (int m = 0; m < 10; m++) {
        for (int f = 0; f < 311; f++) {
            model_input[input_idx++] = mfcc_matrix[m][f];
        }
    }

    // 2. 推論実行（NPU / CPUで計算される）
    RunModel(false);

    // 3. 結果の取得
    float* model_output = (float*)GetModelOutputPtr_StatefulPartitionedCall_1_0_70021();
    float probability = model_output[0];

    tm_printf((UB*)"[AI Result] Spray Probability: %d%%\n", (int)(probability * 100.0f));
    tm_printf((UB*)"[AI Result] Raw probability x1000000 = %d\n", (int)(probability * 1000000.0f));

    return probability;
}
