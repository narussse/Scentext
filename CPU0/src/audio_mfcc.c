/*
 * audio_mfcc.c
 *
 *  Created on: 2026/08/02
 *      Author: narus
 */

#include <arm_math.h>
#include <math.h>
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "audio_mfcc.h"
#include "audio_task.h" // TOTAL_SAMPLES (80000) などの定義を使用
#include "audio_inference.h"   // ← 追加
#include "audio_status.h"

/* app_main.c 側のFSPドライバ経由UART送信。CPU0では tm_printf の送信経路
 * (SCI8直叩き)を無効化しているため、書式化だけ tm_sprintf(ハードウェアに
 * 触れない)で行い、実際の送信はこちらに一本化する(SCI8の二重管理を避ける)。 */
extern void console_write(const char *str);

// ==========================================
// パラメータ定義
// ==========================================
#define N_FFT      512
#define HOP_LENGTH 256
#define N_MELS     40
#define N_MFCC     10
#define SR         16000

// フレーム数 = (80000 - 512) / 256 + 1 = 311 フレーム
#define TOTAL_FRAMES 311

// ==========================================
// 💡 PythonからエクスポートしたCMSIS互換の係数群
// (※ Pythonのスクリプト側で出力したC言語配列の数値をここに貼り付けてください)
// ==========================================
// 他のファイル（hal_entry.cなど）に実体があることを伝える
extern const float32_t window_coefs[512];
extern const uint32_t filter_pos[40];
extern const uint32_t filter_lengths[40];
extern const float32_t filter_coefs[490];
extern const float32_t dct_coefs[400];

// ==========================================
// モデルの入力窓(TOTAL_SAMPLES、MODEL_WINDOW_SECONDS秒分)のMFCCを計算し、
// NPU推論の確率をそのまま返す。複数窓に分割してのOR判定などの上位ロジックは
// 呼び出し側(audio_task.cのrun_audio_mfcc_multiwindow())が担当する。
// ==========================================
float run_audio_mfcc_process(int16_t *input_raw_audio) {
    console_write("[MFCC] Starting MFCC extraction...\r\n");

    // 1. CMSIS-DSP MFCCインスタンスの初期化
    arm_mfcc_instance_f32 mfcc_inst;
    arm_status status = arm_mfcc_init_f32(
        &mfcc_inst,
        N_FFT,
        N_MELS,
        N_MFCC,
        (float32_t*)dct_coefs,
        (uint32_t*)filter_pos,
        (uint32_t*)filter_lengths,
        (float32_t*)filter_coefs,
        (float32_t*)window_coefs
    );

    if (status != ARM_MATH_SUCCESS) {
        console_write("[MFCC Error] arm_mfcc_init_f32 failed!\r\n");
        return 0.0f;
    }

    // 作業用バッファ（FFTやメルフィルタ計算用）
    static float32_t float_frame[N_FFT];
    static float32_t mfcc_out[N_MFCC];
    static float32_t tmp_buffer[N_FFT * 2];

    // 全311フレーム分のMFCCを格納するメモリ (10 × 311)
    // ※スタック溢れを防ぐため static またはヒープ確保を推奨
    static float32_t all_mfcc_matrix[N_MFCC][TOTAL_FRAMES];

    int frame_idx = 0;

    // 2. ホップ長 (256サンプル) ずつズラしながらフレーム単位で計算
    for (int start_idx = 0; (start_idx + N_FFT) <= TOTAL_SAMPLES; start_idx += HOP_LENGTH) {
        if (frame_idx >= TOTAL_FRAMES) break;

        // A. int16_t から float32_t (-1.0 ~ 1.0) に変換
        for (int i = 0; i < N_FFT; i++) {
            float_frame[i] = (float32_t)input_raw_audio[start_idx + i] / 32768.0f;
        }

        // B. 1フレーム分のMFCC計算を実行
        arm_mfcc_f32(&mfcc_inst, float_frame, mfcc_out, tmp_buffer);

        // C. 結果をマトリクスに保存
        for (int m = 0; m < N_MFCC; m++) {
            all_mfcc_matrix[m][frame_idx] = mfcc_out[m];
        }

        frame_idx++;
    }

    {
        char buf[64];
        tm_sprintf((UB*)buf, (UB*)"[MFCC] Calculation complete! Total frames: %d\r\n", frame_idx);
        console_write(buf);
    }

    // 3. 【インスタンス正規化（ファイルごと・フレームごとの平均引き算）の適用】
    // 各MFCC次元（0〜9）ごとに、311フレーム分の平均と標準偏差を求めて正規化します
    for (int m = 0; m < N_MFCC; m++) {
        float32_t sum = 0.0f;
        for (int f = 0; f < TOTAL_FRAMES; f++) {
            sum += all_mfcc_matrix[m][f];
        }
        float32_t mean = sum / (float32_t)TOTAL_FRAMES;

        float32_t var_sum = 0.0f;
        for (int f = 0; f < TOTAL_FRAMES; f++) {
            float32_t diff = all_mfcc_matrix[m][f] - mean;
            var_sum += diff * diff;
        }
        float32_t std = sqrtf(var_sum / (float32_t)TOTAL_FRAMES);

        // 正規化の適用 (x - mean) / (std + 1e-7)
        for (int f = 0; f < TOTAL_FRAMES; f++) {
            all_mfcc_matrix[m][f] = (all_mfcc_matrix[m][f] - mean) / (std + 1e-7f);
        }
    }

    console_write("[MFCC] Instance normalization applied successfully.\r\n");

    // 診断: 実際にMFCCデータが入力ごとに変化しているか確認
    // tm_printf((UB*)"[Diag] MFCC sample values (m=0, f=0..4): %d.%03d %d.%03d %d.%03d %d.%03d %d.%03d\n",
    //            (int)all_mfcc_matrix[0][0], (int)fabsf((all_mfcc_matrix[0][0] - (int)all_mfcc_matrix[0][0]) * 1000),
    //            (int)all_mfcc_matrix[0][1], (int)fabsf((all_mfcc_matrix[0][1] - (int)all_mfcc_matrix[0][1]) * 1000),
    //            (int)all_mfcc_matrix[0][2], (int)fabsf((all_mfcc_matrix[0][2] - (int)all_mfcc_matrix[0][2]) * 1000),
    //            (int)all_mfcc_matrix[0][3], (int)fabsf((all_mfcc_matrix[0][3] - (int)all_mfcc_matrix[0][3]) * 1000),
    //            (int)all_mfcc_matrix[0][4], (int)fabsf((all_mfcc_matrix[0][4] - (int)all_mfcc_matrix[0][4]) * 1000));

    // 完成したMFCC行列を推論モデルに渡して実行する。
    // このウィンドウ単独の陽性/陰性の最終判定・ステータス表示への反映は
    // 呼び出し側(複数窓のOR判定を行うrun_audio_mfcc_multiwindow())が行う。
    float probability = audio_run_inference(all_mfcc_matrix);

    {
        char buf[64];
        tm_sprintf((UB*)buf, (UB*)"[RESULT] window probability: %d%%\r\n", (int)(probability * 100.0f));
        console_write(buf);
    }

    return probability;
}
