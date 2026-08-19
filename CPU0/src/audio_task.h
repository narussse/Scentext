#ifndef AUDIO_TASK_H
#define AUDIO_TASK_H

#include <tk/tkernel.h>
#include "hal_data.h"

// 外部から呼ぶ関数
void start_microphone_recording(void);
void task_audio_main(INT stacd, void *exinf);

// 外部参照するバッファ
#define DMAC_CHUNK_SAMPLES 1024
#define SAMPLING_RATE      16000

/* NPU/MFCCモデルが1回の推論で受け取る、固定の音声長(学習時と同じなので変更不可)。
 * audio_mfcc.cのTOTAL_FRAMES(311)はこの値に基づいて手計算された定数なので、
 * ここを変える場合はaudio_mfcc.c側の計算・確認も必要。 */
#define REC_SECONDS        5
#define TOTAL_REQUEST_SAMPLES (SAMPLING_RATE * REC_SECONDS)
#define MODEL_HALF_SAMPLES    (((TOTAL_REQUEST_SAMPLES / 2) / DMAC_CHUNK_SAMPLES) * DMAC_CHUNK_SAMPLES)
#define TOTAL_SAMPLES (MODEL_HALF_SAMPLES * 2) /* モデル1回分(REC_SECONDS秒)の入力サンプル数。audio_mfcc.c等が参照 */

/* 実際に「遡って」保持し、保留バッファ/手動録音バッファとして抽出する音声の長さ。
 * モデルの入力窓(REC_SECONDS)より長くしたい場合は、audio_task.cの
 * run_audio_mfcc_multiwindow()がこの長さをREC_SECONDS単位の窓に均等分割し、
 * それぞれ推論にかけていずれか1つでも陽性ならOR判定で「検出」とする
 * (例: 8秒→2分割(先頭5秒+末尾5秒)、12秒→3分割、REC_SECONDS以下なら
 * 分割なしの従来通りの1回推論)。
 *
 * PDM/DMACの転送はハードウェア仕様上32bit固定(PDM_PCM_WIDTH_20_BITS_0_18を
 * 32bit幅のFIFOレジスタから読む)なので、sound_buffer_A/B自体をint16化はできない。
 * 代わりに、sound_buffer_A/Bは小さい生スクラッチ(DMAC_CHUNK_SAMPLES単位)に留め、
 * my_pdm_loop_callback()内で1チャンク完了ごとにint16へ変換し、
 * g_lookback_history(循環リング、int16、audio_task.c)へ追記していく。
 * これにより「遡って保持する」ための大きい方のメモリはint16で済むようになる。 */
#define LOOKBACK_SECONDS   8
#define LOOKBACK_REQUEST_SAMPLES (SAMPLING_RATE * LOOKBACK_SECONDS)
/* リング容量はDMAC_CHUNK_SAMPLES単位に切り上げる(端数分だけ実際の保持時間が
 * わずかに長くなるだけで実害はない)。 */
#define LOOKBACK_TOTAL_SAMPLES \
    ((((LOOKBACK_REQUEST_SAMPLES) + DMAC_CHUNK_SAMPLES - 1) / DMAC_CHUNK_SAMPLES) * DMAC_CHUNK_SAMPLES)

/* PDM/DMACの生スクラッチ用ピンポンバッファ(int32)1本あたりのサンプル数。
 * DMAC_CHUNK_SAMPLES単位の小さいチャンクにすることで、sound_buffer_A/Bの
 * メモリ使用量をLOOKBACK_SECONDSから切り離す(名前は既存コードとの互換のため維持)。 */
#define HALF_SAMPLES DMAC_CHUNK_SAMPLES

extern int32_t sound_buffer_A[];
extern int32_t sound_buffer_B[];
extern volatile uint8_t g_active_buffer_id;

#endif
