/*
 * gas_recognition.h
 *
 * ガス4チャンネル(NO2/C2H5CH/VOC/CO)の立上り検知とアルコール認識。
 * PC側の gas_audio_realtime_plot_v6.py と同じロジック・同じパラメータで
 * 実装している(学習済みモデルは gas_recognition_model.h)。
 */
#ifndef GAS_RECOGNITION_H
#define GAS_RECOGNITION_H

#include <stdint.h>

/* task_4_main のループから、ガス4チャンネルを読み取るたびに呼び出す */
void gas_recognition_update(int32_t no2, int32_t c2h5ch, int32_t voc, int32_t co);

/* いずれかのチャンネルが現在RISING中なら1、そうでなければ0(CPU1への状態通知用) */
int gas_recognition_is_any_rising(void);

/* センサーのヒーターがまだウォームアップ中(NO2チャンネルのスロープがまだ
 * 安定していない)なら1、安定済みなら0。安定するまでは立上り検知トリガーを
 * 抑制する(gas_recognition_update()内で実施)。 */
int gas_recognition_is_warming_up(void);

/*
 * CPU1のステータス行2段目("解析中... 残りNs"等)用の状態。
 * PC側(gas_audio_realtime_plot_v8.py)のgas_text_objが表す3状態(IDLE/収集中/結果)に加え、
 * 検知した瞬間だけ短く出す「立上り検出！」フラッシュ状態(RISING_DETECTED)を持つ:
 *   IDLE             : 何も表示しない
 *   RISING_DETECTED  : 立上りを検知した直後 約1秒間だけ表示(COLLECTINGの先頭部分)
 *   COLLECTING       : POST_WINDOW(5秒)分のデータ収集中(RISING_DETECTED区間の後)。残り秒数を表示
 *   RESULT_ALCOHOL / RESULT_CLEAR : 推論結果を一定時間(RECOGNITION_DISPLAY_SEC)だけ表示
 */
typedef enum {
    GAS_LINE2_IDLE = 0,
    GAS_LINE2_COLLECTING = 1,
    GAS_LINE2_RESULT_ALCOHOL = 2,
    GAS_LINE2_RESULT_CLEAR = 3,
    GAS_LINE2_RISING_DETECTED = 4,
} gas_line2_state_t;

/* 200msごとに呼び出す。out_secondsは表示用の残り秒数(切り上げ)。 */
void gas_recognition_get_line2(int *out_state, int *out_seconds);

/*
 * ガス側 <-> 音声側(audio_task.c) の連携用シグナル。
 *
 *   GAS_AUDIO_SIGNAL_STAGE   : 立上りを検知した瞬間にセット。音声側は直近5秒間を
 *                              「保留バッファ」に抽出するだけ(まだMFCC/NPU推論はしない)。
 *   GAS_AUDIO_SIGNAL_PROCESS : ガス推論でアルコールと確定した瞬間(onsetの5秒後)に
 *                              セット。音声側は保留バッファに対してMFCC/NPU推論を実行する。
 *   GAS_AUDIO_SIGNAL_DISCARD : ガス推論で非アルコールと確定した瞬間にセット。
 *                              音声側は保留バッファを処理せず、ロックだけ解除する。
 *
 * 音声側は読み取ったら GAS_AUDIO_SIGNAL_NONE に戻すこと。
 *
 * 💡 保留バッファは、ガスの推論結果(PROCESS/DISCARDのどちらか)が確定するまでは
 * 絶対に上書きされない。これはガス側の状態機械(g_collecting による排他制御、
 * アルコール確定後のクールダウン)により、STAGE が再び来るのは必ず前回の
 * PROCESS/DISCARD より後になる、という設計で保証している
 * (加えて、音声側にも念のための明示的なロックフラグを持たせている)。
 */
typedef enum {
    GAS_AUDIO_SIGNAL_NONE = 0,
    GAS_AUDIO_SIGNAL_STAGE = 1,
    GAS_AUDIO_SIGNAL_PROCESS = 2,
    GAS_AUDIO_SIGNAL_DISCARD = 3,
} gas_audio_signal_t;

extern volatile gas_audio_signal_t g_gas_audio_signal;

#endif /* GAS_RECOGNITION_H */
